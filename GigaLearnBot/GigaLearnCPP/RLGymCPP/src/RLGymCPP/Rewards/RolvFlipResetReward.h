// RolvFlipResetReward.h
// C++ port of the FlipResetReward function by Rolv-Arild.
// Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/flip_reset_reward.py
// Port: NJH (2026-08-01)
//
// Two-stage reward:
//   1. obtainFlipWeight — fires once when the player touches the ball this step
//      AND newly gains the flip-reset state (player.HasFlipReset() transitions
//      false -> true via player.prev), gated by facing the ball within 45 deg
//      (wheels-toward-ball, matching FlipResetApproachReward's alignment check).
//   2. hitBallWeight — fires once the player actually uses the reset (detected
//      via isFlipping while the reset was held) and then touches the ball again,
//      validating the reset was put to productive use rather than just held.
//
// OVERLAPS: FlipResetReward.h (acquisition+approach shaping, Phase 6),
// FlipResetApproachReward.h (approach shaping, Phase 5+6), SimpleFlipResetReward
// (flat touch-triggered reward, Phase 6 -- commented out in favor of this class
// as of 2026-08-01, see RewardConfig.cpp PHASE_6_CHAMPION). Do not casually
// re-enable SimpleFlipResetReward alongside this without reviewing gradient
// dilution in the flip-reset category.

#pragma once

#include "Reward.h"
#include "RewardMath.h"
#include "../Math.h"
#include <unordered_set>
#include <cstdint>

namespace RLGC {

    class RolvFlipResetReward : public Reward {
    private:
        static constexpr float COS_45_DEG = 0.70710678f; // sqrt(0.5)

        float obtainFlipWeight;
        float hitBallWeight;

        // Per-episode, per-player state mirroring the Python source's
        // self.has_reset / self.has_flipped sets, keyed by carId.
        std::unordered_set<uint32_t> _hasReset;
        std::unordered_set<uint32_t> _hasFlipped;

    public:
        explicit RolvFlipResetReward(float obtainFlipWeight_ = 1.0f, float hitBallWeight_ = 1.0f)
            : obtainFlipWeight(obtainFlipWeight_), hitBallWeight(hitBallWeight_) {}

        virtual void Reset(const GameState& initialState) override {
            _hasReset.clear();
            _hasFlipped.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;
            float reward = 0.0f;

            bool touchedThisStep = player.ballTouchedStep;
            bool nowHasFlip = player.HasFlipReset();
            // No prev on the first frame after Reset() -> treat as no transition.
            bool prevHadFlip = player.prev ? player.prev->HasFlipReset() : nowHasFlip;

            if (touchedThisStep && nowHasFlip && !prevHadFlip) {
                // Facing check: wheels (down = -up) pointing toward the ball within 45 deg.
                Vec down = Vec(-player.rotMat.up.x, -player.rotMat.up.y, -player.rotMat.up.z);
                Vec toBall = state.ball.pos - player.pos;
                float cossim = SafeNorm(down).Dot(SafeNorm(toBall));
                if (cossim > COS_45_DEG) {
                    _hasReset.insert(id);
                    reward = obtainFlipWeight;
                }
            } else if (player.isOnGround) {
                _hasReset.erase(id);
                _hasFlipped.erase(id);
            } else if (player.isFlipping && _hasReset.count(id)) {
                _hasReset.erase(id);
                _hasFlipped.insert(id);
            }

            // Both branches require touchedThisStep and are mutually exclusive in
            // practice: HasFlipReset() is false immediately after flipping (hasJumped
            // becomes true), so a same-frame re-acquisition can't also fire this.
            if (touchedThisStep && _hasFlipped.count(id)) {
                _hasFlipped.erase(id);
                reward = hitBallWeight;
            }

            return reward;
        }
    };

} // namespace RLGC
