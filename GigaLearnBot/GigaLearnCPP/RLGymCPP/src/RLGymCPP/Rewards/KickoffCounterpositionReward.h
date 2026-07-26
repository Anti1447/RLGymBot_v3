#pragma once
// KickoffCounterpositionReward.h
// Rev 1.0 (2026-06-28, NJH + Claude)
//
// Category: PREDICTION / POSITIONING
// Target: PHASE_5_DIAMOND+
//
// When an opponent executes a speedflip kickoff and hits the ball diagonally
// off-center, teaches the bot to pre-shade laterally toward the predicted
// ball-deflection side BEFORE the opponent makes contact.
//
// Two reward signals:
//
//   1. Pre-shade shaping (continuous, tiny): while speedflip is detected and
//      before opponent contacts ball, reward counter-team player for:
//        - X position on the predicted deflection side
//        - X velocity toward that side
//      Together these teach "drift toward where the ball is going to land."
//
//   2. Touch bonus (event, one-shot): after the speedflipping player contacts
//      the ball, if the counter player touches it back toward opponent's goal
//      within COUNTER_WINDOW_SECS, give a bonus reward.
//
// Speedflip detection: airborne + speed > 1600 UU/s + |velN.x| > 0.25.
// Predicted deflection side = sign of speedflipper's X velocity.
//
// Timing follows CLAUDE.md 7C: koTime accumulated in PreStep only.
// touchBonus_paid map cleared in PreStep on kickoff rising edge.
//
// W&B: Kickoff/CounterPos/SpeedflipDetected
//      Kickoff/CounterPos/PreshadeCorrect
//      Kickoff/CounterPos/TouchBonus

#include "Reward.h"
#include "AdvancedKickoffRewards.h"   // KickoffUtil
#include <atomic>
#include <unordered_map>

namespace RLGC {

class KickoffCounterpositionReward : public Reward {
private:
    static constexpr float SPEEDFLIP_DETECT_SPEED   = 1600.0f;
    static constexpr float SPEEDFLIP_DETECT_LATERAL = 0.25f;   // min |velN.x| for diagonal detection
    // Real speedflips execute in the first ~1.2s of a kickoff. Without this window,
    // any later incidental jump/dodge meeting the kinematic thresholds (airborne, fast,
    // diagonal) could spuriously trigger the whole pre-shade + touch-bonus subsystem.
    static constexpr float SPEEDFLIP_DETECT_WINDOW_SECS = 1.3f;
    static constexpr float SHADE_REWARD_SCALE       = 0.05f;   // per-step continuous shaping magnitude
    static constexpr float TOUCH_BONUS              = 2.0f;    // event reward for counter touch
    static constexpr float COUNTER_WINDOW_SECS      = 1.5f;    // window after opp contact for touch bonus
    static constexpr float MIN_WIN_ALIGNMENT        = 0.3f;    // min ball-toward-oppgoal to count as win
    static constexpr float MAX_EPOCH                = 5.0f;    // hard cap on kickoff epoch length (s)
    static constexpr float SHADE_X_RANGE            = 1500.0f; // X distance that maps to full pre-shade reward
    static constexpr float SHADE_VX_RANGE           = 1000.0f; // X velocity that maps to full velocity reward

    // --- global PreStep state (managed once per step, NOT per-player) ---
    bool    koEpochOpen          = false;
    float   koTime               = 0.0f;
    bool    speedflipDetected    = false;
    int     speedflipTeam        = -1;        // which team is executing the speedflip
    float   predictedDeflectionX = 0.0f;     // +1.0 = ball goes right, -1.0 = ball goes left
    bool    oppContactedBall     = false;     // speedflipping team has touched the ball
    float   oppContactTime       = 0.0f;     // koTime when they did

    // --- per-player one-shot guards (cleared per kickoff in PreStep) ---
    mutable std::unordered_map<uint32_t, bool> touchBonus_paid;

    // --- metrics (thread-safe across parallel environments) ---
    inline static std::atomic<int> s_speedflipDetected { 0 };
    inline static std::atomic<int> s_preshadeCorrect   { 0 };
    inline static std::atomic<int> s_touchBonus        { 0 };

public:
    struct Metrics {
        int speedflipDetected = 0;
        int preshadeCorrect   = 0;
        int touchBonus        = 0;
    };

    static Metrics GetAndResetMetrics() {
        return {
            s_speedflipDetected.exchange(0),
            s_preshadeCorrect.exchange(0),
            s_touchBonus.exchange(0)
        };
    }

    virtual void Reset(const GameState& initialState) override {
        koEpochOpen          = false;
        koTime               = 0.0f;
        speedflipDetected    = false;
        speedflipTeam        = -1;
        predictedDeflectionX = 0.0f;
        oppContactedBall     = false;
        oppContactTime       = 0.0f;
        touchBonus_paid.clear();
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        bool isKickoffNow = KickoffUtil::IsKickoff(state);

        // Rising edge: new kickoff epoch
        if (isKickoffNow && !koEpochOpen) {
            koEpochOpen          = true;
            koTime               = 0.0f;
            speedflipDetected    = false;
            speedflipTeam        = -1;
            predictedDeflectionX = 0.0f;
            oppContactedBall     = false;
            oppContactTime       = 0.0f;
            touchBonus_paid.clear();
        }

        if (!koEpochOpen) return;

        // Epoch close conditions
        if (!isKickoffNow || koTime > MAX_EPOCH ||
            (oppContactedBall && koTime > oppContactTime + COUNTER_WINDOW_SECS)) {
            koEpochOpen = false;
            return;
        }

        koTime += dt;

        // Speedflip detection: scan for any airborne player above speed threshold
        // with significant lateral (X) velocity component. Restricted to the first
        // SPEEDFLIP_DETECT_WINDOW_SECS of the kickoff epoch to avoid false positives
        // from later, unrelated jumps/dodges that happen to clear the same thresholds.
        if (!speedflipDetected && koTime <= SPEEDFLIP_DETECT_WINDOW_SECS) {
            for (const auto& p : state.players) {
                if (p.isDemoed || p.isOnGround) continue;
                float speed = p.vel.Length();
                if (speed < SPEEDFLIP_DETECT_SPEED) continue;
                float latComp = fabsf(p.vel.x) / (speed + 1e-6f);
                if (latComp < SPEEDFLIP_DETECT_LATERAL) continue;

                speedflipDetected    = true;
                speedflipTeam        = (int)p.team;
                predictedDeflectionX = (p.vel.x > 0.0f) ? 1.0f : -1.0f;
                s_speedflipDetected.fetch_add(1);
                break;
            }
        }

        // Track first contact by the speedflipping team
        if (speedflipDetected && !oppContactedBall) {
            for (const auto& p : state.players) {
                if ((int)p.team != speedflipTeam) continue;
                if (p.ballTouchedStep) {
                    oppContactedBall = true;
                    oppContactTime   = koTime;
                    break;
                }
            }
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed)    return 0.0f;
        if (!koEpochOpen)       return 0.0f;
        if (!speedflipDetected) return 0.0f;
        // Only reward the counter team, not the speedflipper
        if ((int)player.team == speedflipTeam) return 0.0f;

        uint32_t id    = player.carId;
        float    reward = 0.0f;

        // --- Pre-shade shaping (continuous, before opponent contacts ball) ---
        if (!oppContactedBall) {
            // Reward for being on the predicted deflection X side
            float xAlignment  = player.pos.x * predictedDeflectionX;
            float normalizedX = RS_CLAMP(xAlignment / SHADE_X_RANGE, 0.0f, 1.0f);

            // Reward for moving toward the deflection side
            float xVelAlign    = player.vel.x * predictedDeflectionX;
            float normalizedXVel = RS_CLAMP(xVelAlign / SHADE_VX_RANGE, 0.0f, 1.0f);

            reward = SHADE_REWARD_SCALE * (0.5f * normalizedX + 0.5f * normalizedXVel);

            if (normalizedX > 0.3f) {
                s_preshadeCorrect.fetch_add(1);
            }
        }

        // --- Touch bonus (event, after opponent contacts ball, one-shot per player) ---
        if (oppContactedBall && !touchBonus_paid[id] && player.ballTouchedStep &&
            (koTime - oppContactTime) <= COUNTER_WINDOW_SECS) {

            Vec dir   = (KickoffUtil::OppGoal(player) - state.ball.pos).Normalized();
            float align = state.ball.vel.Normalized().Dot(dir);

            if (align > MIN_WIN_ALIGNMENT) {
                touchBonus_paid[id] = true;
                s_touchBonus.fetch_add(1);
                reward += TOUCH_BONUS;
            }
        }

        return reward;
    }
};

} // namespace RLGC
