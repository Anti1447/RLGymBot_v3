#pragma once
#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"
#include <unordered_map>

namespace RLGC {

// ============================================================================
// WALL DASH INPUT REWARD — Rev 1.0 | 2026-06-28 | NJH
// ============================================================================
// Rewards the correct INPUT CONFIGURATION for wall dashing:
//   on wall (rotMat.up horizontal) + throttle + pitch-down + handbrake
//   + correct air roll direction for traversal path + forward angle check.
// Jump event bonus fires on each jump press while in setup (0.3s cooldown).
//
// TRAVERSAL DIRECTION (determines correct air roll):
//   crossZ = pos.x * vel.y - pos.y * vel.x
//   crossZ > 0 → CCW (viewed from +Z above) → air roll RIGHT (roll > 0.7)
//   crossZ ≤ 0 → CW                          → air roll LEFT  (roll < -0.7)
//
// ON-WALL DETECTION (replaces proximity check from WallDashReward.h):
//   rotMat.up.z ≈ 0  → car up vector is horizontal (not floor/ceiling)
//   isOnGround       → wheels have surface contact
//   pos.z > 150      → above the curved floor-wall transition zone
//
// REWARD STRUCTURE:
//   SETUP_REWARD (0.3f/step): all 9 conditions met
//   JUMP_REWARD  (3.0f):      setup + jump pressed, 0.3s float-seconds cooldown
//
// TUNING FALLBACKS if signal is dead at 200M steps:
//   Relax UP_Z_THRESHOLD to 0.3f, MIN_H_SPEED_SQ to 60000.0f (245 UU/s)
// ============================================================================

class WallDashInputReward : public Reward {
private:
    mutable std::unordered_map<uint32_t, float> _jumpCooldown;

    static constexpr float JUMP_COOLDOWN_S  = 0.3f;
    static constexpr float SETUP_REWARD     = 0.3f;
    static constexpr float JUMP_REWARD      = 3.0f;
    static constexpr float UP_Z_THRESHOLD   = 0.2f;    // |up.z| < this → on wall
    static constexpr float FWD_Z_THRESHOLD  = 0.5f;    // |fwd.z| < this → ≤30° from horizontal
    static constexpr float MIN_Z            = 150.0f;   // above floor-wall curve
    static constexpr float MIN_H_SPEED_SQ   = 90000.0f; // 300 UU/s minimum horizontal speed

public:
    WallDashInputReward() = default;

    void Reset(const GameState& initialState) override {
        _jumpCooldown.clear();
    }

    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        // Tick down jump cooldown every step (per CLAUDE.md: always decrement)
        float& cd = _jumpCooldown[player.carId];
        if (cd > 0.0f) cd = RS_MAX(0.0f, cd - dt);

        // 1. On wall: surface contact + horizontal up vector + above floor curve
        if (!player.isOnGround) return 0.0f;
        if (fabsf(player.rotMat.up.z) >= UP_Z_THRESHOLD) return 0.0f;
        if (player.pos.z < MIN_Z) return 0.0f;

        // 2. Required inputs: throttle on, pitch down, handbrake on
        if (player.prevAction.throttle < 0.5f) return 0.0f;
        if (player.prevAction.pitch > -0.7f) return 0.0f;
        if (player.prevAction.handbrake < 0.5f) return 0.0f;

        // 3. Forward vector within 30° of horizontal (not nose-diving or nose-up on wall)
        if (fabsf(player.rotMat.forward.z) >= FWD_Z_THRESHOLD) return 0.0f;

        // 4. Minimum horizontal traversal speed (makes traversal direction meaningful)
        float hSpeedSq = player.vel.x * player.vel.x + player.vel.y * player.vel.y;
        if (hSpeedSq < MIN_H_SPEED_SQ) return 0.0f;

        // 5. Correct air roll for traversal direction
        //    crossZ > 0 → CCW around field center → roll right
        //    crossZ ≤ 0 → CW  around field center → roll left
        float crossZ = player.pos.x * player.vel.y - player.pos.y * player.vel.x;
        bool isCCW = crossZ > 0.0f;
        bool correctRoll = isCCW ? (player.prevAction.roll > 0.7f)
                                 : (player.prevAction.roll < -0.7f);
        if (!correctRoll) return 0.0f;

        // All setup conditions met — continuous shaping reward
        float reward = SETUP_REWARD;

        // Jump event bonus (0.3s cooldown prevents button-mash farming)
        if (player.prevAction.jump > 0.5f && cd <= 0.0f) {
            reward += JUMP_REWARD;
            cd = JUMP_COOLDOWN_S;
        }

        return reward;
    }
};

} // namespace RLGC
