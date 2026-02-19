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
#include "BallPrediction.h"
#include "../Math.h"
#include "AirDribbleUtilities.h"
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

        BallPred::Cache _ballCache;

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
        virtual void PreStep(const GameState& state) override {
            _ballCache.Update(state);
        }

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
    // Also rewards facing toward the inner part of the goal (toward the net).
    // PENALIZES staying at goal post if a teammate is already behind us.
    // ============================================================================
    class GoalpostPositioningReward : public Reward {
    private:
        static constexpr float FAR_POST_RADIUS = 400.0f;     // Distance to be "at" far post
        static constexpr float BALL_THREAT_DISTANCE = 3500.0f;  // Ball must be threatening
        static constexpr float BASE_REWARD = 0.5f;
        static constexpr float PERFECT_POSITION_BONUS = 0.5f;
        static constexpr float FACING_GOAL_BONUS = 0.3f;
        static constexpr float TEAMMATE_BEHIND_PENALTY = 0.6f;  // Heavy penalty for redundant positioning

        // Get the "back post" position (furthest goal post from ball)
        // Back post positioning is critical for shot blocking - stand at the post
        // furthest from the ball threat to maximize coverage of the goal line
        Vec GetBackPostPosition(const Player& player, const GameState& state) const {
            Vec goalCenter = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_LINE_CENTER : CommonValues::ORANGE_GOAL_LINE_CENTER;

            // Rocket League goal width is approximately 1785.5 units
            // Goal posts are at ±892.75 from center
            const float POST_OFFSET = 892.75f;

            // Define both goal posts on the goal line
            Vec leftPost = Vec(goalCenter.x - POST_OFFSET, goalCenter.y, 17.0f);
            Vec rightPost = Vec(goalCenter.x + POST_OFFSET, goalCenter.y, 17.0f);

            // Back post = the post furthest from the current ball position
            float distToBallLeft = (leftPost - state.ball.pos).Length();
            float distToBallRight = (rightPost - state.ball.pos).Length();
            Vec backPost = (distToBallLeft > distToBallRight) ? leftPost : rightPost;

            // Position slightly forward from goal line (toward net interior)
            // This allows the car to drive forward toward the ball if needed
            float forwardOffset = (player.team == Team::BLUE) ? 100.0f : -100.0f;
            return Vec(backPost.x, backPost.y + forwardOffset, 17.0f);
        }

        // Get the inner goal center (facing direction should point toward this)
        Vec GetGoalInnerPoint(const Player& player) const {
            return (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_CENTER : CommonValues::ORANGE_GOAL_CENTER;
        }

        // Check if player is facing toward the goal (into the net coverage area)
        float CalculateGoalFacingAlignment(const Player& player, const GameState& state) const {
            Vec goalInner = GetGoalInnerPoint(player);
            Vec toGoal = (goalInner - player.pos).Normalized();
            Vec carForward = player.rotMat.forward;

            // How well the car is facing toward the goal
            return carForward.Dot(toGoal);
        }

        // Check if any teammate is closer to our goal than us (behind us)
        bool HasTeammateCloserToGoal(const Player& player, const GameState& state) const {
            Vec ownGoal = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            float playerDistToGoal = (player.pos - ownGoal).Length();

            for (const auto& teammate : state.players) {
                // Skip self and opponents
                if (teammate.team != player.team || teammate.carId == player.carId) {
                    continue;
                }

                // Skip demoed teammates
                if (teammate.isDemoed) {
                    continue;
                }

                float tmDistToGoal = (teammate.pos - ownGoal).Length();

                // Teammate is behind us (closer to goal) - they should be the defender
                if (tmDistToGoal < playerDistToGoal) {
                    return true;
                }
            }

            return false;
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

            Vec backPost = GetBackPostPosition(player, state);
            float distToBackPost = (player.pos - backPost).Length();

            // Check if teammate is already defending
            bool hasTeammateDefending = HasTeammateCloserToGoal(player, state);

            // Reward based on proximity to back post
            float reward = 0.0f;

            if (distToBackPost < FAR_POST_RADIUS) {
                // At back post - excellent positioning for shot blocking
                reward = BASE_REWARD;

                // Bonus for perfect positioning (very close to post)
                if (distToBackPost < FAR_POST_RADIUS * 0.5f) {
                    reward += PERFECT_POSITION_BONUS;
                }

                // Bonus for facing toward the goal interior (covering the net)
                float goalFacingAlignment = CalculateGoalFacingAlignment(player, state);
                if (goalFacingAlignment > 0.5f) {
                    // Facing toward the goal - good coverage for blocking
                    reward += FACING_GOAL_BONUS;
                }
                else if (goalFacingAlignment < -0.5f) {
                    // Facing away from goal - bad coverage, penalize
                    reward *= 0.5f;
                }

                // Bonus for facing the ball (secondary bonus)
                Vec toBall = (state.ball.pos - player.pos).Normalized();
                Vec carForward = player.rotMat.forward;
                float facingAlignment = carForward.Dot(toBall);

                if (facingAlignment > 0.8f) {
                    reward *= 1.3f;
                }

                // PENALTY: If teammate is already defending (closer to goal), penalize redundant positioning
                if (hasTeammateDefending) {
                    reward *= (1.0f - TEAMMATE_BEHIND_PENALTY);
                }
            }
            else if (distToBackPost < FAR_POST_RADIUS * 2.0f) {
                // Moving toward back post - small reward
                reward = BASE_REWARD * 0.3f;

                // Penalize moving to post if teammate already defending
                if (hasTeammateDefending) {
                    reward *= (1.0f - TEAMMATE_BEHIND_PENALTY);
                }
            }

            return RS_CLAMP(reward, -1.0f, 1.0f);
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

        BallPred::Cache _ballCache;

        // Find interception point on line from predicted ball to goal
        Vec FindInterceptionPoint(const Vec& goalPos) const {
            // Use the cache slice nearest to PREDICTION_TIME for physics-accurate position
            Vec predictedBall = _ballCache.AtTime(PREDICTION_TIME).pos;

            Vec predictedToGoal = goalPos - predictedBall;
            float predictedDist = predictedToGoal.Length();

            if (predictedDist < 0.01f) return predictedBall;

            // Find point 30% of the way from predicted ball to goal
            return predictedBall + (predictedToGoal * 0.3f);
        }

    public:
        virtual void PreStep(const GameState& state) override {
            _ballCache.Update(state);
        }

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
            Vec interceptionPoint = FindInterceptionPoint(goalPos);
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
    // MULTI-AIR TOUCH REWARD
    // Rewards multiple consecutive touches on the ball in the air before it hits ground.
    // Teaches combo plays, aerial control, and ball possession in the air.
    // ============================================================================
    class MultiAirTouchReward : public Reward {
    private:
        struct AirTouchMemory {
            int touchCount = 0;           // Number of touches in current air sequence
            int lastTouchStep = 0;        // Step number of last touch
            float highestAltitude = 0.0f; // Highest altitude reached
            bool ballWasInAir = false;    // Was ball in air on last frame?

            // NEW: Contact angle and softness tracking
            float lastContactAngleFactor = 0.0f;  // How well-angled was last touch (0-1)
            float lastSoftnessScore = 0.0f;       // How soft was last touch (0-1)
            float ballZVelocityGainLast = 0.0f;   // Z velocity gain from last touch
            Vec lastBallPos = Vec(0, 0, 0);       // Ball position last frame
            Vec lastBallVel = Vec(0, 0, 0);       // Ball velocity last frame

            // NEW: Boost tracking
            float sequenceStartBoost = 0.0f;      // Boost level when sequence started
        };

        std::unordered_map<uint32_t, AirTouchMemory> memory;
        int currentStep = 0;

        static constexpr float BALL_GROUND_THRESHOLD = 150.0f;  // Height considered "on ground"
        static constexpr float MIN_TOUCH_HEIGHT = 300.0f;       // Only count touches above this height
        static constexpr float TOUCH_TIMEOUT_STEPS = 240;        // Reset if no touch for 1 sec (at 120 ticks/sec)
        static constexpr float MIN_TOUCH_DELAY_STEPS = 30;       // Minimum 0.5 sec between touches (at 120 ticks/sec)
        static constexpr float SINGLE_TOUCH_REWARD = 0.1f;
        static constexpr float DOUBLE_TOUCH_REWARD = 0.3f;
        static constexpr float TRIPLE_PLUS_REWARD = 0.5f;
        static constexpr float CONSISTENCY_BONUS = 0.2f;         // For spacing touches well
        static constexpr float HEIGHT_BONUS = 0.15f;             // For high aerial touches

        // NEW: Enhanced touch quality bonuses
        static constexpr float CONTACT_ANGLE_BONUS = 0.1f;       // Bonus for bottom-of-ball contacts
        static constexpr float SOFTNESS_BONUS = 0.15f;           // Bonus for soft first touch
        static constexpr float Z_VELOCITY_BONUS = 0.12f;         // Bonus for increasing ball height
        static constexpr float MAX_VELOCITY_DIFF_SOFT = 500.0f;  // Velocity diff threshold for "soft" contact
        static constexpr float ANGLE_TOLERANCE = 0.4f;           // Radians (~23°) tolerance for optimal angle

    public:
        virtual void Reset(const GameState& initialState) override {
            memory.clear();
            currentStep = 0;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            auto& mem = memory[player.carId];
            float reward = 0.0f;

            bool ballInAir = state.ball.pos.z > BALL_GROUND_THRESHOLD;

            // Check if ball has landed (was in air, now not) - reset sequence
            if (mem.ballWasInAir && !ballInAir) {
                // Sequence ended, ball hit ground
                mem.touchCount = 0;
                mem.highestAltitude = 0.0f;
                mem.ballWasInAir = false;
                return 0.0f;
            }

            // Check if touch timeout exceeded (too long without touch)
            if (mem.touchCount > 0 && (currentStep - mem.lastTouchStep) > TOUCH_TIMEOUT_STEPS) {
                // Reset if we haven't touched in a while (ball likely hit ground or sequence broke)
                mem.touchCount = 0;
                mem.highestAltitude = 0.0f;
            }

            // Detect NEW touch
            if (player.ballTouchedStep && ballInAir && player.pos.z > MIN_TOUCH_HEIGHT) {
                // Check if this is a new touch (not the same touch as last frame)
                // AND ensure minimum 0.5s delay between touches
                if (currentStep != mem.lastTouchStep &&
                    (currentStep - mem.lastTouchStep) >= MIN_TOUCH_DELAY_STEPS) {
                    mem.touchCount++;
                    mem.lastTouchStep = currentStep;

                    // NEW: Capture boost on first touch of sequence
                    if (mem.touchCount == 1) {
                        mem.sequenceStartBoost = player.boost;
                    }

                    // Track highest altitude of touches
                    mem.highestAltitude = RS_MAX(mem.highestAltitude, player.pos.z);

                    // Reward based on touch count
                    if (mem.touchCount == 1) {
                        reward = SINGLE_TOUCH_REWARD;
                    }
                    else if (mem.touchCount == 2) {
                        reward = DOUBLE_TOUCH_REWARD;
                    }
                    else if (mem.touchCount >= 3) {
                        reward = TRIPLE_PLUS_REWARD;
                    }

                    // Bonus for well-spaced touches (consistency)
                    // We want touches spread out, not rapid-fire
                    if (mem.touchCount >= 2) {
                        reward += CONSISTENCY_BONUS * 0.3f;  // Small bonus per touch
                    }

                    // Bonus for high altitude touches
                    if (mem.highestAltitude > 1500.0f) {
                        reward += HEIGHT_BONUS;
                    }

                    // ========== NEW: ENHANCED TOUCH QUALITY BONUSES ==========

                    // 1. CONTACT ANGLE BONUS - Reward touches on bottom of ball
                    if (state.prev) {
                        // Calculate optimal contact angle for field position
                        float desiredAngle = AirDribbleUtils::GetOptimalContactAngleForPosition(
                            player.team, state.ball.pos);

                        // Use ball normal relative to car position for actual angle
                        Vec toBall = (state.ball.pos - player.pos).Normalized();
                        float dotWithUp = toBall.Dot(player.rotMat.up);
                        float actualAngle = acos(RS_CLAMP(dotWithUp, -1.0f, 1.0f));

                        // Calculate angle factor (Gaussian scaling, peak at desired angle)
                        mem.lastContactAngleFactor = AirDribbleUtils::CalculateContactAngleFactor(
                            desiredAngle, actualAngle, ANGLE_TOLERANCE);

                        reward += CONTACT_ANGLE_BONUS * mem.lastContactAngleFactor;
                    }

                    // 2. SOFTNESS BONUS - Reward softer first touches
                    // Only apply strong bonus on first touch of sequence (more important to be soft on pickup)
                    if (mem.touchCount == 1 || mem.touchCount == 2) {
                        Vec velDiff = player.vel - state.ball.vel;
                        float velocityDifference = velDiff.Length();
                        mem.lastSoftnessScore = AirDribbleUtils::CalculateSoftnessBonus(
                            velocityDifference, MAX_VELOCITY_DIFF_SOFT);

                        // First touch gets full softness bonus, second touch gets reduced bonus
                        float softnessMultiplier = (mem.touchCount == 1) ? 1.0f : 0.5f;
                        reward += SOFTNESS_BONUS * mem.lastSoftnessScore * softnessMultiplier;
                    }

                    // 3. Z-VELOCITY GAIN BONUS - Reward lifting the ball higher
                    if (state.prev) {
                        mem.ballZVelocityGainLast = AirDribbleUtils::CalculateBallZVelocityGain(
                            state.prev->ball.pos.z,
                            state.ball.pos.z,
                            state.prev->ball.vel.z,
                            state.ball.vel.z);

                        // Only reward if ball is actually gaining height
                        if (mem.ballZVelocityGainLast > 0.0f) {
                            reward += Z_VELOCITY_BONUS * RS_CLAMP(mem.ballZVelocityGainLast / 500.0f, 0.0f, 1.0f);
                        }
                    }

                    // Store ball state for next frame
                    mem.lastBallPos = state.ball.pos;
                    mem.lastBallVel = state.ball.vel;
                }
            }

            mem.ballWasInAir = ballInAir;

            // NEW: Apply boost multiplier to all touch bonuses
            if (mem.touchCount > 0 && reward > 0.0f) {
                float boostMultiplier = RS_CLAMP(mem.sequenceStartBoost / 50.0f, 0.0f, 1.0f);
                reward *= boostMultiplier;
            }

            return reward;
        }

        void PreStep(const GameState& state) override {
            currentStep++;
        }

        // Helper to get current touch count for a player (useful for debugging/logging)
        int GetCurrentTouchCount(uint32_t carId) const {
            auto it = memory.find(carId);
            if (it != memory.end()) {
                return it->second.touchCount;
            }
            return 0;
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
