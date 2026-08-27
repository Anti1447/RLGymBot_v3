#pragma once
#include "Reward.h"
#include "RewardMath.h"
#include "../CommonValues.h"
#include <atomic>

namespace RLGC {

// ============================================================================
// FreeShotConversionReward
// 2026-08-02 | Rev 1.0 | NJH + Claude
//
// Rewards striking the ball toward goal, quickly, at the moment no living
// opponent can realistically reach the shot lane in time to block it — e.g.
// every opponent is overcommitted/out of position after a beaten challenge,
// a whiffed clear, or a give-and-go pass. Shooter-only: whoever ends up
// striking the ball while the window is open gets credit, regardless of how
// the ball got there (dribble, loose ball, or a teammate's pass) — this
// naturally covers give-and-go setups without a separate passer-side class.
//
// GEOMETRY:
//   For an attacking team, a "shot window" is open when every living,
//   non-demoed opponent's estimated time to reach the straight-line path
//   from the ball to the opponent's goal center exceeds the ball's estimated
//   time to travel that same path, plus a safety margin. Opponent arrival
//   time uses perpendicular distance to the ball->goal segment (clamped to
//   the segment), not raw distance to the ball — a fast defender closing on
//   an intercept angle counts even if they're not nearest to the ball itself.
//
//   Two different "ball time to goal" estimates are used for two different
//   purposes:
//     - PreStep (window tracking, continuous shaping): uses an assumed
//       clean-shot speed, since the ball may not yet be moving toward goal
//       at all (e.g. a loose ball sitting still after a whiffed clear). This
//       answers "if a shot were taken right now, would it be safe?"
//     - GetReward on touch (event bonus): uses the ball's actual post-touch
//       velocity component toward goal, matching RewardMath::
//       BallIntersectsOpponentGoalMouth's own trajectory model, so the event
//       bonus only pays for a real, on-target shot — not just standing in a
//       favorable spot.
//
// ANTI-FARMING:
//   - MAX_OPPONENT_SPEED assumes boosting speed (CAR_MAX_SPEED), not walking
//     speed, so a boosted defender with a recovery angle correctly blocks
//     the "free" classification even when spatially "behind" the ball.
//   - Event bonus requires real shot speed (MIN_SHOT_SPEED_ALONG_GOAL) and
//     an on-target trajectory (BallIntersectsOpponentGoalMouth) — a weak tap
//     from a free position doesn't qualify.
//   - Event bonus fires once per continuous window: a per-team "paid" flag
//     is set on conversion and only cleared when the window closes (goes
//     non-free), so re-touching the ball while the window stays open can't
//     re-trigger it.
//   - Continuous shaping only pays the single player closest to the ball on
//     the whole field (RewardMath::IsClosestPlayerToBall), so teammates
//     can't all farm the open window simultaneously.
//
// WEIGHTING: GetReward returns a single normalized value combining both
// components under one WeightedReward entry (matches this codebase's
// one-class-one-weight convention). The event bonus is scaled in
// [MIN_TIME_SCALE, 1.0] by how quickly the window was converted; continuous
// shaping is pre-scaled down by CONTINUOUS_INTERNAL_SCALE so that a single
// external weight in the ~40-70 range (matching AssistReward/ShotReward
// scale) produces event payouts in that range and continuous payouts in the
// ~1-3 range the CLAUDE.md positioning-reward guidance calls for.
//
// W&B: FreeShot/WindowsOpened, FreeShot/WindowsConverted,
//      FreeShot/AvgTimeToConvert, FreeShot/RewardSum
// ============================================================================
class FreeShotConversionReward : public Reward {
public:
    struct Config {
        float maxOpponentSpeed      = CommonValues::CAR_MAX_SPEED; // 2300 UU/s, boosting speed
        float reactionLatency       = 0.15f;   // seconds, defender reaction time
        float safetyMargin          = 0.2f;    // seconds, buffer added to ball time-to-goal
        float assumedShotSpeed      = 2200.0f; // UU/s, hypothetical clean-shot speed for pre-touch window tracking
        float minShotSpeedAlongGoal = 1200.0f; // UU/s, real strike must be at least this fast toward goal
        float maxWindowSec          = 1.5f;    // productive-use window: quickness bonus decays to 0 by this point
        float minTimeScale          = 0.3f;    // floor multiplier for a late (but still qualifying) conversion
    };

    struct Metrics {
        int    windowsOpened    = 0;
        int    windowsConverted = 0;
        double avgTimeToConvert = 0.0;
        double rewardSum        = 0.0;
    };

private:
    static constexpr float CONTINUOUS_INTERNAL_SCALE = 0.04f;

    Config config;

    struct TeamWindow {
        float elapsed = 0.0f;
        bool  paid    = false;
        bool  wasFree = false;
    };
    mutable TeamWindow windows[2]; // 0 = BLUE, 1 = ORANGE

    inline static std::atomic<int>       s_windowsOpened    { 0 };
    inline static std::atomic<int>       s_windowsConverted { 0 };
    inline static std::atomic<long long> s_timeToConvertMilliSum { 0 };
    inline static std::atomic<long long> s_rewardMilliSum   { 0 };

    static int TeamIdx(Team t) { return (t == Team::BLUE) ? 0 : 1; }

    // Perpendicular distance from a point to the segment [ballPos, ballPos + goalDir*segLen].
    static float PerpDistToShotSegment(const Vec& ballPos, const Vec& goalDir, float segLen, const Vec& pos) {
        Vec toPos = pos - ballPos;
        float along = RS_CLAMP(toPos.Dot(goalDir), 0.0f, segLen);
        Vec closest = ballPos + goalDir * along;
        return (pos - closest).Length();
    }

    // True if no living, non-demoed opponent of `attackingTeam` can reach the
    // ball->goal shot lane before the ball would, given `ballTimeToGoal`.
    bool IsShotWindowFree(Team attackingTeam, const GameState& state, float ballTimeToGoal) const {
        Vec goalCenter = (attackingTeam == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_CENTER
            : CommonValues::BLUE_GOAL_CENTER;

        Vec toGoal = goalCenter - state.ball.pos;
        float segLen = toGoal.Length();
        if (segLen < 1.0f) return true; // ball is essentially in the goal already

        Vec goalDir = toGoal * (1.0f / segLen);

        for (const Player& opp : state.players) {
            if (opp.team == attackingTeam) continue;
            if (opp.isDemoed) continue;

            float perpDist = PerpDistToShotSegment(state.ball.pos, goalDir, segLen, opp.pos);
            float oppTimeToBlock = perpDist / config.maxOpponentSpeed + config.reactionLatency;
            if (oppTimeToBlock <= ballTimeToGoal + config.safetyMargin)
                return false;
        }
        return true;
    }

public:
    FreeShotConversionReward() : config() {}
    explicit FreeShotConversionReward(const Config& cfg) : config(cfg) {}

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.windowsOpened    = s_windowsOpened.exchange(0);
        m.windowsConverted = s_windowsConverted.exchange(0);
        long long ttcRaw = s_timeToConvertMilliSum.exchange(0);
        long long rewRaw = s_rewardMilliSum.exchange(0);
        m.rewardSum = double(rewRaw) / 1000.0;
        if (m.windowsConverted > 0)
            m.avgTimeToConvert = double(ttcRaw) / (1000.0 * m.windowsConverted);
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        windows[0] = TeamWindow{};
        windows[1] = TeamWindow{};
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        for (int i = 0; i < 2; i++) {
            Team team = (i == 0) ? Team::BLUE : Team::ORANGE;
            Vec goalCenter = (team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;
            float distToGoal = (goalCenter - state.ball.pos).Length();
            float hypotheticalTimeToGoal = distToGoal / config.assumedShotSpeed;
            bool free = IsShotWindowFree(team, state, hypotheticalTimeToGoal);

            TeamWindow& w = windows[i];
            if (free) {
                if (!w.wasFree) s_windowsOpened.fetch_add(1);
                w.elapsed += dt;
            } else {
                w.elapsed = 0.0f;
                w.paid = false;
            }
            w.wasFree = free;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        int t = TeamIdx(player.team);
        TeamWindow& w = windows[t];

        // --- Event bonus: this player just struck the ball, and the window hasn't been cashed in yet ---
        if (player.ballTouchedStep && !w.paid) {
            Vec goalCenter = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;
            Vec toGoal = goalCenter - state.ball.pos;
            float distToGoal = toGoal.Length();

            if (distToGoal > 1.0f) {
                Vec goalDir = toGoal * (1.0f / distToGoal);
                float ballSpeedTowardGoal = state.ball.vel.Dot(goalDir);

                if (ballSpeedTowardGoal > config.minShotSpeedAlongGoal) {
                    float ballTimeToGoal = distToGoal / ballSpeedTowardGoal;
                    bool free = IsShotWindowFree(player.team, state, ballTimeToGoal);
                    bool onTarget = RewardMath::BallIntersectsOpponentGoalMouth(
                        player, state, ballTimeToGoal + 0.5f, /*requireBackNet*/ false, /*useGravity*/ true);

                    if (free && onTarget) {
                        w.paid = true;
                        float scale = RS_CLAMP(1.0f - w.elapsed / config.maxWindowSec, config.minTimeScale, 1.0f);

                        s_windowsConverted.fetch_add(1);
                        s_timeToConvertMilliSum.fetch_add(static_cast<long long>(w.elapsed * 1000.0f));
                        s_rewardMilliSum.fetch_add(static_cast<long long>(scale * 1000.0f));

                        return scale;
                    }
                }
            }
        }

        // --- Continuous urgency shaping: window open, not yet cashed in, this is the presumptive shooter ---
        if (w.wasFree && !w.paid && RewardMath::IsClosestPlayerToBall(player, state)) {
            Vec toBall = SafeNorm(state.ball.pos - player.pos);
            float closingSpeed = player.vel.Dot(toBall);
            float normalized = RS_CLAMP(closingSpeed / CommonValues::CAR_MAX_SPEED, 0.0f, 1.0f);
            float reward = CONTINUOUS_INTERNAL_SCALE * normalized;

            s_rewardMilliSum.fetch_add(static_cast<long long>(reward * 1000.0f));
            return reward;
        }

        return 0.0f;
    }
};

} // namespace RLGC
