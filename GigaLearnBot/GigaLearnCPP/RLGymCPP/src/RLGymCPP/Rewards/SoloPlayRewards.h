#pragma once
#include "Reward.h"
#include "../CommonValues.h"
#include <atomic>
#include <map>
#include <algorithm>

namespace RLGC {

// ============================================================================
// SoloPlayComboReward
// ============================================================================
// Rewards a player who makes >=2 distinct touches on the ball and converts
// the sequence into a goal-threatening ball state without any other player
// intervening. "Distinct" requires the ball to travel >= MIN_INTER_TOUCH_DIST
// with >= MIN_GAP seconds between touches — filters out continuous dribbles
// and rapid spam taps.
//
// State machine per player:
//   IDLE → (own touch) → BETWEEN_TOUCHES → (own touch + gap + dist met) →
//   CONVERSION_WINDOW → (ball threatening) → fire + COOLDOWN → IDLE
//
// Any other player touching the ball resets the sequence.
// Additional own touches in CONVERSION_WINDOW extend the window and add bonus.
//
// W&B: SoloPlay/Combo/Qualified2Touch, /Converted2Touch, /Converted3PlusTouch,
//      /ConversionRate, /AvgTouchCount, /AvgBallSpeedAtThreat
// ============================================================================
class SoloPlayComboReward : public Reward {
private:
    enum class Phase { IDLE, BETWEEN_TOUCHES, CONVERSION_WINDOW, COOLDOWN };

    struct PlayerState {
        Phase phase          = Phase::IDLE;
        float elapsed        = 0.0f;   // time in current phase
        float interTouchDist = 0.0f;   // ball distance accumulated between touches
        Vec   lastBallPos    = {};
        bool  lastBallPosSet = false;
        int   touchCount     = 0;
    };

    mutable std::map<uint32_t, PlayerState> m_states;

    static constexpr float MIN_GAP              = 0.25f;    // min seconds between touch 1 and touch 2
    static constexpr float MIN_INTER_TOUCH_DIST = 400.0f;   // ball must travel this far between touches (UU)
    static constexpr float TOUCH2_WINDOW        = 4.0f;     // touch 2 must arrive within this many seconds
    static constexpr float CONVERSION_WINDOW    = 1.5f;     // seconds after touch 2 to detect goal threat
    static constexpr float MIN_THREAT_SPEED     = 1000.0f;  // ball speed in goal direction (UU/s)
    static constexpr float THREAT_DOT_MIN       = 0.5f;     // cos(60°) — ball vel toward goal center
    static constexpr float BASE_REWARD          = 4.0f;
    static constexpr float TOUCH_BONUS          = 1.5f;     // per touch beyond 2
    static constexpr int   MAX_TOUCH_BONUS      = 3;        // cap bonus (max payout = 4 + 3*1.5 = 8.5f)
    static constexpr float COOLDOWN_SECS        = 3.0f;

    inline static std::atomic<int> s_qualified     { 0 };
    inline static std::atomic<int> s_converted2    { 0 };
    inline static std::atomic<int> s_converted3p   { 0 };
    inline static std::atomic<int> s_touchCountSum { 0 };
    inline static std::atomic<int> s_ballSpeedSum  { 0 };

    bool OtherPlayerTouchedBall(const Player& me, const GameState& state) const {
        for (const auto& p : state.players) {
            if (p.carId != me.carId && p.ballTouchedStep) return true;
        }
        return false;
    }

    bool IsBallThreatening(const Player& player, const GameState& state) const {
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < MIN_THREAT_SPEED) return false;
        float oppGoalY = (player.team == Team::BLUE) ? 5120.0f : -5120.0f;
        Vec goalCenter(0.0f, oppGoalY, 0.0f);
        Vec goalDir = (goalCenter - state.ball.pos).Normalized();
        return state.ball.vel.Normalized().Dot(goalDir) >= THREAT_DOT_MIN;
    }

public:
    struct Metrics {
        int qualified     = 0;
        int converted2    = 0;
        int converted3p   = 0;
        int touchCountSum = 0;
        int ballSpeedSum  = 0;
    };

    static Metrics GetAndResetMetrics() {
        return {
            s_qualified.exchange(0),
            s_converted2.exchange(0),
            s_converted3p.exchange(0),
            s_touchCountSum.exchange(0),
            s_ballSpeedSum.exchange(0)
        };
    }

    virtual void Reset(const GameState& initialState) override {
        m_states.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        uint32_t id = player.carId;
        PlayerState& ps = m_states[id];

        // Accumulate ball travel distance (used while in BETWEEN_TOUCHES)
        float ballMoved = 0.0f;
        if (ps.lastBallPosSet)
            ballMoved = (state.ball.pos - ps.lastBallPos).Length();
        ps.lastBallPos    = state.ball.pos;
        ps.lastBallPosSet = true;

        switch (ps.phase) {

        case Phase::IDLE:
            if (player.ballTouchedStep) {
                ps.phase          = Phase::BETWEEN_TOUCHES;
                ps.elapsed        = 0.0f;
                ps.interTouchDist = 0.0f;
                ps.touchCount     = 1;
            }
            break;

        case Phase::BETWEEN_TOUCHES:
            ps.elapsed        += dt;
            ps.interTouchDist += ballMoved;

            if (OtherPlayerTouchedBall(player, state)) {
                ps.phase = Phase::IDLE;
                break;
            }
            if (ps.elapsed > TOUCH2_WINDOW) {
                ps.phase = Phase::IDLE;
                break;
            }
            if (player.ballTouchedStep) {
                if (ps.interTouchDist >= MIN_INTER_TOUCH_DIST && ps.elapsed >= MIN_GAP) {
                    // Valid distinct touch 2 — enter conversion window
                    ps.touchCount++;
                    ps.phase   = Phase::CONVERSION_WINDOW;
                    ps.elapsed = 0.0f;
                    s_qualified.fetch_add(1);
                } else {
                    // Gap or distance not met — reset, treat this as a new touch 1
                    ps.elapsed        = 0.0f;
                    ps.interTouchDist = 0.0f;
                    ps.touchCount     = 1;
                }
            }
            break;

        case Phase::CONVERSION_WINDOW:
            ps.elapsed += dt;

            if (OtherPlayerTouchedBall(player, state)) {
                ps.phase = Phase::IDLE;
                break;
            }
            if (player.ballTouchedStep) {
                ps.touchCount++;
                ps.elapsed = 0.0f;  // additional own touch resets conversion timer
            }
            if (IsBallThreatening(player, state)) {
                int extra  = std::min(ps.touchCount - 2, MAX_TOUCH_BONUS);
                float payout = BASE_REWARD + extra * TOUCH_BONUS;

                if (ps.touchCount == 2) s_converted2.fetch_add(1);
                else                    s_converted3p.fetch_add(1);
                s_touchCountSum.fetch_add(ps.touchCount);
                s_ballSpeedSum.fetch_add(static_cast<int>(state.ball.vel.Length()));

                ps.phase   = Phase::COOLDOWN;
                ps.elapsed = 0.0f;
                return payout;
            }
            if (ps.elapsed > CONVERSION_WINDOW) {
                ps.phase = Phase::IDLE;
            }
            break;

        case Phase::COOLDOWN:
            ps.elapsed += dt;
            if (ps.elapsed >= COOLDOWN_SECS)
                ps.phase = Phase::IDLE;
            break;
        }

        return 0.0f;
    }
};


// ============================================================================
// DualThreatReward
// ============================================================================
// Continuous shaping reward (2v2/3v3 only) for a ball carrier who simultaneously
// has a solo shot trajectory toward the opponent goal AND an unmarked teammate
// in the scoring zone. Creates the "goalkeeper's dilemma" — opponent must choose
// which threat to cover.
//
// Fires per step when ALL of:
//   1. Player recently touched ball (within RECENT_TOUCH seconds)
//   2. Ball is in opponent's half
//   3. Ball has goal-directed velocity component >= MIN_GOAL_VEL (UU/s)
//   4. >=1 teammate is within SCORING_ZONE_DIST of opp goal AND no opponent
//      within MARKING_DIST of that teammate (unmarked)
//
// W&B: SoloPlay/DualThreat/Frames, /EligibleFrames, /ActivationRate
// ============================================================================
class DualThreatReward : public Reward {
private:
    static constexpr float RECENT_TOUCH_WINDOW = 0.6f;    // seconds since last touch to count as carrier
    static constexpr float MIN_GOAL_VEL        = 600.0f;  // ball velocity component toward goal (UU/s)
    static constexpr float SCORING_ZONE_DIST   = 2200.0f; // distance from opp goal center (UU)
    static constexpr float MARKING_DIST        = 800.0f;  // opponent within this = teammate is marked
    static constexpr float PER_STEP_REWARD     = 0.05f;

    mutable std::map<uint32_t, float> m_timeSinceTouch;

    inline static std::atomic<int> s_dualFrames { 0 };
    inline static std::atomic<int> s_eligFrames { 0 };

    Vec OppGoalCenter(const Player& player) const {
        return Vec(0.0f, (player.team == Team::BLUE) ? 5120.0f : -5120.0f, 0.0f);
    }

    bool HasUnmarkedTeammateInScoringZone(const Player& me, const GameState& state) const {
        Vec oppGoal = OppGoalCenter(me);
        for (const auto& tm : state.players) {
            if (tm.team != me.team || tm.carId == me.carId || tm.isDemoed) continue;
            if ((tm.pos - oppGoal).Length() > SCORING_ZONE_DIST) continue;
            bool marked = false;
            for (const auto& opp : state.players) {
                if (opp.team == me.team || opp.isDemoed) continue;
                if ((opp.pos - tm.pos).Length() < MARKING_DIST) { marked = true; break; }
            }
            if (!marked) return true;
        }
        return false;
    }

public:
    struct Metrics { int dualFrames = 0; int eligFrames = 0; };

    static Metrics GetAndResetMetrics() {
        return { s_dualFrames.exchange(0), s_eligFrames.exchange(0) };
    }

    virtual void Reset(const GameState& initialState) override {
        m_timeSinceTouch.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        // 2v2/3v3 only
        int teamCount = 0;
        for (const auto& p : state.players)
            if (p.team == player.team) teamCount++;
        if (teamCount < 2) return 0.0f;

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        float& tst = m_timeSinceTouch[player.carId];
        if (player.ballTouchedStep) tst = 0.0f;
        else                        tst += dt;

        if (tst > RECENT_TOUCH_WINDOW) return 0.0f;

        // Ball must be in opponent's half
        bool ballInOppHalf = (player.team == Team::BLUE)
            ? (state.ball.pos.y > 0.0f)
            : (state.ball.pos.y < 0.0f);
        if (!ballInOppHalf) return 0.0f;

        // Ball must be moving toward goal with sufficient speed
        Vec goalDir = (OppGoalCenter(player) - state.ball.pos).Normalized();
        float goalVelComponent = state.ball.vel.Dot(goalDir);

        s_eligFrames.fetch_add(1);

        if (goalVelComponent < MIN_GOAL_VEL) return 0.0f;
        if (!HasUnmarkedTeammateInScoringZone(player, state)) return 0.0f;

        s_dualFrames.fetch_add(1);
        return PER_STEP_REWARD;
    }
};

} // namespace RLGC
