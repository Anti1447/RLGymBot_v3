#pragma once
#include "Reward.h"
#include "../Math.h"

// ============================================================================
// PASSING REWARDS - Progressive system from Bronze to SSL
// ============================================================================
// Key concepts:
// 1. Bronze-Diamond: Reward back passes (safe, simple)
// 2. Gold-Plat: Reward forward passes with positioning checks
// 3. Diamond+: Reward team positioning and pass reception
//
// References: Passing Guide by Rank (Bronze-SSL)
// ============================================================================

// ============================================================================
// BackPassReward - Diamond/SSL safe passing
// Rewards passes backward to teammates in recovery or rotation
// Key: Safe, maintains possession, minimal risk
// ============================================================================
class BackPassReward : public Reward {
private:
    mutable std::map<uint32_t, Vec> lastTouchPos;

    // Helper: Check if player is in opponent's half (attacking)
    bool IsInOpponentHalf(const Player& player) const {
        if (player.team == Team::BLUE) {
            return player.pos.y > 0;  // BLUE attacks +Y (toward ORANGE goal)
        } else {
            return player.pos.y < 0;  // ORANGE attacks -Y (toward BLUE goal)
        }
    }

    // Helper: Check if player is in own half (defending)
    bool IsInOwnHalf(const Player& player) const {
        if (player.team == Team::BLUE) {
            return player.pos.y < 0;  // BLUE defends -Y
        } else {
            return player.pos.y > 0;  // ORANGE defends +Y
        }
    }

public:
    virtual void Reset(const GameState& initialState) override {
        lastTouchPos.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        // Store touch position for this player
        lastTouchPos[player.carId] = player.pos;

        if (!state.prev) return 0.0f;

        // ====================================================================
        // NEW: Only reward if passer is in OPPONENT'S HALF
        // This ensures we're rewarding tactical retreat from attacking positions
        // ====================================================================
        if (!IsInOpponentHalf(player)) {
            return 0.0f;  // Passer is not in opponent's half - no reward
        }

        // Check if ball moved backward (toward own goal)
        Vec ballMovement = state.ball.pos - state.prev->ball.pos;
        Vec ownGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

        Vec toOwnGoal = (ownGoal - player.pos).Normalized();

        // Back pass = moving toward own goal after touch
        float backPassAlignment = ballMovement.Normalized().Dot(toOwnGoal);

        if (backPassAlignment > 0.5f) {
            // Check if there's a teammate behind to receive
            float minTeammateDistance = 999999.0f;
            bool receiverInOwnHalf = false;

            for (const auto& other : state.players) {
                if (other.carId == player.carId || other.team != player.team) continue;

                float dist = (other.pos - player.pos).Length();
                // Teammate should be behind player
                Vec toTeammate = (other.pos - player.pos).Normalized();
                float alignment = toTeammate.Dot(toOwnGoal);

                if (alignment > 0.3f) {  // Behind enough
                    minTeammateDistance = RS_MIN(minTeammateDistance, dist);

                    // ================================================================
                    // NEW: Check if closest receiver is in OWN HALF
                    // Only consider teammates who are in defensive position
                    // ================================================================
                    if (IsInOwnHalf(other)) {
                        receiverInOwnHalf = true;
                    }
                }
            }

            // ====================================================================
            // NEW: Only reward if receiver is in own half AND within reasonable distance
            // This ensures back passes are to players ready to defend
            // ====================================================================
            if (minTeammateDistance < 2000.0f && receiverInOwnHalf) {  // Reasonable back pass distance
                float distance_factor = 1.0f - RS_CLAMP(minTeammateDistance / 2000.0f, 0.0f, 1.0f);
                return 5.0f * distance_factor;  // 0-5 reward for strategic back pass
            }
        }

        return 0.0f;
    }
};

// ============================================================================
// ForwardPassReward - Gold+ forward/setup passes
// Rewards passes that move ball toward opponent goal with teammate positioning
// Key: Requires teammate awareness, direction control
// ============================================================================
class ForwardPassReward : public Reward {
private:
    static constexpr float MIN_PASS_DISTANCE = 300.0f;  // Lowered from 500
    static constexpr float TEAMMATE_POSITIONING_RANGE = 3000.0f;  // Increased from 2500

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Check forward direction (toward opponent goal)
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();  // From ball, not player

        // Ball movement analysis
        Vec ballMovement = state.ball.pos - state.prev->ball.pos;
        float ballDistance = ballMovement.Length();

        // Must move with meaningful distance
        if (ballDistance < MIN_PASS_DISTANCE) return 0.0f;

        // Relaxed forward alignment check (0.1 instead of 0.3)
        float forwardAlignment = ballMovement.Normalized().Dot(toGoal);
        if (forwardAlignment < 0.1f) return 0.0f;

        // Find best teammate ahead of ball
        float bestTeammateDistance = FLT_MAX;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;

            // Vector from ball to teammate
            Vec toTeammate = (other.pos - state.ball.pos);
            float distanceToTeammate = toTeammate.Length();

            // Teammate must be "ahead" (toward goal direction): 0.2+ dot product (relaxed from 0.4)
            float aheadAlignment = toTeammate.Normalized().Dot(toGoal);
            if (aheadAlignment > 0.2f && distanceToTeammate < TEAMMATE_POSITIONING_RANGE) {
                bestTeammateDistance = RS_MIN(bestTeammateDistance, distanceToTeammate);
            }
        }

        // Simple distance-based reward (no scoring threshold)
        if (bestTeammateDistance < FLT_MAX) {
            float distance_factor = 1.0f - RS_CLAMP(bestTeammateDistance / TEAMMATE_POSITIONING_RANGE, 0.0f, 1.0f);
            return 6.0f * distance_factor;  // Max: 6.0 (lowered from 8.0)
        }

        return 0.0f;
    }
};

// ============================================================================
// HighPassReward - Plat+ advanced passes (lobs, aerials)
// Rewards high-ball passes that create scoring opportunities
// Key: Ball height + teammate positioning + angle toward goal
// ============================================================================
class HighPassReward : public Reward {
private:
    static constexpr float HIGH_BALL_THRESHOLD = 100.0f;  // Lowered from 200

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Ball movement analysis
        Vec ballMovement = state.ball.pos - state.prev->ball.pos;
        float ballSpeed = ballMovement.Length();

        // Require minimum ball speed for realistic passes
        if (ballSpeed < 200.0f) return 0.0f;

        // Ball must be elevated after touch
        float heightGain = state.ball.pos.z - state.prev->ball.pos.z;
        if (heightGain < 50.0f) return 0.0f;

        // Lowered ball height threshold (100 instead of 200)
        if (state.ball.pos.z < HIGH_BALL_THRESHOLD) return 0.0f;

        // Check passing direction toward goal (relaxed from 0.5 to 0.3)
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballMovement.Normalized().Dot(toGoal);

        if (goalAlignment < 0.3f) return 0.0f;  // Relaxed from 0.5f

        // Find best teammate in aerial reception position
        float bestAerialDistance = FLT_MAX;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;

            // Distance from ball to teammate
            Vec toTeammate = (other.pos - state.ball.pos);
            float distance = toTeammate.Length();

            // Teammate must be within aerial range (200-1500 units)
            if (distance < 1500.0f && distance > 200.0f) {
                // FIXED: Correct height calculation - teammate below ball is good
                // Calculate how far below the ball the teammate is
                float heightBelowBall = RS_MAX(0.0f, state.ball.pos.z - other.pos.z);

                // Reward teammate within reasonable catch height (0-400 units below)
                if (heightBelowBall < 400.0f) {
                    bestAerialDistance = RS_MIN(bestAerialDistance, distance);
                }
            }
        }

        if (bestAerialDistance < FLT_MAX) {
            // Simple distance-based calculation
            float distance_factor = 1.0f - RS_CLAMP(bestAerialDistance / 1500.0f, 0.0f, 1.0f);
            return 8.0f * distance_factor;  // Max: 8.0 (lowered from 12.0)
        }

        return 0.0f;
    }
};

// ============================================================================
// OffensivePlayReward - SSL setup passes leading to direct scoring chances
// Rewards creating immediate scoring opportunities through passes
// Key: Teammate in striking position + undefended + high ball speed
// ============================================================================
class OffensivePlayReward : public Reward {
private:
    static constexpr float GOAL_PROXIMITY = 2000.0f;  // Close to enemy goal
    static constexpr float MIN_BALL_SPEED = 500.0f;

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Pass must result in significant ball movement
        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < MIN_BALL_SPEED) return 0.0f;

        // Check for teammate in striking position
        float bestStrikingScore = 0.0f;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;

            float distToGoal = (other.pos - opponentGoal).Length();

            // Teammate close to enemy goal (striking position)
            if (distToGoal < GOAL_PROXIMITY) {
                // Check if opponent is blocking
                int blockingOpponents = 0;
                for (const auto& opp : state.players) {
                    if (opp.team == player.team) continue;
                    Vec toTeammate = (other.pos - opp.pos);
                    if (toTeammate.Length() < 500.0f) blockingOpponents++;
                }

                // Undefended or outnumbering defenders
                if (blockingOpponents <= 1) {
                    float positionScore = 1.0f - RS_CLAMP(distToGoal / GOAL_PROXIMITY, 0.0f, 1.0f);
                    bestStrikingScore = RS_MAX(bestStrikingScore, positionScore);
                }
            }
        }

        if (bestStrikingScore > 0.3f) {
            return 15.0f * bestStrikingScore;  // 0-15 reward for offensive setups
        }

        return 0.0f;
    }
};
