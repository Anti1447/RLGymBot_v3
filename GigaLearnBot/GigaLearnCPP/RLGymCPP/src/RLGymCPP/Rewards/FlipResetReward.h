#pragma once
// ============================================================================
// FlipResetReward.h - FLIP RESET SYSTEM
//
// Rev 1.0 - 2025-12-28 (NJH)
//
// Three-layer system:
//   1) FlipResetValidator - Validates flip reset mechanics (carState checks)
//   2) ProductiveUseValidator - Classifies post-flip outcome (shot, clear, delayed)
//   3) FlipResetReward - Main reward calculator (inherits Reward)
//
// State machine tracks:
//   - Flip reset acquisition (was it valid?)
//   - Productive use window (what did we do with it?)
//   - Outcome scoring (full reward vs partial)
// ============================================================================

#include <map>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace RLGC {

    // ============================================================================
    // FLIP RESET SPECIFIC CONSTANTS (non-duplicates only)
    // ============================================================================

    constexpr float CAR_BALL_CONTACT_DISTANCE = 200.0f;

    // Note: NORMAL_PARALLELISM_THRESHOLD, NORMAL_REJECTION_THRESHOLD, and MIN_AIRTIME_FOR_FLIP
    // are defined in OutplayRewards.h to avoid duplication

    // ============================================================================
    // FLIP RESET VALIDATION (Core mechanics check)
    // ============================================================================

    class FlipResetValidator {
    public:
        /**
         * REQUIREMENT #1 & #2: Validate flip reset occurred with proper mechanics
         *
         * Checks:
         * - Car was airborne AND ball is airborne
         * - All 4 wheels contacted ball (via normal vector parallelism)
         * - Not a hood contact (normals would be opposite)
         * - Jump capability restored (flip reset flag)
         * - Reasonable airtime (not instant)
         */
        static bool IsValidFlipReset(
            bool car_was_airborne,
            bool ball_is_airborne,
            bool all_wheels_had_contact,
            float car_normal_dot_ball_normal,
            bool jump_capability_restored,
            float time_in_air_before_contact
        ) {
            // Both must be airborne
            if (!car_was_airborne || !ball_is_airborne) {
                return false;
            }

            // Normal vectors must be parallel (same direction)
            if (car_normal_dot_ball_normal < NORMAL_PARALLELISM_THRESHOLD) {
                return false;
            }

            // Reject hood contact (opposite normals)
            if (car_normal_dot_ball_normal < NORMAL_REJECTION_THRESHOLD) {
                return false;
            }

            // All wheels must have contacted
            if (!all_wheels_had_contact) {
                return false;
            }

            // Jump capability must be restored
            if (!jump_capability_restored) {
                return false;
            }

            // Reasonable airtime
            if (time_in_air_before_contact < MIN_AIRTIME_FOR_FLIP) {
                return false;
            }

            return true;
        }

        /**
         * Calculate normal vector dot product for validation
         */
        static float CalculateNormalParallelism(const Vec& car_normal_up, const Vec& ball_normal_at_contact) {
            return DotN(car_normal_up, ball_normal_at_contact);
        }

        /**
         * Check if normals are parallel (valid wheel contact)
         */
        static bool AreNormalsParallel(float dot_product) {
            return dot_product > NORMAL_PARALLELISM_THRESHOLD;
        }

        /**
         * Check if this is hood contact (reject)
         */
        static bool IsHoodContact(float dot_product) {
            return dot_product < NORMAL_REJECTION_THRESHOLD;
        }
    };

    // ============================================================================
    // PRODUCTIVE USE VALIDATION (Flip reset utility check)
    // ============================================================================

    class ProductiveUseValidator {
    public:
        enum UsageType {
            INVALID = 0,
            OFFENSIVE_SHOT = 1,       // Shooting on opponent goal
            DEFENSIVE_CLEAR = 2,      // Clearing away from own goal
            DELAYED_SHOT = 3,         // Shot that occurs > 1s after flip reset
            NEUTRAL_REDIRECT = 4      // Ball deflection in neutral zone
        };

        /**
         * REQUIREMENT #3: Detect productive use of flip reset
         */
        static UsageType ClassifyUsage(
            const Vec& ball_pos_at_flip,
            const Vec& ball_pos_after_action,
            const Vec& ball_vel_after_action,
            int team_id,
            float time_since_flip_reset
        ) {
            // Determine goal directions
            float own_goal_y = (team_id == 0) ? OWN_GOAL_Y : GOAL_LINE_Y;
            float opponent_goal_y = (team_id == 0) ? GOAL_LINE_Y : OWN_GOAL_Y;

            // Check if ball moving toward opponent goal
            bool moving_toward_opponent = (opponent_goal_y > 0) ?
                (ball_vel_after_action.y > 100.0f) :
                (ball_vel_after_action.y < -100.0f);

            bool moving_toward_own = (own_goal_y > 0) ?
                (ball_vel_after_action.y > 100.0f) :
                (ball_vel_after_action.y < -100.0f);

            // Determine zone
            float distance_to_own = fabs(ball_pos_after_action.y - own_goal_y);
            float distance_to_opponent = fabs(ball_pos_after_action.y - opponent_goal_y);

            // Check for delayed action (>1s after flip reset)
            bool is_delayed = time_since_flip_reset > 1.0f;

            // Classify usage
            if (distance_to_opponent < distance_to_own) {
                if (moving_toward_opponent) {
                    return is_delayed ? DELAYED_SHOT : OFFENSIVE_SHOT;
                }
            }

            if (distance_to_own < distance_to_opponent) {
                if (moving_toward_own && moving_toward_opponent) {
                    return NEUTRAL_REDIRECT;
                }
                if (!moving_toward_own) {
                    return DEFENSIVE_CLEAR;
                }
            }

            // Midfield
            if (moving_toward_opponent) {
                return is_delayed ? DELAYED_SHOT : OFFENSIVE_SHOT;
            }

            return INVALID;
        }

        /**
         * Check if usage is productive
         */
        static bool IsProductive(UsageType usage) {
            return usage != INVALID;
        }
    };

    // ============================================================================
    // FLIP RESET REWARD
    // ============================================================================

    class FlipResetReward : public Reward {
    public:
        struct Config {
            // Acquisition phase rewards (incremental)
            float underside_contact_reward = 0.1f;         // Small reward for any underside contact while airborne
            float flip_reset_trigger_reward = 0.4f;        // Reward for actually getting flip reset
            float wheel_contact_bonus = 0.15f;             // Bonus for good normal alignment

            // Flip usage rewards (NEW - reward for using the flip)
            float flip_used_reward = 0.3f;                 // Immediate reward for using the flip reset
            float flip_toward_ball_bonus = 0.2f;           // Bonus if flipping toward ball

            // Productive use rewards
            float offensive_shot_reward = 1.0f;
            float offensive_shot_wide_reward = 0.5f;
            float defensive_clear_reward = 0.8f;
            float delayed_shot_bonus = 0.2f;
            float neutral_redirect_reward = 0.3f;

            // Penalties (reduced to encourage experimentation)
            float invalid_usage_penalty = -0.05f;          // Reduced penalty
        };

        Config config;

    private:
        struct PendingFlipReset {
            float timeLeft = 0.0f;
            bool wasValidFlipReset = false;
            bool flipWasUsed = false;                      // NEW: Track if flip was actually used
            float normalParallelism = 0.0f;
            Vec ballPosAtFlip;
            Vec ballVelAtFlip;
            ProductiveUseValidator::UsageType usageType = ProductiveUseValidator::INVALID;
        };

        mutable std::map<uint32_t, PendingFlipReset> pending;
        mutable std::map<uint32_t, bool> previouslyFlipping;  // Track flipping state

        /**
         * Calculate reward for flip reset acquisition
         */
        float CalculateFlipResetAcquisitionReward(
            bool is_valid_flip_reset,
            float normal_parallelism_dot
        ) const {
            if (!is_valid_flip_reset) {
                return 0.0f;
            }

            float reward = config.flip_reset_trigger_reward;

            // Bonus for perfect normal alignment (>0.95)
            if (normal_parallelism_dot > 0.95f) {
                reward += config.wheel_contact_bonus;
            }

            return reward;
        }

        /**
         * Calculate reward for productive use
         */
        float CalculateProductiveUseReward(
            ProductiveUseValidator::UsageType usage_type,
            const Vec& ball_pos,
            const Vec& goal_center,
            int team_id
        ) const {
            switch (usage_type) {
            case ProductiveUseValidator::OFFENSIVE_SHOT: {
                float x_distance = fabs(ball_pos.x - goal_center.x);
                if (x_distance < NET_WIDTH / 2.0f) {
                    return config.offensive_shot_reward;
                }
                else if (x_distance < NET_WIDTH / 2.0f + 300.0f) {
                    return config.offensive_shot_wide_reward;
                }
                return config.invalid_usage_penalty;
            }

            case ProductiveUseValidator::DELAYED_SHOT: {
                float x_distance = fabs(ball_pos.x - goal_center.x);
                if (x_distance < NET_WIDTH / 2.0f) {
                    return config.offensive_shot_reward + config.delayed_shot_bonus;
                }
                else if (x_distance < NET_WIDTH / 2.0f + 300.0f) {
                    return config.offensive_shot_wide_reward + config.delayed_shot_bonus;
                }
                return config.invalid_usage_penalty;
            }

            case ProductiveUseValidator::DEFENSIVE_CLEAR: {
                return config.defensive_clear_reward;
            }

            case ProductiveUseValidator::NEUTRAL_REDIRECT: {
                return config.neutral_redirect_reward;
            }

            case ProductiveUseValidator::INVALID:
            default: {
                return config.invalid_usage_penalty;
            }
            }
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            pending.clear();
            previouslyFlipping.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Don't reward flip resets during kickoff
            bool isKickoff = (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 200.0f);
            if (isKickoff) {
                return 0.0f;
            }

            // Require minimum height (no ground-level flip resets)
            if (player.pos.z < 300.0f) {
                return 0.0f;
            }

            uint32_t id = player.carId;
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            float totalReward = 0.0f;

            // ====================================================================
            // PHASE 2: Track pending flip reset and detect flip usage
            // ====================================================================

            auto pit = pending.find(id);
            if (pit != pending.end()) {
                PendingFlipReset& pfr = pit->second;

                // Detect if player just started flipping (NEW)
                bool wasFlipping = previouslyFlipping[id];
                bool isFlippingNow = player.isFlipping;

                if (!wasFlipping && isFlippingNow && !pfr.flipWasUsed) {
                    // Player just started using their flip reset!
                    pfr.flipWasUsed = true;
                    totalReward += config.flip_used_reward;

                    // Bonus if flipping toward ball
                    Vec toBall = (state.ball.pos - player.pos).Normalized();
                    float flipAlignment = player.rotMat.forward.Dot(toBall);
                    if (flipAlignment > 0.5f) {
                        totalReward += config.flip_toward_ball_bonus;
                    }
                }

                previouslyFlipping[id] = isFlippingNow;

                pfr.timeLeft -= dt;

                if (pfr.timeLeft <= 0.0f) {
                    // Evaluate productive use after flip reset window (reduced from 5s to 2s)
                    if (!pfr.wasValidFlipReset) {
                        pending.erase(pit);
                        return totalReward;
                    }

                    // Award productive use bonus (only if flip was used)
                    if (pfr.flipWasUsed) {
                        Vec oppGoal = (player.team == Team::BLUE) ? Vec(0.0f, GOAL_LINE_Y, GOAL_HEIGHT * 0.5f)
                            : Vec(0.0f, -GOAL_LINE_Y, GOAL_HEIGHT * 0.5f);

                        float usage_reward = CalculateProductiveUseReward(
                            pfr.usageType,
                            state.ball.pos,
                            oppGoal,
                            (player.team == Team::BLUE) ? 0 : 1
                        );

                        totalReward += usage_reward;
                    }

                    pending.erase(pit);
                    return totalReward;
                }

                // Update usage classification as we wait
                float timeSinceFlip = 2.0f - pfr.timeLeft;  // Reduced window from 5s to 2s
                pfr.usageType = ProductiveUseValidator::ClassifyUsage(
                    pfr.ballPosAtFlip,
                    state.ball.pos,
                    state.ball.vel,
                    (player.team == Team::BLUE) ? 0 : 1,
                    timeSinceFlip
                );

                return totalReward;
            }

            // ====================================================================
            // PHASE 1: Detect flip reset acquisition
            // ====================================================================

            // Check if we just got a flip reset this frame
            const Player* prevPlayer = player.prev;

            if (prevPlayer) {
                // Detect flip reset acquisition:
                // - Previous frame: we had no flip reset (hasFlipReset = false)
                // - Current frame: we have flip reset (HasFlipReset() = true)
                bool prevHadFlipReset = prevPlayer->HasFlipReset();
                bool nowHasFlipReset = player.HasFlipReset();

                if (!prevHadFlipReset && nowHasFlipReset) {
                    // We just got a flip reset! Give incremental rewards.

                    // REQUIREMENT 1: Both car and ball must be airborne
                    bool carWasAirborne = !prevPlayer->isOnGround;
                    bool ballIsAirborne = state.ball.pos.z > BALL_RADIUS;

                    // Give small reward for ANY underside contact while airborne
                    if (carWasAirborne && prevPlayer->ballTouchedStep) {
                        totalReward += config.underside_contact_reward;
                    }

                    // REQUIREMENT 2: Check wheel contact (RELAXED - at least 2 wheels instead of all 4)
                    int wheelsContacted = 0;
                    for (int i = 0; i < 4; i++) {
                        if (prevPlayer->wheelsWithContact[i]) {
                            wheelsContacted++;
                        }
                    }
                    bool goodWheelContact = wheelsContacted >= 2;  // RELAXED: 2+ wheels instead of all 4

                    // Calculate normal vector parallelism
                    float normalParallelism = 0.0f;
                    if (prevPlayer->ballHitInfo.isValid) {
                        Vec ballContactNormal = prevPlayer->ballHitInfo.relativePosOnBall.Normalized();
                        normalParallelism = FlipResetValidator::CalculateNormalParallelism(
                            player.rotMat.up,
                            ballContactNormal
                        );
                    }

                    // REQUIREMENT 3: Jump capability must be restored
                    bool jumpCapabilityRestored = player.HasFlipReset();

                    // REQUIREMENT 4: Reasonable airtime before contact
                    float airTimeBeforeContact = prevPlayer->airTime;

                    // Validate the flip reset (RELAXED validation)
                    bool isValidFlipReset = (
                        carWasAirborne &&
                        ballIsAirborne &&
                        goodWheelContact &&  // 2+ wheels instead of all 4
                        normalParallelism > 0.5f &&  // RELAXED: 0.5 instead of 0.7
                        jumpCapabilityRestored &&
                        airTimeBeforeContact > MIN_AIRTIME_FOR_FLIP
                    );

                    if (isValidFlipReset) {
                        // Give immediate acquisition reward
                        totalReward += config.flip_reset_trigger_reward;

                        // Bonus for excellent normal alignment
                        if (normalParallelism > 0.85f) {
                            totalReward += config.wheel_contact_bonus;
                        }

                        // ARM PENDING FLIP RESET
                        PendingFlipReset pfr;
                        pfr.timeLeft = 2.0f;  // 2 second window (reduced from 5s)
                        pfr.wasValidFlipReset = true;
                        pfr.flipWasUsed = false;
                        pfr.normalParallelism = normalParallelism;
                        pfr.ballPosAtFlip = state.ball.pos;
                        pfr.ballVelAtFlip = state.ball.vel;
                        pfr.usageType = ProductiveUseValidator::INVALID;

                        pending[id] = pfr;
                        previouslyFlipping[id] = player.isFlipping;
                    }
                }
            }

            return totalReward;
        }
    };

} // namespace RLGC