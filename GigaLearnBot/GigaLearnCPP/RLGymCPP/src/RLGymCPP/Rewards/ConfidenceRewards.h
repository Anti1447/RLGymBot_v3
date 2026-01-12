#pragma once
#include "../Framework.h"
#include "../CommonValues.h"

namespace RLGC {

    // ============================================================================
    // HESITATION PUNISHMENT - Core anti-hesitation reward
    // ============================================================================
    // Punishes slowing down when approaching the ball (the classic hesitation tell)
    // Rev 1.0 (2025-01-11, NJH)
    class HesitationPunishment : public Reward {
    public:
        float minApproachSpeed;      // Min speed to not be "hesitating"
        float punishmentScale;
        float ballProximityThreshold; // Only check when this close to ball

        HesitationPunishment(
            float minSpeed = 800.0f,           // Should be approaching at least this fast
            float proximity = 1200.0f,         // When within this distance of ball
            float scale = 1.0f
        ) : minApproachSpeed(minSpeed),
            ballProximityThreshold(proximity),
            punishmentScale(scale) {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!state.prev) return 0.0f;
            if (player.isDemoed) return 0.0f;

            Vec toBall = state.ball.pos - player.pos;
            float distToBall = toBall.Length();

            // Only apply when approaching the ball
            if (distToBall > ballProximityThreshold) return 0.0f;

            // Check if we're the closer player (should be challenging)
            for (const auto& other : state.players) {
                if (other.team == player.team) continue;
                float oppDist = (state.ball.pos - other.pos).Length();
                if (oppDist < distToBall - 200.0f) {
                    // Opponent is significantly closer, we might be rotating back
                    return 0.0f;
                }
            }

            // Measure approach speed (velocity toward ball)
            Vec toBallDir = toBall.Normalized();
            float approachSpeed = player.vel.Dot(toBallDir);

            // Check for deceleration (hesitation signature)
            if (!player.prev) return 0.0f;
            Vec prevToBall = state.prev->ball.pos - player.prev->pos;
            float prevApproachSpeed = player.prev->vel.Dot(prevToBall.Normalized());

            float deceleration = prevApproachSpeed - approachSpeed;

            // Punish if:
            // 1. We're close to ball AND
            // 2. Approaching slowly OR decelerating
            float punishment = 0.0f;

            if (approachSpeed < minApproachSpeed && approachSpeed > 0) {
                // Moving toward ball but too slowly
                float speedDeficit = (minApproachSpeed - approachSpeed) / minApproachSpeed;
                punishment += speedDeficit * 0.5f;
            }

            if (deceleration > 200.0f && distToBall < 1000.0f) {
                // Significant slowdown when very close - classic hesitation
                punishment += RS_CLAMP(deceleration / 500.0f, 0.0f, 1.0f);
            }

            return -punishment * punishmentScale;
        }
    };


    // ============================================================================
    // CHALLENGE COMMITMENT REWARD
    // ============================================================================
    // Rewards committing to challenges with speed and momentum
    // Rev 1.0 (2025-01-11, NJH)
    class ChallengeCommitmentReward : public Reward {
    public:
        float minCommitSpeed;
        float optimalCommitSpeed;

        ChallengeCommitmentReward(
            float minSpeed = 1300.0f,
            float optimalSpeed = 2000.0f
        ) : minCommitSpeed(minSpeed), optimalCommitSpeed(optimalSpeed) {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) return 0.0f;

            // Reward based on speed at moment of touch
            float speedAtTouch = player.vel.Length();

            if (speedAtTouch < minCommitSpeed) {
                // Weak challenge - could be hesitation or just a soft touch
                return 0.0f;
            }

            // Scale reward by how fast we committed
            float commitFrac = RS_CLAMP(
                (speedAtTouch - minCommitSpeed) / (optimalCommitSpeed - minCommitSpeed),
                0.0f, 1.0f
            );

            // Bonus for supersonic commits
            if (speedAtTouch > CommonValues::SUPERSONIC_THRESHOLD) {
                commitFrac += 0.3f;
            }

            return commitFrac;
        }
    };


    // ============================================================================
    // FIRST TO BALL REWARD
    // ============================================================================
    // Rewards winning the race to the ball (being aggressive about ball access)
    // Rev 1.0 (2025-01-11, NJH)
    class FirstToBallReward : public Reward {
    public:
        float rewardAmount;
        int touchCooldownTicks;

        // Per-player state
        mutable std::unordered_map<uint32_t, int> lastTouchTick;

        FirstToBallReward(float reward = 1.0f, int cooldown = 60)
            : rewardAmount(reward), touchCooldownTicks(cooldown) {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) return 0.0f;

            // Check if opponent touched recently (we want to reward being FIRST)
            for (const auto& other : state.players) {
                if (other.team == player.team) continue;

                auto it = lastTouchTick.find(other.carId);
                if (it != lastTouchTick.end()) {
                    int ticksSinceOppTouch = it->second;
                    if (ticksSinceOppTouch < touchCooldownTicks) {
                        // Opponent touched recently - this isn't a "first to ball" situation
                        lastTouchTick[player.carId] = 0;
                        return 0.0f;
                    }
                }
            }

            // We got there first!
            lastTouchTick[player.carId] = 0;
            return rewardAmount;
        }

        virtual void Reset(const GameState& initialState) override {
            lastTouchTick.clear();
        }
    };


    // ============================================================================
    // IMMEDIATE SHOT REWARD
    // ============================================================================
    // Rewards taking shots quickly when opportunity arises (not hesitating)
    // Rev 1.0 (2025-01-11, NJH)
    class ImmediateShotReward : public Reward {
    public:
        float maxReactionTime;  // seconds - how fast they should shoot

        // Track when shot opportunities arise
        mutable std::unordered_map<uint32_t, float> opportunityStartTime;
        mutable std::unordered_map<uint32_t, bool> hadOpportunity;

        ImmediateShotReward(float maxReaction = 0.5f) : maxReactionTime(maxReaction) {}

        bool IsOpenNet(const Player& player, const GameState& state) const {
            // Simple open net check - opponent far from their goal
            Vec oppGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            for (const auto& other : state.players) {
                if (other.team == player.team) continue;
                if (other.isDemoed) continue;

                float distToGoal = (oppGoal - other.pos).Length();
                if (distToGoal < 2000.0f) {
                    return false;  // Opponent is defending
                }
            }

            // Also check ball is in good position
            float ballDistToGoal = (oppGoal - state.ball.pos).Length();
            return ballDistToGoal < 4000.0f;
        }

        bool IsShooting(const Player& player, const GameState& state) const {
            if (!player.ballTouchedStep) return false;

            Vec oppGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            Vec ballToGoal = (oppGoal - state.ball.pos).Normalized();
            Vec ballVelDir = state.ball.vel.Normalized();

            return ballVelDir.Dot(ballToGoal) > 0.5f && state.ball.vel.Length() > 1000.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float currentTime = state.deltaTime;  // Use deltaTime accumulation instead

            bool openNet = IsOpenNet(player, state);

            if (openNet && !hadOpportunity[player.carId]) {
                // Opportunity just started
                opportunityStartTime[player.carId] = currentTime;
                hadOpportunity[player.carId] = true;
            }
            else if (!openNet) {
                hadOpportunity[player.carId] = false;
            }

            // Check if player shot during opportunity window
            if (hadOpportunity[player.carId] && IsShooting(player, state)) {
                float reactionTime = currentTime - opportunityStartTime[player.carId];

                if (reactionTime < maxReactionTime) {
                    // Quick shot! Reward inversely proportional to reaction time
                    float quicknessFactor = 1.0f - (reactionTime / maxReactionTime);
                    hadOpportunity[player.carId] = false;  // Reset
                    return 0.5f + (quicknessFactor * 0.5f);  // 0.5 to 1.0
                }
            }

            return 0.0f;
        }

        virtual void Reset(const GameState& initialState) override {
            opportunityStartTime.clear();
            hadOpportunity.clear();
        }
    };


    // ============================================================================
    // CHALLENGE VS SHADOW BALANCE
    // ============================================================================
    // Punishes excessive shadowing when a challenge would be appropriate
    // Rev 1.0 (2025-01-11, NJH)
    class ExcessiveShadowPunishment : public Reward {
    public:
        float maxShadowTime;      // Max seconds to shadow before challenging
        float shadowDistanceMax;  // Distance that counts as "shadowing"

        mutable std::unordered_map<uint32_t, float> shadowStartTime;
        mutable std::unordered_map<uint32_t, bool> isShadowing;

        ExcessiveShadowPunishment(float maxTime = 3.0f, float shadowDist = 2500.0f)
            : maxShadowTime(maxTime), shadowDistanceMax(shadowDist) {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float currentTime = state.deltaTime;  // Use deltaTime

            // Detect shadow defense: between ball and own goal, moving backward
            Vec ownGoal = (player.team == Team::BLUE)
                ? CommonValues::BLUE_GOAL_CENTER
                : CommonValues::ORANGE_GOAL_CENTER;

            Vec toBall = state.ball.pos - player.pos;
            Vec toGoal = ownGoal - player.pos;

            float distToBall = toBall.Length();
            bool isBetweenBallAndGoal = toGoal.Dot(toBall) < 0;
            bool movingTowardOwnGoal = player.vel.Dot(toGoal.Normalized()) > 0;

            bool currentlyShadowing = isBetweenBallAndGoal &&
                movingTowardOwnGoal &&
                distToBall < shadowDistanceMax &&
                distToBall > 500.0f;  // Not actively challenging

            // Check if opponent has possession (when shadowing is valid)
            bool oppHasPossession = false;
            for (const auto& other : state.players) {
                if (other.team == player.team) continue;
                float oppBallDist = (state.ball.pos - other.pos).Length();
                if (oppBallDist < 300.0f) {
                    oppHasPossession = true;
                    break;
                }
            }

            if (currentlyShadowing && oppHasPossession) {
                if (!isShadowing[player.carId]) {
                    shadowStartTime[player.carId] = currentTime;
                    isShadowing[player.carId] = true;
                }

                float shadowDuration = currentTime - shadowStartTime[player.carId];

                if (shadowDuration > maxShadowTime) {
                    // Been shadowing too long - should challenge
                    float excessTime = shadowDuration - maxShadowTime;
                    return -RS_CLAMP(excessTime / 2.0f, 0.0f, 1.0f);
                }
            }
            else {
                isShadowing[player.carId] = false;
            }

            return 0.0f;
        }

        virtual void Reset(const GameState& initialState) override {
            shadowStartTime.clear();
            isShadowing.clear();
        }
    };


    // ============================================================================
    // AERIAL COMMITMENT REWARD
    // ============================================================================
    // Rewards going for aerials immediately instead of waiting for ball to drop
    // Rev 1.0 (2025-01-11, NJH)
    class AerialCommitmentReward : public Reward {
    public:
        float minBallHeight;      // Ball must be this high
        float rewardPerTouch;

        AerialCommitmentReward(float minHeight = 400.0f, float reward = 1.0f)
            : minBallHeight(minHeight), rewardPerTouch(reward) {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) return 0.0f;
            if (player.isOnGround) return 0.0f;  // Must be aerial

            float ballHeight = state.ball.pos.z;
            if (ballHeight < minBallHeight) return 0.0f;

            // Reward more for higher aerials (went for it instead of waiting)
            float heightFactor = RS_CLAMP((ballHeight - minBallHeight) / 800.0f, 0.0f, 1.0f);

            // Extra reward if ball was going to bounce soon (we committed early)
            bool ballFalling = state.ball.vel.z < -100.0f;
            float commitBonus = ballFalling ? 0.3f : 0.0f;

            return (heightFactor + commitBonus) * rewardPerTouch;
        }
    };


    // ============================================================================
    // FORWARD MOMENTUM MAINTENANCE
    // ============================================================================
    // Rewards maintaining speed/momentum through plays (not stop-start)
    // Rev 1.0 (2025-01-11, NJH)
    class MomentumMaintenanceReward : public Reward {
    public:
        float targetSpeed;
        float sampleInterval;  // How often to sample (seconds)

        mutable std::unordered_map<uint32_t, float> lastSampleTime;
        mutable std::unordered_map<uint32_t, float> avgSpeed;
        mutable std::unordered_map<uint32_t, int> sampleCount;

        MomentumMaintenanceReward(float target = 1600.0f, float interval = 0.25f)
            : targetSpeed(target), sampleInterval(interval) {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;

            float currentTime = state.deltaTime;  // Use deltaTime

            // Sample at intervals to avoid noise
            if (currentTime - lastSampleTime[player.carId] < sampleInterval) {
                return 0.0f;
            }
            lastSampleTime[player.carId] = currentTime;

            float speed = player.vel.Length();

            // Update running average
            sampleCount[player.carId]++;
            float n = (float)sampleCount[player.carId];
            avgSpeed[player.carId] = avgSpeed[player.carId] * ((n - 1) / n) + speed * (1 / n);

            // Small continuous reward for maintaining good average speed
            float speedRatio = RS_CLAMP(avgSpeed[player.carId] / targetSpeed, 0.0f, 1.2f);

            // Reward is small but continuous
            return (speedRatio - 0.5f) * 0.1f;  // -0.05 to +0.07 per sample
        }

        virtual void Reset(const GameState& initialState) override {
            lastSampleTime.clear();
            avgSpeed.clear();
            sampleCount.clear();
        }
    };


    // ============================================================================
    // FIFTY-FIFTY AGGRESSION REWARD
    // ============================================================================
    // Rewards attacking 50/50s with speed rather than arriving passively
    // Rev 1.0 (2025-01-11, NJH)
    class FiftyAggressionReward : public Reward {
    public:
        float fiftyProximity;
        float minAggressiveSpeed;

        FiftyAggressionReward(float proximity = 500.0f, float minSpeed = 1400.0f)
            : fiftyProximity(proximity), minAggressiveSpeed(minSpeed) {
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.ballTouchedStep) return 0.0f;

            // Check if this was a 50/50 (opponent also close to ball)
            bool wasFifty = false;
            for (const auto& other : state.players) {
                if (other.team == player.team) continue;

                float oppDist = (state.ball.pos - other.pos).Length();
                if (oppDist < fiftyProximity) {
                    wasFifty = true;
                    break;
                }
            }

            if (!wasFifty) return 0.0f;

            // Reward based on our speed going into the 50
            float ourSpeed = player.vel.Length();

            if (ourSpeed >= minAggressiveSpeed) {
                float aggressionFactor = RS_CLAMP(
                    (ourSpeed - minAggressiveSpeed) / 800.0f,
                    0.0f, 1.0f
                );
                return 0.5f + (aggressionFactor * 0.5f);
            }

            // Arrived slowly - slight punishment
            return -0.2f;
        }
    };

} // namespace RLGC