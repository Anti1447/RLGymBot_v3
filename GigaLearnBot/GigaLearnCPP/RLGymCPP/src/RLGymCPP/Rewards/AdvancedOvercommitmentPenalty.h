#pragma once
#include "Reward.h"
#include "PositionAnalysis.h"
#include "../Gamestates/GameState.h"
#include "../Math.h"
#include "RewardMath.h"
#include <unordered_map>
#include <cmath>

namespace RLGC {

// ============================================================================
// ADVANCED OVERCOMMITMENT PENALTY - Phase 2 (2026-02-14 | v1.0)
// ============================================================================
// Teaches motion-aware resource management through trajectory prediction
//
// Core principle: Penalize actions that will lock you into bad states by
// projecting outcomes rather than just checking current positions.
//
// Improvements over Phase 1:
// - Flip lock window: Detect if flip will meaningfully miss ball
// - Aerial commitment: Project if aerial will reach ball height
// - Boost depletion: Predict if boost runs out in bad position
// - Efficiency: Sample every 0.25 seconds instead of per-frame (4x faster)
// - Exception handling: Reuse Phase 1's comprehensive exception logic
//
// Sampling: Every 0.25 seconds (~30 frames @ 120Hz)
// Computational cost: 2-4 trajectory projections per check
// Reward: Negative (penalties) to discourage predictably bad outcomes
//
// ============================================================================

class AdvancedOvercommitmentPenalty : public Reward {
private:
    // Sampling state per player
    struct SamplingMemory {
        float lastSampleTime = -999.0f;         // Per-player sampling timestamp
        Vec lastProjectedPos = Vec(0, 0, 0);    // Last projected position
        float lastFlipDistance = 0.0f;          // Distance flip would miss by
    };

    mutable std::unordered_map<uint32_t, SamplingMemory> memory;
    mutable float currentTime = 0.0f;

    // ===== SAMPLING CONFIGURATION =====
    static constexpr float SAMPLE_INTERVAL = 0.25f;            // Check every 0.25 seconds

    // ===== GATE 1: FLIP LOCK WINDOW =====
    static constexpr float MEANINGFUL_INTERACTION = 500.0f;    // Flip must get within this
    static constexpr float FLIP_BOOST_THRESHOLD = 20.0f;       // Risky flip threshold
    static constexpr float FLIP_LOCK_PENALTY = 0.4f;           // Higher specificity = lower penalty

    // ===== GATE 2: AERIAL COMMITMENT =====
    static constexpr float AERIAL_TOUCH_MARGIN = 200.0f;       // Height to touch ball
    static constexpr float AERIAL_BOOST_THRESHOLD = 30.0f;     // Risky aerial threshold
    static constexpr float AERIAL_LOCK_PENALTY = 0.35f;        // Misses are costly

    // ===== GATE 3: BOOST DEPLETION ENDPOINT =====
    static constexpr float STRANDED_DISTANCE = 3000.0f;        // Far from goal = stranded
    static constexpr float OPPONENT_DEMO_RANGE = 1500.0f;      // Demo threat range
    static constexpr float STRANDED_PENALTY = 0.3f;            // Stranded scenario penalty

    // ===== EXCEPTION THRESHOLDS (Reused from Phase 1) =====
    static constexpr float BALL_CONTEST_MARGIN = 300.0f;       // More generous contest window
    static constexpr float TEAMMATE_DEFER_MARGIN = 500.0f;     // Stricter defer
    static constexpr float PRESSURE_DISTANCE = 700.0f;         // Opponent threat range
    static constexpr float SHOT_RECOVERY_WINDOW = 0.5f;        // Shot recovery window
    static constexpr float BOOST_DEPLETION_RATE = 33.3f;       // Units per second

public:
    AdvancedOvercommitmentPenalty() = default;

    virtual void Reset(const GameState& initialState) override {
        memory.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // ===================================================================
        // SAMPLING GATE: Only check every 0.25 seconds (4x more efficient)
        // ===================================================================
        currentTime = state.deltaTime;
        auto& playerMemory = memory[player.carId];

        if (currentTime - playerMemory.lastSampleTime < SAMPLE_INTERVAL) {
            return 0.0f;  // Too soon to sample, return neutral
        }
        playerMemory.lastSampleTime = currentTime;

        // ===================================================================
        // PRE-CHECK EXCEPTIONS (Situations where overcommitment is justified)
        // ===================================================================
        float ourDistance = (player.pos - state.ball.pos).Length();
        float closestOpponentDistance = PositionAnalysis::ClosestOpponentDistance(
            player, state.ball.pos, state.players);
        float closestTeammateDistance = PositionAnalysis::ClosestTeammateDistance(
            player, state.ball.pos, state.players);

        // Exception: Kickoff formation (all players equidistant by design)
        if (PositionAnalysis::IsKickoffFormation(player, state)) {
            return 0.0f;
        }

        // Exception: Teammate clearly handling it (much closer, defer)
        if (closestTeammateDistance < ourDistance - TEAMMATE_DEFER_MARGIN) {
            return 0.0f;
        }

        float penalty = 0.0f;

        // ===================================================================
        // GATE 1: FLIP LOCK WINDOW ANALYSIS
        // ===================================================================
        // Penalty: Don't flip if projected completion position is far from ball with low boost
        if (player.isFlipping && player.boost < FLIP_BOOST_THRESHOLD) {
            Vec projectedFlipPos = PositionAnalysis::predictFlipCompletionPosition(player);
            float projectedDistToBall = (projectedFlipPos - state.ball.pos).Length();

            if (projectedDistToBall > MEANINGFUL_INTERACTION) {
                // Flip will complete far from ball - wasted effort

                // Exception 1a: Dodging imminent demo (survival overrides resource management)
                if (PositionAnalysis::IsDodgingImminentDemo(player, state.players)) {
                    // Survival justified
                } else {
                    // Predictably bad flip outcome - penalize
                    penalty += FLIP_LOCK_PENALTY;
                }
            }
        }

        // ===================================================================
        // GATE 2: AERIAL COMMITMENT ANALYSIS
        // ===================================================================
        // Penalty: Don't attempt aerial if projected height misses ball significantly
        if (!player.isOnGround && player.vel.z < 0 && player.boost < AERIAL_BOOST_THRESHOLD) {
            // Player is airborne and falling with low boost

            float timeToGround = PositionAnalysis::calculateTimeToGround(player);
            float projectedZ = PositionAnalysis::projectZPosition(player, timeToGround);
            float ballZ = state.ball.pos.z;
            float heightDifference = std::abs(projectedZ - ballZ);

            if (heightDifference > AERIAL_TOUCH_MARGIN) {
                // Projected height will miss ball significantly

                // Exception 2a: Teammate already committed to aerial (let them handle it)
                float closestTeammateAerialDistance = FLT_MAX;
                for (const auto& teammate : state.players) {
                    if (teammate.team != player.team || teammate.carId == player.carId) continue;
                    if (!teammate.isOnGround) {
                        float dist = (teammate.pos - state.ball.pos).Length();
                        closestTeammateAerialDistance = RS_MIN(closestTeammateAerialDistance, dist);
                    }
                }

                if (closestTeammateAerialDistance < ourDistance) {
                    // Teammate closer and already aerial - defer to them
                } else if (PositionAnalysis::IsDodgingImminentDemo(player, state.players)) {
                    // Dodging is justified even if aerial misses
                } else {
                    // Predictably bad aerial outcome - penalize
                    penalty += AERIAL_LOCK_PENALTY;
                }
            }
        }

        // ===================================================================
        // GATE 3: BOOST DEPLETION ENDPOINT ANALYSIS
        // ===================================================================
        // Penalty: Don't aggressively push into opponent half if boost will deplete far from goal
        bool inOppHalf = (player.team == Team::BLUE && player.pos.y > 0) ||
                        (player.team == Team::ORANGE && player.pos.y < 0);

        if (inOppHalf && player.boost > 0.0f && player.boost < 50.0f) {
            // Low boost in opponent half - check if stranded at depletion
            if (PositionAnalysis::isProjectedToStrandInOpponentHalf(player, BOOST_DEPLETION_RATE)) {
                // Will be stranded when boost runs out

                // Exception 3a: Opponent within demo range (threat forces urgency)
                float distToClosestOpponent = PositionAnalysis::ClosestOpponentDistance(
                    player, player.pos, state.players);

                if (distToClosestOpponent < OPPONENT_DEMO_RANGE) {
                    // Opponent threatening - urgency justified
                } else {
                    // No imminent threat, penalize stranding scenario
                    penalty += STRANDED_PENALTY;
                }
            }
        }

        // Return penalty (negative = punishment)
        return -penalty;
    }

private:
    // Helper to check if player is in dangerous state
    bool isInDangerousState(const Player& player, const GameState& state) {
        float boostLevel = player.boost;
        bool lowBoost = boostLevel < 25.0f;
        bool noBounces = player.hasDoubleJumped;
        bool airborne = !player.isOnGround;

        return lowBoost && noBounces && airborne;
    }
};

}  // namespace RLGC
