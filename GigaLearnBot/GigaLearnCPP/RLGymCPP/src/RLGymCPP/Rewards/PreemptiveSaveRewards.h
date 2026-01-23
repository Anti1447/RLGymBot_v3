/*
 * File: PreemptiveSaveRewards.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Rewards for pre-emptive saves and defensive positioning.
 *   Addresses the issue of bots not attempting saves by rewarding:
 *   - Early danger recognition
 *   - Pre-positioning between ball and goal
 *   - Shadow defense
 *   - Ball trajectory prediction and interception
 *   - Goalpost positioning
 *
 * Author: @user + Claude
 * Date: January 2026
 * Revision: 1.0
 */

#pragma once

#include "Reward.h"
#include "../Math.h"
#include <map>
#include <vector>
#include <cmath>
#include "../CommonValues.h"

namespace RLGC {

    // ============================================================================
    // EARLY DANGER DETECTION REWARD
    // Rewards recognizing dangerous situations early (ball moving toward goal).
    // This teaches the bot to react BEFORE the ball is too close.
    // ============================================================================
    class EarlyDangerDetectionReward : public Reward {
    private:
        static constexpr float DANGER_DISTANCE_FAR = 4000.0f;   // Start detecting danger
        static constexpr float DANGER_DISTANCE_NEAR = 2000.0f;  // Critical danger
        static constexpr float BALL_SPEED_THRESHOLD = 800.0f;   // Ball speed considered dangerous
        static constexpr float BASE_REWARD = 0.1f;
        static constexpr float CRITICAL_MULTIPLIER = 3.0f;

        // Predict where ball will be in N seconds
        Vec PredictBallPosition(const GameState& state, float seconds) const {
            // Simple linear prediction (could be improved with physics)
            return state.ball.pos + (state.ball.vel * seconds);
        }

        // Check if ball is heading toward goal
        bool IsBallHeadingTowardGoal(const Player& player, const GameState& state, const Vec& goalPos) const {
            Vec ballToGoal = goalPos - state.ball.pos;
            float ballSpeed = state.ball.vel.Length();

            if (ballSpeed < BALL_SPEED_THRESHOLD) {
                return false;
            }

            // Check if ball velocity is pointing toward goal
            float alignment = state.ball.vel.Dot(ballToGoal.Normalized());
            return alignment > 0.5f;  // At least somewhat aligned
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            // No state needed
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            Vec goalPos = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            // Check if ball is heading toward own goal
            if (!IsBallHeadingTowardGoal(player, state, goalPos)) {
                return 0.0f;
            }

            float ballDistToGoal = (state.ball.pos - goalPos).Length();

            // Only reward if player is positioning to defend
            float playerDistToGoal = (player.pos - goalPos).Length();
            float playerDistToBall = (player.pos - state.ball.pos).Length();

            // Player should be between ball and goal
            if (playerDistToGoal > ballDistToGoal) {
                return 0.0f;  // Behind the ball (wrong side)
            }

            // Reward based on how early we're detecting danger
            float reward = 0.0f;

            if (ballDistToGoal > DANGER_DISTANCE_FAR) {
                // Very early detection - small reward
                reward = BASE_REWARD * 0.5f;
            }
            else if (ballDistToGoal > DANGER_DISTANCE_NEAR) {
                // Good timing - moderate reward
                reward = BASE_REWARD;
            }
            else {
                // Critical danger - large reward for being positioned
                reward = BASE_REWARD * CRITICAL_MULTIPLIER;
            }

            // Bonus if player is facing the ball
            Vec toBall = (state.ball.pos - player.pos).Normalized();
            Vec carForward = player.rotMat.forward;
            float facingAlignment = carForward.Dot(toBall);

            if (facingAlignment > 0.7f) {
                reward *= 1.5f;  // Bonus for good positioning
            }

            return reward;
        }
    };

    // ============================================================================
    // SHADOW DEFENSE REWARD
    // Rewards "shadowing" the opponent - staying between ball and goal.
    // This is the #1 defensive technique in Rocket League.
    // ============================================================================
    class ShadowDefenseReward : public Reward {
    private:
        static constexpr float OPTIMAL_SHADOW_DISTANCE = 1200.0f;  // Ideal distance from ball
        static constexpr float MIN_SHADOW_DISTANCE = 400.0f;       // Too close (challenging)
        static constexpr float MAX_SHADOW_DISTANCE = 2500.0f;      // Too far
        static constexpr float BASE_REWARD = 0.3f;
        static constexpr float VELOCITY_MATCH_BONUS = 0.2f;

        // Calculate how well positioned player is between ball and goal
        float CalculateShadowQuality(const Player& player, const GameState& state, const Vec& goalPos) const {
            Vec ballToGoal = goalPos - state.ball.pos;
            Vec playerToBall = state.ball.pos - player.pos;

            // Check if player is on the line between ball and goal
            float alignment = playerToBall.Normalized().Dot(ballToGoal.Normalized());

            // alignment = 1.0 means perfect shadow (directly between)
            // alignment = 0.0 means perpendicular
            // alignment = -1.0 means behind ball (wrong side)

            return RS_CLAMP(alignment, 0.0f, 1.0f);
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            // No state needed
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            Vec goalPos = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            float ballDistToGoal = (state.ball.pos - goalPos).Length();
            float playerDistToGoal = (player.pos - goalPos).Length();

            // Only shadow when ball is in opponent's possession (closer to goal than us)
            if (playerDistToGoal < ballDistToGoal - 500.0f) {
                return 0.0f;  // We're too far forward
            }

            float playerDistToBall = (player.pos - state.ball.pos).Length();

            // Check distance from ball (optimal shadow range)
            if (playerDistToBall < MIN_SHADOW_DISTANCE || playerDistToBall > MAX_SHADOW_DISTANCE) {
                return 0.0f;  // Too close or too far
            }

            // Calculate shadow quality (alignment)
            float shadowQuality = CalculateShadowQuality(player, state, goalPos);

            if (shadowQuality < 0.5f) {
                return 0.0f;  // Not shadowing well enough
            }

            // Base reward scaled by shadow quality
            float reward = BASE_REWARD * shadowQuality;

            // Distance factor (optimal distance gets full reward)
            float distanceFromOptimal = std::abs(playerDistToBall - OPTIMAL_SHADOW_DISTANCE);
            float distanceFactor = 1.0f - RS_CLAMP(distanceFromOptimal / OPTIMAL_SHADOW_DISTANCE, 0.0f, 1.0f);
            reward *= (0.5f + distanceFactor * 0.5f);  // Scale between 0.5x and 1.0x

            // Bonus for matching ball velocity (mirroring movement)
            Vec playerVelNorm = player.vel.Normalized();
            Vec ballVelNorm = state.ball.vel.Normalized();
            float velocityMatch = playerVelNorm.Dot(ballVelNorm);

            if (velocityMatch > 0.7f) {
                reward += VELOCITY_MATCH_BONUS;
            }

            return reward;
        }
    };

    // ============================================================================
    // GOALPOST POSITIONING REWARD
    // Rewards positioning at the "far post" during defensive situations.
    // This is optimal positioning for saves and prevents most goals.
    // ============================================================================
    class GoalpostPositioningReward : public Reward {
    private:
        static constexpr float FAR_POST_RADIUS = 400.0f;     // Distance to be "at" far post
        static constexpr float BALL_THREAT_DISTANCE = 3500.0f;  // Ball must be threatening
        static constexpr float BASE_REWARD = 0.5f;
        static constexpr float PERFECT_POSITION_BONUS = 0.5f;

        // Get the "far post" position (opposite side from ball)
        Vec GetFarPostPosition(const Player& player, const GameState& state) const {
            Vec goalCenter = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_LINE_CENTER : CommonValues::ORANGE_GOAL_LINE_CENTER;

            // Determine which side of goal the ball is on
            float ballX = state.ball.pos.x;
            float goalX = goalCenter.x;

            // Far post is on opposite side from ball
            float farPostX = (ballX > goalX) ? (goalX - 700.0f) : (goalX + 700.0f);

            // Position slightly forward from goal line
            float goalY = goalCenter.y;
            float farPostY = (player.team == Team::BLUE) ? (goalY + 200.0f) : (goalY - 200.0f);

            return Vec(farPostX, farPostY, 17.0f);  // Ground level
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            // No state needed
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            Vec goalPos = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            float ballDistToGoal = (state.ball.pos - goalPos).Length();

            // Only reward when ball is threatening
            if (ballDistToGoal > BALL_THREAT_DISTANCE) {
                return 0.0f;
            }

            Vec farPost = GetFarPostPosition(player, state);
            float distToFarPost = (player.pos - farPost).Length();

            // Reward based on proximity to far post
            float reward = 0.0f;

            if (distToFarPost < FAR_POST_RADIUS) {
                // At far post - excellent positioning
                reward = BASE_REWARD;

                // Bonus for perfect positioning (very close)
                if (distToFarPost < FAR_POST_RADIUS * 0.5f) {
                    reward += PERFECT_POSITION_BONUS;
                }

                // Bonus for facing the ball
                Vec toBall = (state.ball.pos - player.pos).Normalized();
                Vec carForward = player.rotMat.forward;
                float facingAlignment = carForward.Dot(toBall);

                if (facingAlignment > 0.8f) {
                    reward *= 1.3f;
                }
            }
            else if (distToFarPost < FAR_POST_RADIUS * 2.0f) {
                // Moving toward far post - small reward
                reward = BASE_REWARD * 0.3f;
            }

            return reward;
        }
    };

    // ============================================================================
    // BALL TRAJECTORY INTERCEPTION REWARD
    // Rewards positioning to intercept the ball's trajectory toward goal.
    // Uses physics prediction to find optimal interception point.
    // ============================================================================
    class BallTrajectoryInterceptionReward : public Reward {
    private:
        static constexpr float PREDICTION_TIME = 1.5f;  // Predict 1.5 seconds ahead
        static constexpr float INTERCEPTION_RADIUS = 500.0f;
        static constexpr float BASE_REWARD = 0.4f;
        static constexpr float EARLY_INTERCEPTION_BONUS = 0.3f;

        // Simple ball trajectory prediction
        Vec PredictBallPosition(const GameState& state, float seconds) const {
            // Linear prediction with gravity
            Vec predictedPos = state.ball.pos + (state.ball.vel * seconds);
            predictedPos.z -= 0.5f * 650.0f * seconds * seconds;  // Gravity

            // Clamp to field bounds
            predictedPos.z = RS_MAX(predictedPos.z, 93.0f);  // Ball radius
            return predictedPos;
        }

        // Find interception point on line from ball to goal
        Vec FindInterceptionPoint(const GameState& state, const Vec& goalPos) const {
            Vec ballToGoal = goalPos - state.ball.pos;
            float distToGoal = ballToGoal.Length();

            if (distToGoal < 0.01f) return state.ball.pos;

            // Predict ball position
            Vec predictedBall = PredictBallPosition(state, PREDICTION_TIME);

            // Interception point is on line between predicted ball and goal
            Vec predictedToGoal = goalPos - predictedBall;
            float predictedDist = predictedToGoal.Length();

            if (predictedDist < 0.01f) return predictedBall;

            // Find point 30% of the way from predicted ball to goal
            Vec interceptionPoint = predictedBall + (predictedToGoal * 0.3f);

            return interceptionPoint;
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            // No state needed
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            Vec goalPos = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            // Check if ball is moving toward goal
            Vec ballToGoal = goalPos - state.ball.pos;
            float ballSpeed = state.ball.vel.Length();

            if (ballSpeed < 500.0f) {
                return 0.0f;  // Ball not moving fast enough to be threatening
            }

            float alignment = state.ball.vel.Dot(ballToGoal.Normalized());
            if (alignment < 0.3f) {
                return 0.0f;  // Ball not heading toward goal
            }

            // Find optimal interception point
            Vec interceptionPoint = FindInterceptionPoint(state, goalPos);
            float distToInterception = (player.pos - interceptionPoint).Length();

            // Reward proximity to interception point
            float reward = 0.0f;

            if (distToInterception < INTERCEPTION_RADIUS) {
                // At interception point
                float proximityFactor = 1.0f - (distToInterception / INTERCEPTION_RADIUS);
                reward = BASE_REWARD * proximityFactor;

                // Bonus for early positioning (before ball gets close)
                float ballDistToGoal = (state.ball.pos - goalPos).Length();
                if (ballDistToGoal > 3000.0f) {
                    reward += EARLY_INTERCEPTION_BONUS;
                }
            }

            return reward;
        }
    };

    // ============================================================================
    // DEFENSIVE READINESS REWARD
    // Rewards being in "ready" state: on ground, facing ball, boost available.
    // Only active during defensive situations.
    // ============================================================================
    class DefensiveReadinessReward : public Reward {
    private:
        static constexpr float BALL_THREAT_DISTANCE = 3000.0f;
        static constexpr float MIN_BOOST_THRESHOLD = 12.0f;  // Minimum boost for saves
        static constexpr float BASE_REWARD = 0.15f;
        static constexpr float ALL_CONDITIONS_BONUS = 0.15f;

    public:
        virtual void Reset(const GameState& initialState) override {
            // No state needed
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            Vec goalPos = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            float ballDistToGoal = (state.ball.pos - goalPos).Length();
            float playerDistToGoal = (player.pos - goalPos).Length();

            // Only reward during defensive situations
            if (ballDistToGoal > BALL_THREAT_DISTANCE) {
                return 0.0f;
            }

            // Player should be between ball and goal
            if (playerDistToGoal > ballDistToGoal) {
                return 0.0f;
            }

            float reward = 0.0f;
            int conditionsMet = 0;

            // Condition 1: On ground (ready to jump/dodge)
            if (player.isOnGround) {
                reward += BASE_REWARD;
                conditionsMet++;
            }

            // Condition 2: Facing ball
            Vec toBall = (state.ball.pos - player.pos).Normalized();
            Vec carForward = player.rotMat.forward;
            float facingAlignment = carForward.Dot(toBall);

            if (facingAlignment > 0.7f) {
                reward += BASE_REWARD;
                conditionsMet++;
            }

            // Condition 3: Has boost for aerial saves
            if (player.boost >= MIN_BOOST_THRESHOLD) {
                reward += BASE_REWARD;
                conditionsMet++;
            }

            // Condition 4: Reasonable speed (not stationary, not too fast)
            float speed = player.vel.Length();
            if (speed > 300.0f && speed < 1800.0f) {
                reward += BASE_REWARD;
                conditionsMet++;
            }

            // Bonus for meeting all conditions
            if (conditionsMet == 4) {
                reward += ALL_CONDITIONS_BONUS;
            }

            return reward;
        }
    };

    // ============================================================================
    // GOAL LINE CLEARANCE REWARD
    // High reward for touching ball when it's very close to goal line.
    // This is the "last chance" save reward.
    // ============================================================================
    class GoalLineClearanceReward : public Reward {
    private:
        static constexpr float CRITICAL_DISTANCE = 800.0f;   // Very close to goal
        static constexpr float URGENT_DISTANCE = 1500.0f;    // Close to goal
        static constexpr float CRITICAL_REWARD = 5.0f;
        static constexpr float URGENT_REWARD = 2.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            // No state needed
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) {
                return 0.0f;  // No touch
            }

            Vec goalPos = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            float ballDistToGoal = (state.ball.pos - goalPos).Length();

            // Massive reward for critical saves
            if (ballDistToGoal < CRITICAL_DISTANCE) {
                return CRITICAL_REWARD;
            }
            // Large reward for urgent saves
            else if (ballDistToGoal < URGENT_DISTANCE) {
                return URGENT_REWARD;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // COMBINED PREEMPTIVE SAVE REWARD
    // Convenience class combining all pre-emptive save rewards
    // ============================================================================
    class PreemptiveSaveCombinedReward : public Reward {
    private:
        EarlyDangerDetectionReward earlyDanger;
        ShadowDefenseReward shadowDefense;
        GoalpostPositioningReward goalpostPositioning;
        BallTrajectoryInterceptionReward trajectoryInterception;
        DefensiveReadinessReward defensiveReadiness;
        GoalLineClearanceReward goalLineClearance;

        // Weights
        float earlyDangerWeight;
        float shadowDefenseWeight;
        float goalpostWeight;
        float trajectoryWeight;
        float readinessWeight;
        float clearanceWeight;

    public:
        PreemptiveSaveCombinedReward(
            float earlyDangerW = 1.0f,
            float shadowDefenseW = 2.0f,      // Most important
            float goalpostW = 1.5f,
            float trajectoryW = 1.2f,
            float readinessW = 0.8f,
            float clearanceW = 3.0f           // Critical
        ) : earlyDangerWeight(earlyDangerW),
            shadowDefenseWeight(shadowDefenseW),
            goalpostWeight(goalpostW),
            trajectoryWeight(trajectoryW),
            readinessWeight(readinessW),
            clearanceWeight(clearanceW) {}

        virtual void Reset(const GameState& initialState) override {
            earlyDanger.Reset(initialState);
            shadowDefense.Reset(initialState);
            goalpostPositioning.Reset(initialState);
            trajectoryInterception.Reset(initialState);
            defensiveReadiness.Reset(initialState);
            goalLineClearance.Reset(initialState);
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float total = 0.0f;

            total += earlyDanger.GetReward(player, state, isFinal) * earlyDangerWeight;
            total += shadowDefense.GetReward(player, state, isFinal) * shadowDefenseWeight;
            total += goalpostPositioning.GetReward(player, state, isFinal) * goalpostWeight;
            total += trajectoryInterception.GetReward(player, state, isFinal) * trajectoryWeight;
            total += defensiveReadiness.GetReward(player, state, isFinal) * readinessWeight;
            total += goalLineClearance.GetReward(player, state, isFinal) * clearanceWeight;

            return total;
        }

        // Setters for weights
        void SetWeights(float earlyDanger, float shadowDefense, float goalpost,
                       float trajectory, float readiness, float clearance) {
            earlyDangerWeight = earlyDanger;
            shadowDefenseWeight = shadowDefense;
            goalpostWeight = goalpost;
            trajectoryWeight = trajectory;
            readinessWeight = readiness;
            clearanceWeight = clearance;
        }
    };

} // namespace RLGC
