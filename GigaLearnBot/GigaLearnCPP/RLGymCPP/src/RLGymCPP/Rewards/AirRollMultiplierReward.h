#pragma once
#include "Reward.h"
#include <cmath>

namespace RLGC {

    // ============================================================================
    // AIR ROLL MULTIPLIER GATE — Rev 1.0
    //
    // Gate reward that returns a multiplier value based on air rolling state.
    // Designed to be used with MathRewardWrapper(MathOp::MULTIPLY) to boost
    // other rewards when the player is air rolling.
    //
    // This encourages air rolling as a *supplement* to other skills.
    // Useful for:
    //   - Rewarding flip resets with bonus when air rolling
    //   - Rewarding aerial touches with higher multiplier when rolling
    //   - Encouraging rotation-based skill execution
    //
    // Detection:
    //   - Player must be airborne (!player.isOnGround)
    //   - Player must be above MIN_HEIGHT (300 UU)
    //   - Player must have active roll input (|roll| > 0.5)
    //
    // Return value:
    //   - When air rolling: 1.5f (default, configurable)
    //   - When NOT air rolling: 1.0f (neutral, no penalty)
    //
    // Example usage with MathRewardWrapper:
    //   WeightedReward(
    //       new ZeroSumReward(
    //           new MathRewardWrapper(
    //               {
    //                   new SimpleFlipResetReward(),
    //                   new AirRollMultiplierGate(1.5f)
    //               },
    //               MathOp::MULTIPLY
    //           ),
    //           0.3f
    //       ),
    //       20.0f
    //   )
    //
    // ============================================================================
    class AirRollMultiplierGate : public Reward {
    private:
        static constexpr float ROLL_THRESHOLD = 0.5f;  // Min |roll| for active input
        static constexpr float MIN_HEIGHT = 300.0f;    // Minimum Z to apply bonus
        float multiplier;                                // Multiplier when rolling (e.g., 1.5f)

    public:
        /**
         * Construct an air roll multiplier gate.
         *
         * @param rollMultiplier Multiplier to return when air rolling (default 1.5f)
         *                       E.g., 1.5f for 50% bonus, 2.0f for 100% bonus
         */
        explicit AirRollMultiplierGate(float rollMultiplier = 1.5f)
            : multiplier(rollMultiplier) {}

        /**
         * Get multiplier value based on air rolling state.
         *
         * Returns:
         *   - multiplier when player is air rolling (default 1.5f)
         *   - 1.0f when player is NOT air rolling (neutral)
         *
         * Use with MathRewardWrapper(MathOp::MULTIPLY) to scale other rewards.
         */
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Check if player is air rolling
            if (IsAirRolling(player)) {
                return multiplier;  // Return configured multiplier when rolling
            }

            // Neutral: return 1.0f when not rolling (no change to multiplied reward)
            return 1.0f;
        }

    private:
        /**
         * Check if player is currently air rolling in the positive direction (right roll).
         *
         * Requirements:
         *   1. Not on ground (airborne)
         *   2. Above MIN_HEIGHT (300 UU)
         *   3. Active POSITIVE roll input (roll > ROLL_THRESHOLD)
         *
         * Note: Only positive roll (right) triggers the bonus. Negative roll (left) returns neutral.
         */
        bool IsAirRolling(const Player& player) const {
            // Must be airborne
            if (player.isOnGround) return false;

            // Must be above minimum height (300 UU)
            if (player.pos.z < MIN_HEIGHT) return false;

            // Must have active POSITIVE roll input (roll > 0.5)
            // Right roll only - negative roll values don't trigger bonus
            // Roll range is [-1, 1]; positive threshold filters for right rolls only
            if (player.prevAction.roll <= ROLL_THRESHOLD) return false;

            return true;
        }
    };

} // namespace RLGC
