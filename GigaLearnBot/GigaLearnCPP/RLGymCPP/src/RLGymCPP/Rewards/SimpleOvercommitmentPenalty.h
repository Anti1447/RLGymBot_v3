#pragma once
#include "Reward.h"
#include "PositionAnalysis.h"
#include "../Gamestates/GameState.h"
#include "../Math.h"
#include "RewardMath.h"
#include <map>

namespace RLGC {

// ============================================================================
// SIMPLE OVERCOMMITMENT PENALTY - Phase 1 (2025-02-04 | Rev 1.0 | NJH)
// ============================================================================
// Teaches resource management and positioning discipline
//
// Core principle: Penalize actions that lock you into bad states without:
// - Sufficient resources to recover (boost, jumps)
// - Adequate positioning (close to ball, defensive coverage)
// - Tactical justification (contest, demo avoidance)
//
// Sampling: Every 0.25 seconds (~30 frames @ 120Hz)
// Computational cost: 5-7 distance calculations per check
// Reward: Negative (penalties) to discourage overcommitment
//
// ============================================================================

class SimpleOvercommitmentPenalty : public Reward {
private:
    // Per-player sampling state
    mutable std::map<uint32_t, float> lastCheckTime;

    // Configuration constants (can be tuned after initial training)
    static constexpr float CHECK_INTERVAL = 0.0f;          // Check every frame (more reactive)
    static constexpr float BALL_CONTEST_MARGIN = 300.0f;   // More generous contest window
    static constexpr float TEAMMATE_DEFER_MARGIN = 500.0f; // Stricter defer - only if clearly handled

    // Gate 1: Flip in Danger
    static constexpr float FLIP_MIN_BOOST_THRESHOLD = 15.0f;
    static constexpr float SAFE_FLIP_DISTANCE = 1500.0f;
    static constexpr float FLIP_CONTEST_THRESHOLD = 600.0f;
    static constexpr float FLIP_PENALTY = 0.5f;            // Reduced from 10.0f
    static constexpr float OPPONENT_CONTACT_TIME_THRESHOLD = 0.5f;  // Opponent about to hit ball (seconds)

    // Gate 2: Double Jump Depletion
    static constexpr float DOUBLE_JUMP_RECOVERY_BOOST = 25.0f;
    static constexpr float DOUBLE_JUMP_PENALTY = 0.3f;     // Reduced from 8.0f

    // Gate 3: Stranded in Opponent Half
    static constexpr float OPP_HALF_BOOST_MIN = 20.0f;
    static constexpr float PRESSURE_DISTANCE = 700.0f;
    static constexpr float STRANDED_PENALTY = 0.2f;        // Reduced from 5.0f

    // Gate 4: Corner Challenge Risk
    static constexpr float CORNER_PENALTY = 0.6f;          // Reduced from 12.0f
    static constexpr float OPPONENT_CORNER_ENGAGEMENT = 1200.0f;

    // Gate 5: Goal Net Landing Trap
    static constexpr float RECOVERY_BOOST_THRESHOLD = 40.0f;
    static constexpr float SHOT_RECOVERY_WINDOW = 0.5f;     // 0.5 seconds
    static constexpr float SAFE_RECOVERY_TIME = 3.0f;       // 3 seconds
    static constexpr float BOOST_DEPLETION_RATE = 33.3f;    // Units per second
    static constexpr float GOAL_TRAP_PENALTY = 0.5f;        // Reduced from 14.0f
    static constexpr float GOAL_TRAP_SLOW_PENALTY = 0.6f;   // Reduced from 15.0f

public:
    SimpleOvercommitmentPenalty() = default;

    virtual void Reset(const GameState& initialState) override {
        lastCheckTime.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // ===================================================================
        // SAMPLING GATE: Only check every 0.25 seconds
        // ===================================================================
        float currentTime = state.deltaTime;  // Accumulated time
        float& lastTime = lastCheckTime[player.carId];

        if (currentTime - lastTime < CHECK_INTERVAL) {
            return 0.0f;  // Too soon to check, return neutral
        }
        lastTime = currentTime;

        // ===================================================================
        // STEP 1: PRE-CHECK EXCEPTIONS (Situations where overcommitment is justified)
        // ===================================================================
        float ourDistance = (player.pos - state.ball.pos).Length();
        float closestOpponentDistance = PositionAnalysis::ClosestOpponentDistance(
            player, state.ball.pos, state.players);
        float closestTeammateDistance = PositionAnalysis::ClosestTeammateDistance(
            player, state.ball.pos, state.players);

        // Exception 1: Kickoff formation
        // All players are equidistant by design, don't penalize
        if (PositionAnalysis::IsKickoffFormation(player, state)) {
            return 0.0f;
        }

        // Exception 2: Teammate is VERY clearly handling it (much closer, clear defer)
        // Only skip penalties if teammate is significantly closer
        if (closestTeammateDistance < ourDistance - TEAMMATE_DEFER_MARGIN) {
            return 0.0f;
        }

        float penalty = 0.0f;

        // ===================================================================
        // GATE 1: FLIP IN DANGER
        // ===================================================================
        // Penalty: Don't flip far from ball with low boost (can't recover)
        // Also: Don't flip when an opponent is about to hit the ball
        if (player.isFlipping && player.boost < FLIP_MIN_BOOST_THRESHOLD) {
            float distToBall = (player.pos - state.ball.pos).Length();

            // Check if an opponent is about to hit the ball
            bool opponentAboutToHitBall = false;
            for (const auto& opponent : state.players) {
                // Skip teammates
                if (opponent.team == player.team) continue;

                auto ballToOpponent = opponent.pos - state.ball.pos;
                float distToOpponentBall = ballToOpponent.Length();

                // Check if opponent is moving toward ball
                auto toBallDir = (state.ball.pos - opponent.pos).Normalized();
                float dotProduct = opponent.vel.Dot(toBallDir);

                if (dotProduct > 0.0f) {
                    // Opponent is moving toward ball
                    float opponentSpeed = opponent.vel.Length();
                    if (opponentSpeed > 0.0f) {
                        float timeToContact = distToOpponentBall / opponentSpeed;
                        if (timeToContact < OPPONENT_CONTACT_TIME_THRESHOLD) {
                            opponentAboutToHitBall = true;
                            break;
                        }
                    }
                }
            }

            if (distToBall > SAFE_FLIP_DISTANCE || opponentAboutToHitBall) {
                // Flipped far from ball with low boost, or opponent is about to hit ball

                // Exception 1a: Dodging imminent demo/bump (survival exception only)
                if (PositionAnalysis::IsDodgingImminentDemo(player, state.players)) {
                    // Survival > resource management, flip is justified
                } else {
                    // Don't allow opponent proximity as exception - you should have been positioned better
                    penalty += FLIP_PENALTY;
                }
            }
        }

        // ===================================================================
        // GATE 2: DOUBLE JUMP DEPLETION
        // ===================================================================
        // Penalty: Don't use both jumps with no boost to recover (outside defense)
        if (player.hasDoubleJumped && player.boost < DOUBLE_JUMP_RECOVERY_BOOST) {
            bool inDefense = PositionAnalysis::InDefensivePosition(player);

            if (!inDefense) {
                // In offensive position with both jumps burned and low boost

                // Exception 2a: Dodging imminent demo/bump (survival exception only)
                if (PositionAnalysis::IsDodgingImminentDemo(player, state.players)) {
                    // Survival justified
                } else {
                    // Aggressive play without resources is overcommitment
                    penalty += DOUBLE_JUMP_PENALTY;
                }
            }
        }

        // ===================================================================
        // GATE 3: STRANDED IN OPPONENT HALF
        // ===================================================================
        // Penalty: Don't get stuck deep in opponent half with no boost (no rotation path)
        bool inOppHalf = (player.team == Team::BLUE && player.pos.y > 0) ||
                        (player.team == Team::ORANGE && player.pos.y < 0);

        if (inOppHalf && player.boost < OPP_HALF_BOOST_MIN) {
            // Exception 3a: Pressuring opponent closely
            float distToClosestOpponent = PositionAnalysis::ClosestOpponentDistance(
                player, player.pos, state.players);

            if (distToClosestOpponent < PRESSURE_DISTANCE) {
                // Actively pressuring opponent
            } else {
                // Stranded in opp half with no boost - no good exceptions
                penalty += STRANDED_PENALTY;
            }
        }

        // ===================================================================
        // GATE 4: CORNER CHALLENGE RISK
        // ===================================================================
        // Penalty: Don't challenge in opponent corner unless you're closest to ball
        // Corners are sticky, high-risk zones with low escape routes
        if (PositionAnalysis::InOpponentCorner(player)) {
            // In opponent corner zone

            if (ourDistance > closestOpponentDistance + BALL_CONTEST_MARGIN) {
                // We're NOT closest to ball in this corner
                // Simple: if you're not closest, you shouldn't be in the corner
                penalty += CORNER_PENALTY;
            }
        }

        // ===================================================================
        // GATE 5: GOAL NET LANDING TRAP
        // ===================================================================
        // Penalty: Don't land in opponent goal with low boost after shot
        // This leaves your team vulnerable to counter-attacks
        if (PositionAnalysis::InOpponentGoalArea(player) &&
            player.boost < RECOVERY_BOOST_THRESHOLD) {
            // In goal area with low boost

            // Exception 5: Recently attempted shot/flick (being in goal is expected)
            if (PositionAnalysis::RecentlyAttemptedShot(player, SHOT_RECOVERY_WINDOW)) {
                // Just attempted a shot, analyze recovery time
                float timeToRecover = (RECOVERY_BOOST_THRESHOLD - player.boost) / BOOST_DEPLETION_RATE;

                if (timeToRecover >= SAFE_RECOVERY_TIME) {
                    // Takes too long to recover, teammates vulnerable
                    penalty += GOAL_TRAP_SLOW_PENALTY;
                }
                // Otherwise allow it (quick recovery expected)
            } else {
                // Stuck in goal without good reason
                penalty += GOAL_TRAP_PENALTY;
            }
        }

        // Return penalty (negative = punishment)
        return -penalty;
    }
};

}  // namespace RLGC
