#pragma once
#include "../Framework.h"
#include "../CommonValues.h"

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

} // namespace RLGC
