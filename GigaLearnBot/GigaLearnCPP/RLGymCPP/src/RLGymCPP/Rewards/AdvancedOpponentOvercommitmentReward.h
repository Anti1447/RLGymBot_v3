#pragma once
#include "Reward.h"
#include "PositionAnalysis.h"
#include "../Gamestates/GameState.h"
#include "../Math.h"
#include <unordered_map>
#include <cmath>

namespace RLGC {

// ============================================================================
// ADVANCED OPPONENT OVERCOMMITMENT REWARD (2026-02-14 | v1.0)
// ============================================================================
// Rewards players for exploiting opponent lock-outs created by overcommitment
//
// Core principle: When opponent flips/aerials lock them into bad positions,
// reward players who capitalize on the opportunity by beating them to the ball
// or gaining positional advantage.
//
// Behavior: Detects when opponent is locked out by:
// - Active flip/aerial in progress
// - Projected position will miss target
// - Our distance to ball < their distance at completion
//
// Sampling: Every 0.5 seconds (low frequency, high impact)
// Computational cost: Minimal - only tracking opponent state
// Reward: Positive (shaping + events) to encourage exploitation
//
// ============================================================================

class AdvancedOpponentOvercommitmentReward : public Reward {
private:
    // Per-player memory for tracking lock-outs
    struct OpponentMemory {
        bool wasFlippingLocked = false;
        bool wasAerialLocked = false;
        uint32_t lockedOpponentId = UINT32_MAX;
        int framesUntilOpponentRecovery = 0;
        float lastAdvantageRewardTime = -999.0f;
    };

    mutable std::unordered_map<uint32_t, OpponentMemory> memory;
    mutable float currentTime = 0.0f;

    // ===== SAMPLING CONFIGURATION =====
    static constexpr float SAMPLE_INTERVAL = 0.5f;             // Check every 0.5 seconds

    // ===== LOCK-OUT DETECTION =====
    static constexpr float FLIP_LOCK_OUT_WINDOW = 0.3f;        // Flip lock lasts ~0.3 seconds
    static constexpr float MEANINGFUL_INTERACTION = 500.0f;    // How far is "locked out"

    // ===== REWARD MAGNITUDES =====
    static constexpr float POSITIONING_ADVANTAGE_SHAPING = 0.05f;  // Per-frame for advantage
    static constexpr float BALL_WIN_EVENT_REWARD = 0.4f;           // Win contested ball during lock
    static constexpr float LOCK_OUT_CONTEXT_BONUS = 0.1f;          // Bonus if creates 2v1 advantage

    // ===== THRESHOLDS =====
    static constexpr float SAFE_ENGAGEMENT_BOOST = 50.0f;      // Min boost to engage safely
    static constexpr float COMPETITIVE_DISTANCE = 2000.0f;     // Within this = competitive

public:
    AdvancedOpponentOvercommitmentReward() = default;

    virtual void Reset(const GameState& initialState) override {
        memory.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // ===================================================================
        // SAMPLING GATE: Only check every 0.5 seconds
        // ===================================================================
        currentTime = state.deltaTime;
        auto& playerMemory = memory[player.carId];

        if (currentTime - playerMemory.lastAdvantageRewardTime < SAMPLE_INTERVAL) {
            return 0.0f;  // Too soon to sample
        }
        playerMemory.lastAdvantageRewardTime = currentTime;

        // ===================================================================
        // PRE-CHECK EXCEPTIONS
        // ===================================================================
        // Only activate in 2v2+ (need opponents to lock out)
        int opponentCount = 0;
        for (const auto& other : state.players) {
            if (other.team != player.team) {
                opponentCount++;
            }
        }
        if (opponentCount < 1) return 0.0f;

        // Don't reward exploitation in own goal area
        if (PositionAnalysis::InOpponentGoalArea(player)) {
            return 0.0f;
        }

        float reward = 0.0f;
        float ourDistToBall = (player.pos - state.ball.pos).Length();

        // ===================================================================
        // DETECT LOCKED-OUT OPPONENTS AND GRANT REWARDS
        // ===================================================================

        for (const auto& opponent : state.players) {
            // Only check opponents
            if (opponent.team == player.team) continue;

            // ===== Check Flip Lock-Out =====
            if (opponent.isFlipping) {
                // Opponent is currently flipping - check if we can exploit it
                Vec projectedFlipPos = PositionAnalysis::predictFlipCompletionPosition(opponent);
                float projectedOppDistToBall = (projectedFlipPos - state.ball.pos).Length();

                // Is flip locking them out significantly?
                if (projectedOppDistToBall > MEANINGFUL_INTERACTION) {
                    // Flip will leave them far from ball

                    if (ourDistToBall < projectedOppDistToBall) {
                        // We're closer - we have advantage!

                        // Shaping reward for positional advantage
                        reward += POSITIONING_ADVANTAGE_SHAPING;

                        // Event reward if we have safe boost to engage
                        if (player.boost >= SAFE_ENGAGEMENT_BOOST) {
                            reward += BALL_WIN_EVENT_REWARD * 0.5f;  // Potential reward (not guaranteed)
                        }

                        // Bonus if this creates numerical advantage
                        int ourPlayersNearBall = 0;
                        int theirPlayersNearBall = 0;

                        for (const auto& other : state.players) {
                            float distToBall = (other.pos - state.ball.pos).Length();
                            if (distToBall < COMPETITIVE_DISTANCE) {
                                if (other.team == player.team) {
                                    ourPlayersNearBall++;
                                } else {
                                    theirPlayersNearBall++;
                                }
                            }
                        }

                        if (ourPlayersNearBall > theirPlayersNearBall) {
                            reward += LOCK_OUT_CONTEXT_BONUS;
                        }
                    }
                }
            }

            // ===== Check Aerial Lock-Out =====
            if (!opponent.isOnGround && opponent.vel.z < 0) {
                // Opponent is airborne and falling - check if aerial locks them out

                float timeToGround = PositionAnalysis::calculateTimeToGround(opponent);
                float projectedOppZ = PositionAnalysis::projectZPosition(opponent, timeToGround);
                float ballZ = state.ball.pos.z;
                float heightDifference = std::abs(projectedOppZ - ballZ);

                if (heightDifference > 200.0f) {
                    // Aerial will miss ball significantly

                    // Check if we can exploit this lock-out
                    if (ourDistToBall < 2000.0f && player.boost >= SAFE_ENGAGEMENT_BOOST) {
                        // We're close enough and safe to engage

                        reward += POSITIONING_ADVANTAGE_SHAPING * 1.5f;  // Increased shaping

                        // If opponent was primary aerial threat, bonus for taking initiative
                        if (!player.isOnGround) {
                            reward += LOCK_OUT_CONTEXT_BONUS;  // Aerial duel reward
                        }
                    }
                }
            }
        }

        return reward;
    }

private:
    // Helper to check if player can safely engage
    bool canSafelyEngage(const Player& player, float minBoost = SAFE_ENGAGEMENT_BOOST) {
        return player.boost >= minBoost && !player.hasDoubleJumped;
    }

    // Helper to count players near ball within distance
    int countPlayersNearBall(const Player& reference,
                            const Vec& ballPos,
                            float distance,
                            const std::vector<Player>& allPlayers,
                            bool sameTeamOnly = true) {
        int count = 0;

        for (const auto& other : allPlayers) {
            if (sameTeamOnly && other.team != reference.team) continue;
            if (!sameTeamOnly && other.team == reference.team) continue;
            if (other.carId == reference.carId) continue;

            float dist = (other.pos - ballPos).Length();
            if (dist < distance) {
                count++;
            }
        }

        return count;
    }
};

}  // namespace RLGC
