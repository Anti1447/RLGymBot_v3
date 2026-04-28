#pragma once
#include "Reward.h"
#include "../Math.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>

// AirDribbleExplorationReward — Phase 6+
// Rewards airborne ball contact in patterns OUTSIDE normal air dribble scope.
// Three additive channels: orientation diversity, touch rhythm variety, power air touch.
// Designed to complement AirDribbleAccuracyReward, not compete with it.
// Rev 1.1 2026-04-22 NJH

namespace RLGC {

class AirDribbleExplorationReward : public Reward {
public:
    // Channel A thresholds
    static constexpr float MIN_BALL_Z            = 200.f;
    static constexpr float SIDEWAYS_UP_Z_MAX     = 0.35f;   // |up.z| < this = sideways
    static constexpr float INVERTED_UP_Z_MAX     = -0.5f;   // up.z < this = inverted
    static constexpr float AIR_ROLL_INPUT_MIN    = 0.5f;
    static constexpr float ORIENTATION_COOLDOWN  = 2.0f;    // seconds
    static constexpr int   MAX_DIVERSITY_PER_SEQ = 3;

    // Channel B thresholds
    static constexpr float MULTITAP_WINDOW       = 0.25f;
    static constexpr float DELAYED_TOUCH_MIN     = 2.0f;
    static constexpr int   MAX_RHYTHM_TOUCHES    = 6;

    // Channel C thresholds
    static constexpr float POWER_MIN_HEIGHT      = 300.f;
    static constexpr float POWER_VEL_FLOOR       = 1200.f;
    static constexpr float POWER_VEL_SCALE       = 1800.f;
    static constexpr float POWER_COOLDOWN        = 1.5f;
    static constexpr float POWER_REWARD_MAX      = 0.4f;

private:
    struct PlayerState {
        // Channel A
        float sidewaysCooldown   = 0.f;
        float invertedCooldown   = 0.f;
        int   diversityCount     = 0;
        bool  wasAirborne        = false;

        // Channel B
        float lastAirTouchTime   = -99.f;
        int   rhythmTouchCount   = 0;
        bool  stayedAirborne     = true;

        // Channel C
        float powerCooldown      = 0.f;
        Vec   prevBallVel        = {};
    };

    std::unordered_map<uint32_t, PlayerState> _states;
    float _time = 0.f;

public:
    void Reset(const GameState& state) override {
        _states.clear();
        _time = 0.f;
        for (const auto& p : state.players)
            _states[p.carId] = PlayerState{};
    }

    void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.f) ? state.deltaTime : (1.f / 120.f);
        _time += dt;

        for (const auto& p : state.players) {
            auto& s = _states[p.carId];

            s.sidewaysCooldown = std::max(0.f, s.sidewaysCooldown - dt);
            s.invertedCooldown = std::max(0.f, s.invertedCooldown - dt);
            s.powerCooldown    = std::max(0.f, s.powerCooldown    - dt);

            if (p.isOnGround) {
                s.wasAirborne     = false;
                s.stayedAirborne  = true;
                s.rhythmTouchCount = 0;
            } else {
                s.wasAirborne = true;
            }

            s.prevBallVel = state.ball.vel;
        }
    }

    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.f;
        if (player.isOnGround)       return 0.f;
        if (state.ball.pos.z < MIN_BALL_Z) return 0.f;

        auto& s = _states[player.carId];
        float reward = 0.f;

        // ── Channel A: Orientation Diversity ──────────────────────────────────
        if (s.diversityCount < MAX_DIVERSITY_PER_SEQ) {
            float upZ      = player.rotMat.up.z;
            bool  airRolling = std::abs(player.prevAction.roll) > AIR_ROLL_INPUT_MIN;
            float bonus    = 0.f;

            if (upZ < INVERTED_UP_Z_MAX && s.invertedCooldown <= 0.f) {
                bonus = 0.4f;
                s.invertedCooldown = ORIENTATION_COOLDOWN;
            } else if (std::abs(upZ) < SIDEWAYS_UP_Z_MAX && s.sidewaysCooldown <= 0.f) {
                bonus = 0.25f;
                s.sidewaysCooldown = ORIENTATION_COOLDOWN;
            }

            if (bonus > 0.f) {
                if (airRolling) bonus *= 1.3f;
                reward += bonus;
                s.diversityCount++;
            }
        }

        // ── Channel B: Touch Rhythm Variety ──────────────────────────────────
        if (s.rhythmTouchCount < MAX_RHYTHM_TOUCHES) {
            float timeSinceLast = _time - s.lastAirTouchTime;
            float rhythmBonus = 0.f;

            if (s.lastAirTouchTime < 0.f) {
                rhythmBonus = 0.05f;
            } else if (timeSinceLast < MULTITAP_WINDOW) {
                rhythmBonus = 0.15f;
            } else if (timeSinceLast > DELAYED_TOUCH_MIN && s.stayedAirborne) {
                rhythmBonus = 0.2f;
            } else {
                rhythmBonus = 0.05f;
            }

            reward += rhythmBonus;
            s.rhythmTouchCount++;
        }
        s.lastAirTouchTime = _time;

        // ── Channel C: Power Air Touch ────────────────────────────────────────
        if (player.pos.z > POWER_MIN_HEIGHT && s.powerCooldown <= 0.f) {
            float velDelta = (state.ball.vel - s.prevBallVel).Length();
            if (velDelta > POWER_VEL_FLOOR) {
                float t = std::min((velDelta - POWER_VEL_FLOOR) / POWER_VEL_SCALE, 1.f);
                reward += t * POWER_REWARD_MAX;
                s.powerCooldown = POWER_COOLDOWN;
            }
        }

        return reward;
    }
};

} // namespace RLGC
