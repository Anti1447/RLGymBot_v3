#pragma once
#include "../Framework.h"
#include "../CommonValues.h"
#include <cmath>

namespace RLGC {

    // ============================================================================
    // DOUBLE COMMIT PENALTY - Penalize multiple teammates on same touch
    // ============================================================================
    // In multi-modal training (1v1, 2v2, 3v3), penalize situations where multiple
    // bots on the same team are going for the same ball touch. This teaches proper
    // positioning and rotation instead of ball-chasing mentality.
    //
    // Immediate punishment: -0.1 per additional teammate within threshold
    // Continuous effect: Applied every frame teammates overlap
    //
    // Rev 1.0 (2025-01-15, NJH)
    class DoubleCommitPenalty : public Reward {
    public:
        float commitDistance;      // Distance within which bots are "committing"
        float penaltyPerBot;       // Punishment per additional committing teammate
        float minDistBeforePenalty; // Don't penalize if ball is extremely close

        DoubleCommitPenalty(
            float distance = 1500.0f,
            float penalty = 0.1f,
            float minDist = 300.0f
        ) : commitDistance(distance),
            penaltyPerBot(penalty),
            minDistBeforePenalty(minDist) {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            Vec toBall = state.ball.pos - player.pos;
            float distToBall = toBall.Length();

            // Don't penalize if we're basically on the ball
            if (distToBall < minDistBeforePenalty) return 0.0f;

            // Count how many teammates are within commit range of ball
            int commitCount = 0;
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;

                float tmDist = (state.ball.pos - teammate.pos).Length();
                if (tmDist < commitDistance) {
                    commitCount++;
                }
            }

            // First teammate is allowed (2v2 has 2 per team, so max 1 other)
            // Penalize for each additional one
            if (commitCount > 0) {
                return -penaltyPerBot * commitCount;  // Immediate continuous penalty
            }

            return 0.0f;
        }
    };

    // NOTE: FieldSpacingReward is already defined in CompleteMechanicsRewards.h
    // Use that implementation for multi-modal training


    // ============================================================================
    // MIDFIELD ROTATION REWARD - Return to midfield after offensive play
    // ============================================================================
    // In multi-team scenarios, bots should rotate back to midfield after
    // committing offensively, especially when:
    // - Low on boost (can't chase efficiently)
    // - Far from ball (not helping offense)
    // - Ball is in own half (defensive situation)
    //
    // Immediate reward: +0.15 per second moving toward midfield when criteria met
    // Bonus: +0.5 event reward when successfully reaching midfield
    //
    // Avoids vanishing gradients by:
    // 1. Continuous reward (0.15/s) for every frame moving in right direction
    // 2. Clear immediate feedback on heading toward goal
    // 3. Event bonus (0.5) when reaching midfield zone
    //
    // Rev 1.0 (2025-01-15, NJH)
    class MiddleRotationReward : public Reward {
    public:
        float boostThreshold;      // Below this boost level, should rotate back
        float ballDistThreshold;   // If farther than this from ball, consider rotating
        float midlineY;            // Y-coordinate of midline (use 0.0 for center)
        float rotationRewardRate;  // Reward per frame when rotating appropriately
        float midlineReachBonus;   // One-time bonus for reaching midfield

        mutable std::unordered_map<uint32_t, bool> wasInOffensiveZone;
        mutable std::unordered_map<uint32_t, float> lastReachReward;

        MiddleRotationReward(
            float boost = 30.0f,
            float ballDist = 3000.0f,
            float midY = 0.0f,
            float rotRate = 0.15f,
            float reachBonus = 0.5f
        ) : boostThreshold(boost),
            ballDistThreshold(ballDist),
            midlineY(midY),
            rotationRewardRate(rotRate),
            midlineReachBonus(reachBonus) {
        }

        bool IsInOffensiveZone(const Player& player, const GameState& state) const {
            Vec ownGoal = (player.team == Team::BLUE)
                ? CommonValues::BLUE_GOAL_CENTER
                : CommonValues::ORANGE_GOAL_CENTER;

            float distToOwnGoal = (player.pos - ownGoal).Length();

            // Offensive zone: far from own goal, pushing forward
            return distToOwnGoal > 3000.0f;
        }

        bool ShouldRotate(const Player& player, const GameState& state) const {
            // Low on boost OR far from ball = should rotate back
            float distToBall = (player.pos - state.ball.pos).Length();

            return (player.boost < boostThreshold) || (distToBall > ballDistThreshold);
        }

        bool IsInMidfield(const Player& player, const GameState& state) const {
            Vec ownGoal = (player.team == Team::BLUE)
                ? CommonValues::BLUE_GOAL_CENTER
                : CommonValues::ORANGE_GOAL_CENTER;

            float distToOwnGoal = (player.pos - ownGoal).Length();

            // Midfield: roughly 2000-4000 units from own goal
            return distToOwnGoal >= 1500.0f && distToOwnGoal <= 4000.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            bool currentlyOffensive = IsInOffensiveZone(player, state);
            bool shouldRotateNow = ShouldRotate(player, state);
            bool inMidfield = IsInMidfield(player, state);

            // Track state transitions
            if (currentlyOffensive) {
                wasInOffensiveZone[player.carId] = true;
            }

            float reward = 0.0f;

            // IMMEDIATE CONTINUOUS REWARD: If we were offensive and should rotate, reward rotation
            if (wasInOffensiveZone[player.carId] && shouldRotateNow) {
                // Reward based on movement toward own goal
                Vec ownGoal = (player.team == Team::BLUE)
                    ? CommonValues::BLUE_GOAL_CENTER
                    : CommonValues::ORANGE_GOAL_CENTER;

                Vec toGoal = (ownGoal - player.pos).Normalized();
                float goalwheelVelocity = player.vel.Dot(toGoal);

                // Positive velocity toward goal = reward
                if (goalwheelVelocity > 500.0f) {
                    reward = rotationRewardRate;  // Continuous reward every frame
                }
            }

            // EVENT BONUS: Reaching midfield from offensive zone
            if (wasInOffensiveZone[player.carId] && inMidfield) {
                // One-time bonus per rotation cycle
                if (lastReachReward[player.carId] < state.deltaTime - 5.0f) {
                    reward += midlineReachBonus;
                    lastReachReward[player.carId] = state.deltaTime;
                    wasInOffensiveZone[player.carId] = false;  // Reset cycle
                }
            }

            return reward;
        }

        virtual void Reset(const GameState& initialState) override {
            wasInOffensiveZone.clear();
            lastReachReward.clear();
        }
    };


    // ============================================================================
    // GOOD RECOVERY REWARD - Landing on wheels
    // ============================================================================
    // Rewards landing smoothly on 2+ wheels after airtime, with a slight bonus
    // for wavedashing. Focus is on clean recoveries without flip-spamming.
    //
    // Immediate reward structure:
    // - +0.3 for landing on 2+ wheels (main reward)
    // - +0.05 small bonus for wavedashing during recovery
    // - -0.1 if landing on <2 wheels (unstable landing)
    //
    // Avoids vanishing gradients by:
    // 1. Immediate reward on landing (same frame as contact)
    // 2. No flip incentive - prevents flip-spamming behavior
    // 3. Slight wavedash bonus - encourages skill without forcing it
    //
    // Rev 2.0 (2025-01-15, NJH) - Simplified to prevent flip spam
    class GoodRecoveryReward : public Reward {
    public:
        mutable std::unordered_map<uint32_t, bool> wasInAir;

        GoodRecoveryReward() {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            float reward = 0.0f;

            if (player.isOnGround) {
                // Check if we just landed from air
                if (wasInAir[player.carId]) {
                    // We were in air, now on ground - landing event!
                    wasInAir[player.carId] = false;

                    // Count wheels in contact
                    int wheelsInContact = 0;
                    if (player.wheelsWithContact[0]) wheelsInContact++;
                    if (player.wheelsWithContact[1]) wheelsInContact++;
                    if (player.wheelsWithContact[2]) wheelsInContact++;
                    if (player.wheelsWithContact[3]) wheelsInContact++;

                    // MAIN REWARD: 2+ wheels = good landing, <2 = poor landing
                    if (wheelsInContact >= 2) {
                        reward += 0.3f;  // Smooth stable landing on multiple wheels
                    } else {
                        reward -= 0.1f;  // Unstable landing on 0-1 wheels
                    }

                    // SLIGHT BONUS: Wavedashing (but not flip spam)
                    // Wavedashing is detected by handbrake + jump state
                    // hasFlipped here means we DID flip, so we skip this bonus
                    if (!player.hasFlipped && player.prevAction.handbrake > 0.5f) {
                        // Small bonus for wavedashing (not flipping)
                        if (wheelsInContact >= 2) {
                            reward += 0.05f;
                        }
                    }
                }
            } else {
                // In air
                wasInAir[player.carId] = true;
            }

            return reward;
        }

        virtual void Reset(const GameState& initialState) override {
            wasInAir.clear();
        }
    };


    // ============================================================================
    // TEAMMATE BUMP PENALTY - Penalize bumping teammates
    // ============================================================================
    // In multi-modal training (2v2, 3v3), teammates are allies and bumping them
    // disrupts team coordination and wastes momentum. This penalty teaches respect
    // for teammate positioning and avoids chaotic collisions.
    //
    // Immediate punishment: -0.5 whenever you bump a teammate
    // Continuous effect: Applied every frame a bump happens
    //
    // Rev 1.0 (2025-01-15, NJH)
    class TeammteBumpPenalty : public Reward {
    public:
        float penaltyAmount;

        TeammteBumpPenalty(float penalty = 0.5f) : penaltyAmount(penalty) {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            // Check if we bumped a teammate
            // The bump event is recorded in player.eventState.bump
            // But we need to verify it was against a teammate
            if (!player.eventState.bump) return 0.0f;

            // Find which player we bumped
            // We look for a teammate who was bumped in this frame
            for (const auto& other : state.players) {
                if (other.team != player.team) continue;  // Must be a teammate
                if (other.carId == player.carId) continue;  // Not ourselves
                if (!other.eventState.bumped) continue;  // They must have been bumped

                // Check if we're close enough that we could have bumped them
                float distToOther = (player.pos - other.pos).Length();
                if (distToOther < 300.0f) {  // Collision distance threshold
                    // We bumped a teammate - apply penalty
                    return -penaltyAmount;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // PASSING SETUP REWARD - Reward setting up a pass trajectory
    // ============================================================================
    // This reward encourages:
    // 1. Ball velocity moving toward a teammate (not opponent)
    // 2. Teammate closer to ball destination than opponents
    // 3. Clean, predictable ball trajectories (not crazy bounces)
    //
    // Strategy:
    // - Compute ball velocity direction
    // - Check if velocity points toward teammate space (not opponent space)
    // - Trace ball trajectory 1-2 bounces ahead
    // - Reward if teammates are in better position than opponents along trajectory
    // - Light reward for just creating good ball velocity toward team half
    //
    // Reward structure:
    // - +2.0: Ball moving toward teammate, teammate closer than closest opponent
    // - +0.8: Ball moving toward team half even if no clear teammate position yet
    // - +1.5: Bonus when pass trajectory avoids heavy wall/ceiling bounces
    // - -0.2: Penalty if ball moving toward opponent space
    //
    // Rev 1.0 (2025-01-21, NJH)
    class PassingSetupReward : public Reward {
    private:
        // Predict ball position after 1-2 bounces
        Vec PredictBallTrajectory(const PhysState& ball, int bounces = 1) const {
            Vec pos = ball.pos;
            Vec vel = ball.vel;

            // Simple physics: gravity and basic drag
            // Simulate for ~1 second (120 frames * tick_time / tick_skip)
            const float dt = 1.0f / 120.0f;
            const float gravAccel = CommonValues::BALL_GRAVITY;
            const float dragCoef = 0.03f;

            for (int i = 0; i < 120; i++) {  // ~1 second forward
                // Apply gravity
                vel.z += gravAccel * dt;

                // Apply drag
                float speed = vel.Length();
                if (speed > 0.1f) {
                    vel = vel * (1.0f - dragCoef * dt);
                }

                // Update position
                pos = pos + vel * dt;

                // Ground bounce check
                if (pos.z < CommonValues::BALL_RADIUS) {
                    pos.z = CommonValues::BALL_RADIUS;
                    vel.z = -vel.z * 0.6f;  // Bounce coefficient
                    bounces--;
                    if (bounces <= 0) break;  // Stop predicting after N bounces
                }

                // Wall collision (simple)
                if (std::abs(pos.x) > CommonValues::SIDE_WALL_X) {
                    pos.x = RS_CLAMP(pos.x, -CommonValues::SIDE_WALL_X, CommonValues::SIDE_WALL_X);
                    vel.x = -vel.x * 0.8f;
                }
            }

            return pos;
        }

        bool IsTeammatePosition(const Vec& pos, const Player& player, const GameState& state, float proximityRadius = 1500.0f) const {
            // Check if any teammate is close to this position
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;

                float distToPos = (teammate.pos - pos).Length();
                if (distToPos < proximityRadius) {
                    return true;
                }
            }
            return false;
        }

        float GetTeammateDistToPos(const Vec& pos, const Player& player, const GameState& state) const {
            float minDist = 1e6f;
            for (const auto& teammate : state.players) {
                if (teammate.team != player.team) continue;
                if (teammate.carId == player.carId) continue;
                if (teammate.isDemoed) continue;

                float dist = (teammate.pos - pos).Length();
                minDist = RS_MIN(minDist, dist);
            }
            return minDist;
        }

        float GetOpponentDistToPos(const Vec& pos, const Player& player, const GameState& state) const {
            float minDist = 1e6f;
            for (const auto& opponent : state.players) {
                if (opponent.team == player.team) continue;
                if (opponent.isDemoed) continue;

                float dist = (opponent.pos - pos).Length();
                minDist = RS_MIN(minDist, dist);
            }
            return minDist;
        }

    public:
        PassingSetupReward() {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            // Only reward if WE just touched the ball (setting up the pass)
            if (!player.ballTouchedStep) return 0.0f;

            // Check ball velocity direction
            Vec ballVel = state.ball.vel;
            if (ballVel.Length() < 100.0f) return 0.0f;  // Ball too slow to be a pass

            Vec ballVelNorm = ballVel.Normalized();

            // Get our goal (which direction is "forward" for us)
            Vec ourGoal = (player.team == Team::BLUE)
                ? CommonValues::BLUE_GOAL_CENTER
                : CommonValues::ORANGE_GOAL_CENTER;

            Vec theirGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            Vec towardTheirGoal = (theirGoal - player.pos).Normalized();

            // Check if ball is moving generally toward their goal (not back to our goal)
            float goalwardComponent = ballVelNorm.Dot(towardTheirGoal);

            if (goalwardComponent < 0.2f) {
                // Ball moving backward - not a good pass setup
                return -0.2f;
            }

            // Predict where ball will be after bounces
            Vec predictedBallPos = PredictBallTrajectory(state.ball, 1);  // 1 bounce

            // Check if teammates are better positioned than opponents at ball destination
            float tmDist = GetTeammateDistToPos(predictedBallPos, player, state);
            float oppDist = GetOpponentDistToPos(predictedBallPos, player, state);

            float reward = 0.0f;

            // PRIMARY REWARD: Ball trajectory favors teammates
            if (tmDist < 1e6f && oppDist < 1e6f) {
                if (tmDist < oppDist - 500.0f) {
                    // Teammate is significantly closer - good pass setup!
                    reward += 2.0f;
                }
                else if (tmDist < oppDist) {
                    // Teammate slightly closer - decent pass
                    reward += 1.0f;
                }
            }

            // SECONDARY REWARD: Ball moving toward team half (even if no clear receiver)
            Vec teamHalfDir = (theirGoal - player.pos).Normalized();
            if (ballVelNorm.Dot(teamHalfDir) > 0.5f) {
                reward += 0.8f;
            }

            // TRAJECTORY QUALITY BONUS: Reward predictable passes (not many bounces)
            // Penalize crazy bounces that are hard to receive
            int bounceCount = 0;
            Vec testPos = state.ball.pos;
            for (int i = 0; i < 50 && bounceCount < 3; i++) {
                testPos.z += state.ball.vel.z * (1.0f / 120.0f);
                if (testPos.z < CommonValues::BALL_RADIUS) {
                    bounceCount++;
                }
            }

            if (bounceCount == 0) {
                reward += 1.5f;  // Clean pass with no bounces - excellent!
            }
            else if (bounceCount == 1) {
                reward += 0.8f;  // One bounce - acceptable
            }
            // bounceCount >= 2: no bonus, chaotic trajectory

            return reward;
        }
    };


    // ============================================================================
    // RECEIVING PASS REWARD - Reward receiving a good pass from teammate
    // ============================================================================
    // This reward encourages:
    // 1. Positioning to receive passes (being in teammate passing lanes)
    // 2. Touching the ball when it's already moving toward you (receiving)
    // 3. Maintaining good position relative to ball trajectory
    //
    // Strategy:
    // - Track ball velocity and which team member moved it last
    // - If teammate moved ball toward us, reward our positioning/touch
    // - Reward based on how cleanly we receive (ball speed alignment)
    // - Bonus for converting receive into a shot or dribble setup
    //
    // Reward structure:
    // - +1.5: Touching ball that teammate passed toward you
    // - +0.5: Good positioning relative to incoming pass trajectory
    // - +2.0: Successfully converting receive into immediate shot/play
    // - -0.3: Missing a pass opportunity (ball flies past without touch)
    //
    // Rev 1.0 (2025-01-21, NJH)
    class ReceivingPassReward : public Reward {
    private:
        mutable std::unordered_map<uint32_t, uint32_t> lastBallTouchBy;
        mutable std::unordered_map<uint32_t, float> passReceiveTime;
        mutable std::unordered_map<uint32_t, bool> recentPassToMe;
        mutable std::unordered_map<uint32_t, float> elapsedTime;  // Track time manually

    public:
        ReceivingPassReward() {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            // Update elapsed time
            uint32_t id = player.carId;
            elapsedTime[id] += state.deltaTime;

            float reward = 0.0f;

            // TRACK WHO LAST TOUCHED THE BALL
            for (const auto& p : state.players) {
                if (p.ballTouchedStep && p.team == player.team && p.carId != player.carId) {
                    lastBallTouchBy[0] = p.carId;  // Teammate touched ball
                }
            }

            // Check if this is a pass intended for us
            bool isPassToMe = false;

            if (lastBallTouchBy[0] != 0 && lastBallTouchBy[0] != player.carId) {
                // A teammate (not us) recently touched the ball
                // Check if ball is moving toward us

                Vec ballVel = state.ball.vel;
                if (ballVel.Length() > 200.0f) {  // Ball is moving
                    Vec toUs = (player.pos - state.ball.pos).Normalized();
                    float towardComponent = ballVel.Normalized().Dot(toUs);

                    if (towardComponent > 0.3f) {
                        // Ball is moving toward our general direction
                        isPassToMe = true;
                        recentPassToMe[player.carId] = true;
                        passReceiveTime[player.carId] = elapsedTime[id];
                    }
                }
            }

            // REWARD 1: Positioning relative to incoming pass
            if (recentPassToMe[player.carId]) {
                Vec ballVel = state.ball.vel;
                if (ballVel.Length() > 200.0f) {
                    // Reward being on the same heading as ball trajectory
                    Vec ballVelNorm = ballVel.Normalized();
                    Vec playerVelNorm = player.vel.Length() > 100.0f
                        ? player.vel.Normalized()
                        : ballVelNorm;  // Default to ball direction if we're slow

                    float alignmentBonus = RS_MAX(0.0f, playerVelNorm.Dot(ballVelNorm));
                    if (alignmentBonus > 0.6f) {
                        reward += 0.5f;  // Good positioning/movement relative to pass
                    }
                }
            }

            // REWARD 2: Receiving the pass (touching ball when teammate passed it)
            if (player.ballTouchedStep && recentPassToMe[player.carId]) {
                // We touched the ball after teammate passed it toward us!
                float timeSincePass = elapsedTime[id] - passReceiveTime[player.carId];

                if (timeSincePass < 1.0f) {  // Within ~1 second of the pass
                    reward += 1.5f;  // Clean reception!

                    // BONUS: Check if we're converting to offense immediately
                    // (High ball speed relative to pass arrival speed)
                    Vec ballVel = state.ball.vel;
                    if (ballVel.Length() > 2000.0f) {
                        // We accelerated the ball significantly - offensive play!
                        reward += 2.0f;
                    }

                    // Reset the pass tracking
                    recentPassToMe[player.carId] = false;
                }
            }

            // PENALTY: Missing a pass opportunity
            // (Ball passed toward us but we didn't touch it and it's now far away)
            if (recentPassToMe[player.carId]) {
                float timeSincePass = elapsedTime[id] - passReceiveTime[player.carId];

                if (timeSincePass > 0.5f) {
                    // More than half second has passed
                    float distToBall = (player.pos - state.ball.pos).Length();

                    if (distToBall > 2000.0f) {
                        // Ball is far away and we didn't touch it - we missed the pass!
                        reward -= 0.3f;
                        recentPassToMe[player.carId] = false;
                    }
                }
            }

            return reward;
        }

        virtual void Reset(const GameState& initialState) override {
            lastBallTouchBy.clear();
            passReceiveTime.clear();
            recentPassToMe.clear();
            elapsedTime.clear();
        }
    };


    // ============================================================================
    // PASSING ACCURACY REWARD - Reward passes that reach intended receiver
    // ============================================================================
    // This reward is designed to be used alongside PassingSetupReward.
    // While PassingSetupReward rewards the INTENTION (setting up a good trajectory),
    // this reward tracks the EXECUTION (did the pass actually get to a teammate?).
    //
    // Strategy:
    // - After you touch ball, track if a teammate touches it within 2 seconds
    // - Reward based on how efficiently the pass sequence happened
    // - Bonus if pass leads to a shot
    //
    // Reward structure:
    // - +3.0: Teammate receives pass (ball goes player->teammate)
    // - +5.0: Pass leads directly to a shot (teammate scores/shoots within 0.5s)
    // - +1.0: Partial credit if teammate gets 2nd touch (possession chain)
    //
    // Rev 1.0 (2025-01-21, NJH)
    class PassingAccuracyReward : public Reward {
    private:
        mutable std::unordered_map<uint32_t, float> lastPassTime;
        mutable std::unordered_map<uint32_t, uint32_t> passedFromPlayer;  // Who initiated pass
        mutable std::unordered_map<uint32_t, float> elapsedTime;  // Track time manually

    public:
        PassingAccuracyReward() {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            // Update elapsed time
            uint32_t id = player.carId;
            elapsedTime[id] += state.deltaTime;

            float reward = 0.0f;

            // TRACK: Did we just touch the ball?
            if (player.ballTouchedStep) {
                lastPassTime[player.carId] = elapsedTime[id];
                passedFromPlayer[player.carId] = player.carId;
            }

            // CHECK: Did a teammate receive our pass?
            if (player.ballTouchedStep && passedFromPlayer[0] != player.carId) {
                // We're receiving a pass from a teammate
                uint32_t passerCarId = passedFromPlayer[0];
                float timeSincePass = elapsedTime[id] - lastPassTime[passerCarId];

                if (timeSincePass < 2.0f && timeSincePass > 0.1f) {
                    // Within ~2 second window - this is a successful pass!
                    reward += 3.0f;

                    // Check if this is a setup for a shot
                    // (Is the ball now high speed and toward goal?)
                    Vec theirGoal = (player.team == Team::BLUE)
                        ? CommonValues::ORANGE_GOAL_CENTER
                        : CommonValues::BLUE_GOAL_CENTER;

                    Vec toGoal = (theirGoal - player.pos).Normalized();
                    float goalwardSpeed = state.ball.vel.Dot(toGoal);

                    if (goalwardSpeed > 1500.0f) {
                        // Pass led to immediate offensive play!
                        reward += 1.0f;
                    }
                }
            }

            return reward;
        }

        virtual void Reset(const GameState& initialState) override {
            lastPassTime.clear();
            passedFromPlayer.clear();
            elapsedTime.clear();
        }
    };

} // namespace RLGC
