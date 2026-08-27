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
        static constexpr float OPTIMAL_SHADOW_DISTANCE = 1100.0f;  // Ideal distance from ball
        static constexpr float MIN_SHADOW_DISTANCE = 500.0f;       // Too close (challenging)
        static constexpr float MAX_SHADOW_DISTANCE = 2000.0f;      // Too far
        static constexpr float BASE_REWARD = 0.3f;
        static constexpr float VELOCITY_MATCH_BONUS = 0.3f;

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
    // BALL LANDING POSITION REWARD
    // Rev 1.0 (2026-07-07, NJH+Claude) — replaces BallTrajectoryInterceptionReward.
    //
    // Rewards whichever player (per team) is closest to the ball's actual predicted
    // post-bounce landing spot, while the ball is airborne and a bounce is imminent.
    // Uses BallPred::Cache's real bounce simulation (ground/wall/ceiling/backboard)
    // instead of a straight-line heuristic, and applies to both offense and defense
    // (not just balls heading toward the player's own goal).
    //
    // Trigger: ball airborne (z > AIRBORNE_HEIGHT_MIN) AND BallPred shows a first
    // bounce (ground or surface, whichever is sooner) within PREDICTION_WINDOW.
    // Anti-farming: gate depends on live ball state (can't camp a fixed spot); only
    // the closest teammate to the landing spot is rewarded (prevents every teammate
    // farming the same predicted spot in 2v2/3v3); smooth proximity curve, not a
    // flat threshold.
    //
    // W&B: BallLandingPosition/ActivationRate, /MeanReward
    // ============================================================================
    class BallLandingPositionReward : public Reward {
    private:
        static constexpr float PREDICTION_WINDOW  = 1.5f;    // only count bounces within this many seconds
        static constexpr float AIRBORNE_HEIGHT_MIN = 150.0f; // ball must be at least this high to count as "in flight"
        static constexpr float FALLOFF_RADIUS      = 2750.0f; // distance at which reward reaches 0
        static constexpr float BASE_SCALE          = 0.35f;

        BallPred::Cache _ballCache;

        inline static std::atomic<long long> s_rewardMilli{0};
        inline static std::atomic<int> s_activeFrames{0};

    public:
        struct Metrics {
            double totalReward  = 0.0;
            int    activeFrames = 0;
        };

        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.totalReward  = double(s_rewardMilli.exchange(0)) / 1000.0;
            m.activeFrames = s_activeFrames.exchange(0);
            return m;
        }

        virtual void PreStep(const GameState& state) override {
            _ballCache.Update(state);
        }

        virtual void Reset(const GameState& initialState) override {
            // No state needed
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            // Only meaningful while the ball is actually in flight
            if (state.ball.pos.z < AIRBORNE_HEIGHT_MIN) return 0.0f;

            // Find the first predicted bounce of any kind (ground or surface)
            float groundT  = _ballCache.FirstGroundHitTime();
            float surfaceT = _ballCache.FirstSurfaceHitTime();
            float landingT;
            if (groundT >= 0.0f && surfaceT >= 0.0f)  landingT = RS_MIN(groundT, surfaceT);
            else if (groundT >= 0.0f)                 landingT = groundT;
            else if (surfaceT >= 0.0f)                landingT = surfaceT;
            else                                       return 0.0f;  // no bounce predicted in the cache horizon

            if (landingT > PREDICTION_WINDOW) return 0.0f;

            Vec landingPos = _ballCache.AtTime(landingT).pos;

            // Only reward the player closest to the landing spot on their own team
            // (prevents every teammate farming the same predicted spot)
            float myDist = (player.pos - landingPos).Length();
            for (const auto& tm : state.players) {
                if (tm.team != player.team || tm.carId == player.carId || tm.isDemoed) continue;
                if ((tm.pos - landingPos).Length() < myDist) return 0.0f;
            }

            float proximity = RS_CLAMP(1.0f - myDist / FALLOFF_RADIUS, 0.0f, 1.0f);
            float reward = BASE_SCALE * proximity;

            s_activeFrames.fetch_add(1);
            if (reward > 0.0f) s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

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
    //
    // Rev 1.1: Cross-player sequence intent (deliberate, not an oversight) — a
    // TEAMMATE touching the ball mid-sequence does NOT invalidate a player's own
    // touchCount; only an opponent touch (state.lastTouchCarID going to the other
    // team) or the ball grounding does. This matches "ball possession in the air"
    // above rather than treating this as a strictly-solo juggling reward.
    // ============================================================================
    class MultiAirTouchReward : public Reward {
    private:
        struct AirTouchMemory {
            int touchCount = 0;           // Number of touches in current air sequence
            float lastTouchTime = 0.0f;   // currentTime value at last touch (seconds)
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
        // Rev 1.1: was an int incremented once per PreStep call (~15Hz post-tick-skip,
        // not the 120Hz the old TOUCH_TIMEOUT_STEPS/MIN_TOUCH_DELAY_STEPS comments
        // assumed) — switched to float seconds via state.deltaTime per the house
        // timing convention (CLAUDE.md §7C).
        float currentTime = 0.0f;

        static constexpr float BALL_GROUND_THRESHOLD = 150.0f;  // Height considered "on ground"
        static constexpr float MIN_TOUCH_HEIGHT = 300.0f;       // Only count touches above this height
        static constexpr float TOUCH_TIMEOUT_SECONDS = 1.0f;      // Reset if no touch for 1 real second
        static constexpr float MIN_TOUCH_DELAY_SECONDS = 0.5f;  // Minimum 0.5 real seconds between counted touches
        static constexpr float SINGLE_TOUCH_REWARD = 0.1f;
        static constexpr float DOUBLE_TOUCH_REWARD = 0.4f;
        static constexpr float TRIPLE_PLUS_REWARD = 0.7f;
        static constexpr float CONSISTENCY_BONUS = 0.3f;         // For spacing touches well
        static constexpr float HEIGHT_BONUS = 0.2f;             // For high aerial touches

        // NEW: Enhanced touch quality bonuses
        static constexpr float CONTACT_ANGLE_BONUS = 0.25f;       // Bonus for bottom-of-ball contacts
        static constexpr float SOFTNESS_BONUS = 0.15f;           // Bonus for soft first touch
        static constexpr float Z_VELOCITY_BONUS = 0.25f;         // Bonus for increasing ball height
        static constexpr float MAX_VELOCITY_DIFF_SOFT = 500.0f;  // Velocity diff threshold for "soft" contact
        static constexpr float ANGLE_TOLERANCE = 0.4f;           // Radians (~23°) tolerance for optimal angle

        // Soft penalty when ball horizontal (XY) speed > 2300 uu/s — car can't keep up.
        // Waived within 3000 UU of the opposing goal (hard-shot intent).
        float GetHorizVelPenaltyMul(const Player& player, const GameState& state) const {
            constexpr float HORIZ_VEL_LIMIT = 2300.0f;
            constexpr float HORIZ_VEL_MAX   = 3000.0f;
            constexpr float NEAR_GOAL_DIST  = 3000.0f;
            constexpr float MIN_MUL         = 0.5f;

            Vec oppGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_BACK
                : CommonValues::BLUE_GOAL_BACK;
            if ((state.ball.pos - oppGoal).Length() <= NEAR_GOAL_DIST)
                return 1.0f;

            float horizSpeed = sqrtf(state.ball.vel.x * state.ball.vel.x +
                                     state.ball.vel.y * state.ball.vel.y);
            if (horizSpeed <= HORIZ_VEL_LIMIT)
                return 1.0f;

            float t = RS_CLAMP((horizSpeed - HORIZ_VEL_LIMIT) / (HORIZ_VEL_MAX - HORIZ_VEL_LIMIT),
                               0.0f, 1.0f);
            return 1.0f - (1.0f - MIN_MUL) * t;  // [1.0, 0.5]
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            memory.clear();
            currentTime = 0.0f;
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
            if (mem.touchCount > 0 && (currentTime - mem.lastTouchTime) > TOUCH_TIMEOUT_SECONDS) {
                // Reset if we haven't touched in a while (ball likely hit ground or sequence broke)
                mem.touchCount = 0;
                mem.highestAltitude = 0.0f;
            }

            // Rev 1.1: Opponent-touch invalidation — if the ball's last toucher is on the
            // opposing team while our sequence is open, a turnover happened mid-air; clear
            // the sequence so a later retouch starts fresh instead of banking "combo" credit
            // across the interruption. A teammate holding last-touch is deliberately a no-op
            // (see class header). Modeled on AirborneOnPossessionLostPenalty.h's team lookup.
            if (mem.touchCount > 0 && state.lastTouchCarID >= 0
                && (uint32_t)state.lastTouchCarID != player.carId) {
                for (const auto& p : state.players) {
                    if (p.carId != (uint32_t)state.lastTouchCarID) continue;
                    if (p.team != player.team) {
                        mem.touchCount = 0;
                        mem.highestAltitude = 0.0f;
                    }
                    break;
                }
            }

            // Detect NEW touch
            if (player.ballTouchedStep && ballInAir && player.pos.z > MIN_TOUCH_HEIGHT) {
                // Check if this is a new touch (not the same touch as last frame)
                // AND ensure minimum 0.5s delay between touches
                if (currentTime != mem.lastTouchTime &&
                    (currentTime - mem.lastTouchTime) >= MIN_TOUCH_DELAY_SECONDS) {
                    mem.touchCount++;
                    mem.lastTouchTime = currentTime;

                    // NEW: Capture boost on first touch of sequence
                    if (mem.touchCount == 1) {
                        mem.sequenceStartBoost = player.boost;
                    }

                    // Track highest altitude of touches
                    mem.highestAltitude = RS_MAX(mem.highestAltitude, player.pos.z);

                    // Reward based on touch count — base tier, deliberately NOT boost-scaled
                    // (Rev 1.1: see boostMultiplier below, which now only scales bonusReward)
                    float baseTouchReward = 0.0f;
                    if (mem.touchCount == 1) {
                        baseTouchReward = SINGLE_TOUCH_REWARD;
                    }
                    else if (mem.touchCount == 2) {
                        baseTouchReward = DOUBLE_TOUCH_REWARD;
                    }
                    else if (mem.touchCount >= 3) {
                        baseTouchReward = TRIPLE_PLUS_REWARD;
                    }

                    float bonusReward = 0.0f;

                    // Bonus for well-spaced touches (consistency)
                    // We want touches spread out, not rapid-fire
                    if (mem.touchCount >= 2) {
                        bonusReward += CONSISTENCY_BONUS * 0.3f;  // Small bonus per touch
                    }

                    // Bonus for high altitude touches
                    if (mem.highestAltitude > 1500.0f) {
                        bonusReward += HEIGHT_BONUS;
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

                        bonusReward += CONTACT_ANGLE_BONUS * mem.lastContactAngleFactor;
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
                        bonusReward += SOFTNESS_BONUS * mem.lastSoftnessScore * softnessMultiplier;
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
                            bonusReward += Z_VELOCITY_BONUS * RS_CLAMP(mem.ballZVelocityGainLast / 500.0f, 0.0f, 1.0f);
                        }
                    }

                    // Rev 1.1: boost multiplier now scoped to bonus components only — a
                    // legitimate low-boost defensive clear still banks its base touch-tier
                    // reward, only the "extra polish" bonuses get discounted.
                    if (bonusReward > 0.0f) {
                        float boostMultiplier = RS_CLAMP(mem.sequenceStartBoost / 50.0f, 0.0f, 1.0f);
                        bonusReward *= boostMultiplier;
                    }

                    reward = baseTouchReward + bonusReward;

                    // Store ball state for next frame
                    mem.lastBallPos = state.ball.pos;
                    mem.lastBallVel = state.ball.vel;
                }
            }

            mem.ballWasInAir = ballInAir;

            // Horizontal speed cap: penalize ball horiz speed > 1900 uu/s (car can't keep up)
            if (reward > 0.0f)
                reward *= GetHorizVelPenaltyMul(player, state);

            return reward;
        }

        void PreStep(const GameState& state) override {
            currentTime += (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
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
        BallLandingPositionReward trajectoryInterception;
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
