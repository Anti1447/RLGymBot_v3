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
#include <unordered_map>
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
    // FLIP RESET REWARD — Rev 2.1 (2026-02-22, NJH)
    //
    // Acquisition-first design. Rewards the bot for obtaining a flip reset
    // while airborne above Z=300 (no ground-level or trivially-low resets).
    //
    // Reward structure:
    //   - One-shot acquisition: BASE_RESET_REWARD (1.0) + height bonus (0–0.5)
    //     Height bonus is linear from 0 at Z=300 to +0.5 at Z>=1000.
    //   - Per-frame aerial approach shaping: small reward for being airborne
    //     within APPROACH_DIST_MAX (500 UU) of the ball.
    //     This encourages the bot to fly toward the ball for reset attempts.
    //
    // Rev 2.1 changes:
    //  - Apex timing multiplier: acquisition reward scaled by 1.0x–2.0x based on
    //    ball vel.z at acquisition moment. Optimal: vel.z in [-300, 0] UU/s → 2.0x.
    //    Falls linearly to 1.0x at vel.z = +300 (rising) or vel.z = -600 (falling).
    //    BallPrediction infrastructure was considered but state.ball.vel.z is read
    //    directly — it's current-frame data, not a future prediction.
    //  - Handbrake bonus: +0.3 additive if handbrakeVal > 0.1 at acquisition moment.
    //  - Boost-off bonus: +0.25 additive if !isHoldingBoost at acquisition moment.
    //  Max acquisition reward: (1.0 + 0.5) * 2.0 + 0.3 + 0.25 = 3.55 (perfect reset)
    //
    // Validation gates (all must pass):
    //   1. player.pos.z >= MIN_RESET_HEIGHT (300 UU)
    //   2. state.ball.pos.z > BALL_RADIUS (ball off the ground)
    //   3. player.airTime >= MIN_AIRTIME_FOR_FLIP (0.1s)
    //   4. At least MIN_WHEEL_CONTACT (2) wheels had contact
    //   5. Not a kickoff
    //
    // Note: HasFlipReset() from RocketSim already validates the physics
    // (wheels-on-ball → flip restored). The gates above filter training edge
    // cases only. We do NOT re-check normal parallelism — the engine is the
    // ground truth for whether a valid flip reset occurred.
    //
    // Architecture: PreStep() detects the !HasFlipReset()→HasFlipReset()
    // transition and queues the reward into _pendingReward[id]. GetReward()
    // pays it out. This avoids mutable state in GetReward() and ensures the
    // transition is captured exactly once per step.
    //
    // TODO (future phase): Re-enable ProductiveUseValidator to reward using
    // the flip reset for shots/clears after the bot reliably acquires them.
    // ============================================================================

    class FlipResetReward : public Reward {
    private:
        // ----------------------------------------------------------------
        // Constants
        // ----------------------------------------------------------------
        static constexpr float MIN_RESET_HEIGHT   = 300.0f;   // min Z for valid reset
        static constexpr float HEIGHT_BONUS_MAX_Z = 1000.0f;  // Z where height bonus caps
        static constexpr float BASE_RESET_REWARD  = 1.0f;     // one-shot acquisition reward
        static constexpr float MAX_HEIGHT_BONUS   = 0.5f;     // added for Z >= HEIGHT_BONUS_MAX_Z
        static constexpr float APPROACH_DIST_MAX  = 500.0f;   // UU: aerial proximity shaping range
        static constexpr float APPROACH_REWARD    = 0.02f;    // per-frame aerial proximity reward
        static constexpr int   MIN_WHEEL_CONTACT  = 2;        // min wheels touched for valid reset

        // Apex timing multiplier — rewards resets when ball is near its apex
        // Optimal window: ball vel.z in [-300, 0] UU/s → 2.0x multiplier
        // Outside ±300 UU/s of the window → linearly falls to 1.0x
        static constexpr float APEX_MULT_MAX      = 2.0f;    // multiplier inside optimal window
        static constexpr float APEX_MULT_MIN      = 1.0f;    // multiplier at ±300 from window edges
        static constexpr float APEX_WINDOW_LOW    = -300.0f; // optimal vel.z lower bound (UU/s)
        static constexpr float APEX_WINDOW_HIGH   = 0.0f;    // optimal vel.z upper bound (UU/s)
        static constexpr float APEX_FALLOFF_RANGE = 300.0f;  // UU/s over which mult falls 2.0→1.0

        // Handbrake bonus — binary: rewarded when handbrake is active at acquisition
        // handbrakeVal threshold matches pattern in RecoveryRewards.h
        static constexpr float HANDBRAKE_BONUS     = 0.3f;   // additive bonus
        static constexpr float HANDBRAKE_THRESHOLD = 0.1f;   // analog threshold for "active"

        // Boost-off bonus — binary: rewarded when NOT boosting at acquisition moment
        // Uses prevAction.boost < 0.5f (0=off, 1=on binary action)
        // Encourages saving boost for the follow-up shot after the reset
        static constexpr float BOOST_OFF_BONUS     = 0.25f;  // additive bonus

        // ----------------------------------------------------------------
        // State
        // ----------------------------------------------------------------
        std::unordered_map<uint32_t, float> _pendingReward;  // queued in PreStep, paid in GetReward

        // ----------------------------------------------------------------
        // Helper: apex timing multiplier
        // Returns 2.0x when ball vel.z is in the optimal descent window
        // [APEX_WINDOW_LOW, APEX_WINDOW_HIGH], falling linearly to 1.0x
        // at APEX_FALLOFF_RANGE outside either edge.
        // ----------------------------------------------------------------
        float ComputeApexMultiplier(float ballVelZ) const {
            if (ballVelZ >= APEX_WINDOW_LOW && ballVelZ <= APEX_WINDOW_HIGH) {
                return APEX_MULT_MAX;  // inside optimal window [−300, 0] UU/s
            }
            if (ballVelZ > APEX_WINDOW_HIGH) {
                // Ball still rising — scale from 2.0x at 0 down to 1.0x at +300
                float excess = ballVelZ - APEX_WINDOW_HIGH;
                float t = RS_CLAMP(excess / APEX_FALLOFF_RANGE, 0.0f, 1.0f);
                return APEX_MULT_MAX - (APEX_MULT_MAX - APEX_MULT_MIN) * t;
            }
            // Ball falling below optimal — scale from 2.0x at -300 down to 1.0x at -600
            float excess = (-ballVelZ) - (-APEX_WINDOW_LOW);
            float t = RS_CLAMP(excess / APEX_FALLOFF_RANGE, 0.0f, 1.0f);
            return APEX_MULT_MAX - (APEX_MULT_MAX - APEX_MULT_MIN) * t;
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            _pendingReward.clear();
        }

        // ----------------------------------------------------------------
        // PreStep: detect flip reset acquisition transition for all players
        // ----------------------------------------------------------------
        virtual void PreStep(const GameState& state) override {
            for (const auto& player : state.players) {
                uint32_t id = player.carId;
                _pendingReward[id] = 0.0f;

                if (player.prev == nullptr) continue;

                // Detect !HasFlipReset() → HasFlipReset() transition while airborne
                bool prevHad = player.prev->HasFlipReset();
                bool nowHas  = player.HasFlipReset();

                if (!prevHad && nowHas && !player.isOnGround) {
                    // Gate 1: minimum height
                    if (player.pos.z < MIN_RESET_HEIGHT) continue;

                    // Gate 2: ball must be airborne (not resting on ground)
                    if (state.ball.pos.z <= BALL_RADIUS) continue;

                    // Gate 3: car must have been airborne long enough (no instant resets)
                    if (player.airTime < MIN_AIRTIME_FOR_FLIP) continue;

                    // Gate 4: at least 2 wheels contacted the ball
                    int wheels = 0;
                    for (int i = 0; i < 4; i++) {
                        if (player.wheelsWithContact[i]) wheels++;
                    }
                    if (wheels < MIN_WHEEL_CONTACT) continue;

                    // Gate 5: not a kickoff
                    bool isKickoff = (state.ball.pos.Length() < 300.0f
                                   && state.ball.vel.Length() < 200.0f);
                    if (isKickoff) continue;

                    // All gates passed — compute acquisition reward
                    float reward = BASE_RESET_REWARD;

                    // Height bonus: 0 at Z=300, +MAX_HEIGHT_BONUS at Z>=1000
                    float heightFactor = RS_CLAMP(
                        (player.pos.z - MIN_RESET_HEIGHT) / (HEIGHT_BONUS_MAX_Z - MIN_RESET_HEIGHT),
                        0.0f, 1.0f
                    );
                    reward += MAX_HEIGHT_BONUS * heightFactor;

                    // Apex timing multiplier: 2.0x when ball vel.z in [-300, 0] UU/s (just past apex),
                    // linearly falling to 1.0x at +300 UU/s (rising) or -600 UU/s (falling fast).
                    // Reads state.ball.vel.z directly — current-frame data, no prediction needed.
                    float apexMult = ComputeApexMultiplier(state.ball.vel.z);
                    reward *= apexMult;

                    // Handbrake bonus: flat +0.3 when handbrake active at acquisition moment.
                    // Teaches the bot to hold handbrake for controlled wheel-on-ball contact.
                    if (player.handbrakeVal > HANDBRAKE_THRESHOLD) {
                        reward += HANDBRAKE_BONUS;
                    }

                    // Boost-off bonus: flat +0.25 when NOT boosting at acquisition moment.
                    // Encourages saving boost for the follow-up shot after the reset.
                    // prevAction.boost is 0 (off) or 1 (on) from the last action step.
                    if (player.prevAction.boost < 0.5f) {
                        reward += BOOST_OFF_BONUS;
                    }

                    _pendingReward[id] = reward;
                }
            }
        }

        // ----------------------------------------------------------------
        // GetReward: pay out queued acquisition + aerial approach shaping
        // ----------------------------------------------------------------
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;
            float reward = 0.0f;

            // Pay out queued acquisition reward (set in PreStep, consumed here)
            auto it = _pendingReward.find(id);
            if (it != _pendingReward.end() && it->second != 0.0f) {
                reward += it->second;
                it->second = 0.0f;
            }

            // Aerial approach shaping: small per-frame reward for being airborne
            // close to the ball above the height threshold.
            // Teaches the bot to seek the ball for flip reset attempts.
            if (!player.isOnGround && player.pos.z >= MIN_RESET_HEIGHT) {
                float distToBall = (player.pos - state.ball.pos).Length();
                if (distToBall < APPROACH_DIST_MAX) {
                    float proximityFactor = 1.0f - (distToBall / APPROACH_DIST_MAX);
                    reward += APPROACH_REWARD * proximityFactor;
                }
            }

            return reward;
        }
    };

    // ============================================================================
    // SIMPLE FLIP RESET REWARD — Rev 1.0 (2026-02-23, NJH)
    //
    // Bare-minimum flip reset reward. No gates, no bonuses, no shaping.
    // Detects the !HasFlipReset() → HasFlipReset() transition while airborne
    // and returns a flat 1.0 reward. The engine (RocketSim) is the sole arbiter
    // of whether a flip reset occurred — no additional validation applied.
    //
    // Use case: earlier curriculum phases, baseline comparison, or any context
    // where the full gated FlipResetReward (Rev 2.1) is too restrictive.
    // ============================================================================
    // ============================================================================
    // SimpleFlipResetReward - IMPROVED VERSION
    //
    // Detects flip reset acquisitions with ball position validation to avoid
    // rewarding "pancake" shots (ball rebounding off walls).
    //
    // Conditions for reward:
    //   1. Transition from no flip to flip while airborne
    //   2. Ball is 400+ UU off ground (not on court)
    //   3. Ball is 400+ UU away from walls/backboards (not bouncing off walls)
    //
    // This prevents farming flip resets from low-quality ball positions like:
    // - Wall bounces (pancake shots)
    // - Ground bounces
    // - Rebounds near corners
    //
    // Only awards once at the moment all conditions are met simultaneously.
    // ============================================================================
    class SimpleFlipResetReward : public Reward {
    public:
        struct Config {
            float flip_reset_reward = 1.0f;        // Reward for getting flip reset with good ball positioning

            // Thresholds
            float min_ball_height = 400.0f;        // Ball must be this high (uu above ground)
            float min_distance_from_walls = 400.0f; // Distance from nearest wall/backboard (uu)
        };
    private:
        Config config;
        std::map<uint32_t, bool> previouslyHadFlipReset;
    public:
        explicit SimpleFlipResetReward(const Config& cfg = Config{}) : config(cfg) {}

        /**
         * Calculate minimum distance from ball to any wall or backboard.
         * Field bounds:
         *   - Side walls: x = ±4096
         *   - Back walls: y = ±5120
         *   - Ceiling: z = 2044
         */
        static float DistanceToNearestWall(const Vec& ballPos) {
            const float SIDE_WALL_X = 4096.0f;
            const float BACK_WALL_Y = 5120.0f;

            // Distance to side walls (x = ±4096)
            float distToSideWall = std::abs(SIDE_WALL_X - std::abs(ballPos.x));

            // Distance to back walls (y = ±5120)
            float distToBackWall = std::abs(BACK_WALL_Y - std::abs(ballPos.y));

            // Return minimum of the two
            return std::min(distToSideWall, distToBackWall);
        }

        virtual void Reset(const GameState& initialState) override {
            previouslyHadFlipReset.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;
            float totalReward = 0.0f;

            // Track flip reset state change
            bool prevHadFlipReset = previouslyHadFlipReset[id];
            bool nowHasFlipReset = player.HasFlipReset();

            // Detect flip reset acquisition (transition from no flip to flip)
            if (!prevHadFlipReset && nowHasFlipReset) {
                // Check condition 1: Ball is 400+ uu off the ground
                bool ballHighEnough = state.ball.pos.z >= config.min_ball_height;

                // Check condition 2: Ball is 400+ uu away from walls
                float distToWall = DistanceToNearestWall(state.ball.pos);
                bool ballAwayFromWalls = distToWall >= config.min_distance_from_walls;

                // Award only if BOTH conditions are met
                if (ballHighEnough && ballAwayFromWalls) {
                    totalReward = config.flip_reset_reward;
                }
            }

            // Update state for next frame
            previouslyHadFlipReset[id] = nowHasFlipReset;

            return totalReward;
        }
    };

    // ============================================================================
    // AIR ROLL REWARD — Rev 1.0 (2026-02-23, NJH)
    //
    // Per-frame reward for actively using air roll while airborne.
    // Encourages the bot to develop aerial rotation habit — prerequisite for
    // flip resets, aerial redirects, and musty flicks.
    //
    // Detection: player is airborne AND |prevAction.roll| > ROLL_THRESHOLD.
    // Roll action range is [-1, 1]; threshold 0.5 requires a committed roll
    // input rather than incidental analog drift from the network.
    //
    // Framerate-independent: REWARD_PER_SECOND scaled by deltaTime each step.
    // No PreStep needed — fully stateless, read-only in GetReward.
    //
    // Constants:
    //   ROLL_THRESHOLD    = 0.5f  — min |roll| considered intentional
    //   REWARD_PER_SECOND = 1.0f  — reward rate while actively rolling
    //
    // Typical exposure: at 120 ticks/s with 8-tick skip → ~15 steps/s.
    // Continuous rolling yields 1.0 * (8/120) ≈ 0.067 per step, ~1.0/s total.
    // At weight 5 in training: ~5.0 reward units/s for sustained air rolling.
    // ============================================================================
    class AirRollReward : public Reward {
    private:
        static constexpr float ROLL_THRESHOLD    = 0.5f;
        static constexpr float REWARD_PER_SECOND = 1.0f;

    public:
        virtual float GetReward(const Player& player, const GameState& state, bool) override {
            // Must be airborne
            if (player.isOnGround) return 0.0f;

            // Must be actively rolling — |roll| > 0.5 filters out zero input
            if (fabsf(player.prevAction.roll) <= ROLL_THRESHOLD || player.pos.z < 300.0f) return 0.0f;

            // Only reward when climbing (+Z velocity) or already high up (Z >= 600)
            if (player.vel.z <= 0.0f && player.pos.z < 600.0f) return 0.0f;

            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            return REWARD_PER_SECOND * dt;
        }
    };

} // namespace RLGC