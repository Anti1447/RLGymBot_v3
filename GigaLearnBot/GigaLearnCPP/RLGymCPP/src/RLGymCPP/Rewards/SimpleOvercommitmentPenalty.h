#pragma once
#include "Reward.h"
#include "PositionAnalysis.h"
#include "../Gamestates/GameState.h"
#include "../Math.h"
#include "RewardMath.h"
#include <unordered_map>

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
    // Per-player sampling / cooldown state
    mutable std::unordered_map<uint32_t, float> lastCheckTime;
    mutable std::unordered_map<uint32_t, float> gate2Cooldown;
    mutable std::unordered_map<uint32_t, float> gate3Cooldown;
    float _currentTime = 0.0f;

    // Configuration constants (can be tuned after initial training)
    static constexpr float CHECK_INTERVAL = 0.0f;          // Check every frame (more reactive)
    static constexpr float BALL_CONTEST_MARGIN = 300.0f;
    static constexpr float TEAMMATE_DEFER_MARGIN = 500.0f;

    // Gate 1: Flip in Danger
    static constexpr float FLIP_MIN_BOOST_THRESHOLD = 15.0f;
    static constexpr float SAFE_FLIP_DISTANCE = 1500.0f;
    static constexpr float FLIP_PENALTY = 0.5f;
    static constexpr float OPPONENT_CONTACT_TIME_THRESHOLD = 0.5f;

    // Gate 2: Double Jump Depletion
    static constexpr float DOUBLE_JUMP_RECOVERY_BOOST = 25.0f;
    static constexpr float DOUBLE_JUMP_PENALTY = 0.3f;
    static constexpr float GATE2_COOLDOWN_SECONDS = 1.0f;  // one penalty per committed aerial

    // Gate 3: Stranded in Opponent Half
    static constexpr float OPP_HALF_BOOST_MIN = 20.0f;
    static constexpr float PRESSURE_DISTANCE = 700.0f;
    static constexpr float STRANDED_PENALTY = 0.2f;
    static constexpr float GATE3_COOLDOWN_SECONDS = 1.0f;  // one penalty per entry

    // Gate 4: Corner Challenge Risk
    static constexpr float CORNER_PENALTY = 0.6f;

    // Gate 5: Goal Net Landing Trap
    static constexpr float RECOVERY_BOOST_THRESHOLD = 40.0f;
    static constexpr float SHOT_RECOVERY_WINDOW = 0.5f;
    static constexpr float SAFE_RECOVERY_TIME = 3.0f;
    static constexpr float BOOST_DEPLETION_RATE = 33.3f;
    static constexpr float GOAL_TRAP_PENALTY = 0.5f;
    static constexpr float GOAL_TRAP_SLOW_PENALTY = 0.6f;

public:
    SimpleOvercommitmentPenalty() = default;

    virtual void Reset(const GameState& initialState) override {
        lastCheckTime.clear();
        gate2Cooldown.clear();
        gate3Cooldown.clear();
        _currentTime = 0.0f;
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        _currentTime += dt;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // ===================================================================
        // SAMPLING GATE
        // ===================================================================
        float& lastTime = lastCheckTime[player.carId];
        if (_currentTime - lastTime < CHECK_INTERVAL) {
            return 0.0f;
        }
        lastTime = _currentTime;

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        // Tick down per-gate cooldowns every frame regardless of trigger
        float& cd2 = gate2Cooldown[player.carId];
        float& cd3 = gate3Cooldown[player.carId];
        if (cd2 > 0.0f) cd2 = RS_MAX(0.0f, cd2 - dt);
        if (cd3 > 0.0f) cd3 = RS_MAX(0.0f, cd3 - dt);

        // ===================================================================
        // PRE-CHECK: Skip kickoff
        // ===================================================================
        if (PositionAnalysis::IsKickoffFormation(player, state)) {
            return 0.0f;
        }

        float ourDistance = (player.pos - state.ball.pos).Length();
        float closestOpponentDistance = PositionAnalysis::ClosestOpponentDistance(
            player, state.ball.pos, state.players);
        float closestTeammateDistance = PositionAnalysis::ClosestTeammateDistance(
            player, state.ball.pos, state.players);

        float penalty = 0.0f;

        // ===================================================================
        // GATE 1: FLIP IN DANGER
        // ===================================================================
        if (player.isFlipping && player.boost < FLIP_MIN_BOOST_THRESHOLD) {
            float distToBall = (player.pos - state.ball.pos).Length();

            bool opponentAboutToHitBall = false;
            for (const auto& opponent : state.players) {
                if (opponent.team == player.team) continue;
                auto ballToOpponent = opponent.pos - state.ball.pos;
                float distToOpponentBall = ballToOpponent.Length();
                auto toBallDir = (state.ball.pos - opponent.pos).Normalized();
                float dotProduct = opponent.vel.Dot(toBallDir);
                if (dotProduct > 0.0f) {
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
                if (!PositionAnalysis::IsDodgingImminentDemo(player, state.players)) {
                    penalty += FLIP_PENALTY;
                }
            }
        }

        // ===================================================================
        // GATE 2: DOUBLE JUMP DEPLETION  (cooldown: one penalty per committed aerial)
        // ===================================================================
        if (player.hasDoubleJumped && player.boost < DOUBLE_JUMP_RECOVERY_BOOST
            && !PositionAnalysis::InDefensivePosition(player)
            && !PositionAnalysis::IsDodgingImminentDemo(player, state.players)
            && cd2 <= 0.0f)
        {
            penalty += DOUBLE_JUMP_PENALTY;
            cd2 = GATE2_COOLDOWN_SECONDS;
        }

        // ===================================================================
        // GATE 3: STRANDED IN OPPONENT HALF  (cooldown: one penalty per entry)
        // ===================================================================
        bool inOppHalf = (player.team == Team::BLUE && player.pos.y > 0) ||
                         (player.team == Team::ORANGE && player.pos.y < 0);

        if (inOppHalf && player.boost < OPP_HALF_BOOST_MIN && cd3 <= 0.0f
            && PositionAnalysis::AnyLivingOpponent(player, state.players)) {
            float distToClosestOpponent = PositionAnalysis::ClosestOpponentDistance(
                player, player.pos, state.players);
            if (distToClosestOpponent >= PRESSURE_DISTANCE) {
                penalty += STRANDED_PENALTY;
                cd3 = GATE3_COOLDOWN_SECONDS;
            }
        }

        // ===================================================================
        // GATE 4: CORNER CHALLENGE RISK
        // Teammate-defer exception applies here only (not globally)
        // ===================================================================
        if (PositionAnalysis::InOpponentCorner(player)) {
            bool teammateCloser = closestTeammateDistance < ourDistance - TEAMMATE_DEFER_MARGIN;
            if (!teammateCloser && ourDistance > closestOpponentDistance + BALL_CONTEST_MARGIN) {
                penalty += CORNER_PENALTY;
            }
        }

        // ===================================================================
        // GATE 5: GOAL NET LANDING TRAP
        // ===================================================================
        if (PositionAnalysis::InOpponentGoalArea(player) &&
            player.boost < RECOVERY_BOOST_THRESHOLD &&
            PositionAnalysis::AnyLivingOpponent(player, state.players)) {
            if (PositionAnalysis::RecentlyAttemptedShot(player, SHOT_RECOVERY_WINDOW)) {
                float timeToRecover = (RECOVERY_BOOST_THRESHOLD - player.boost) / BOOST_DEPLETION_RATE;
                if (timeToRecover >= SAFE_RECOVERY_TIME) {
                    penalty += GOAL_TRAP_SLOW_PENALTY;
                }
            } else {
                penalty += GOAL_TRAP_PENALTY;
            }
        }

        return -penalty;
    }
};

}  // namespace RLGC
