/*
 * File: RecoveryRewards.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Comprehensive recovery rewards system teaching 12 recovery techniques
 *   from basic to advanced. Improves game speed and flow through better
 *   car control after airborne situations, bumps, and awkward landings.
 *
 *   Difficulty Tiers:
 *   - BRONZE: Basic recoveries (landing straight, reverse, directional flips)
 *   - SILVER: Compound actions (small pads, backflips, powerslide combos)
 *   - GOLD: Advanced techniques (boost down, goal post, bumped recovery)
 *   - CHAMPION: Expert combos (reverse half flip, wavedash chains)
 *   - GRAND CHAMPION: Master mechanics (ceiling double jump, ground to wall)
 *
 * Author: @user + Claude
 * Date: January 2026
 * Revision: 1.0
 */

#pragma once

#include "Reward.h"
#include "../Math.h"
#include <map>
#include <deque>
#include <vector>
#include <cmath>
#include <atomic>
#include "../CommonValues.h"

namespace RLGC {

    // ============================================================================
    // BRONZE TIER - BASIC RECOVERY
    // ============================================================================

    // ============================================================================
    // REVERSE RECOVERY REWARD
    // Rewards landing backward and smoothly transitioning to forward motion.
    // Key skill: Don't panic when landing backward - use powerslide to recover.
    // ============================================================================
    class ReverseRecoveryReward : public Reward {
    private:
        struct ReverseRecoveryState {
            bool wasAirborne = false;
            bool landedBackward = false;
            Vec landingForward;
            Vec landingVel;
            float transitionStartTime = 0.0f;
            float landingSpeed = 0.0f;
        };

        mutable std::map<uint32_t, ReverseRecoveryState> playerStates;
        float currentTime = 0.0f;

        static constexpr float BACKWARD_THRESHOLD = -0.3f;  // Dot product threshold
        static constexpr float FORWARD_THRESHOLD = 0.5f;
        static constexpr float MAX_TRANSITION_TIME = 2.0f;
        static constexpr float BASE_REWARD = 2.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
            currentTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            currentTime += state.deltaTime;
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = ReverseRecoveryState();
            }

            auto& pState = playerStates[id];

            // Track airborne state
            if (!player.isOnGround) {
                pState.wasAirborne = true;
                return 0.0f;
            }

            // Detect backward landing
            if (pState.wasAirborne && player.isOnGround && !pState.landedBackward) {
                Vec velNorm = player.vel.Normalized();
                float forwardAlignment = player.rotMat.forward.Dot(velNorm);

                if (forwardAlignment < BACKWARD_THRESHOLD) {
                    // Landed backward!
                    pState.landedBackward = true;
                    pState.landingForward = player.rotMat.forward;
                    pState.landingVel = player.vel;
                    pState.transitionStartTime = currentTime;
                    pState.landingSpeed = player.vel.Length();
                }

                pState.wasAirborne = false;
                return 0.0f;
            }

            // Track transition to forward
            if (pState.landedBackward) {
                float elapsed = currentTime - pState.transitionStartTime;

                if (elapsed > MAX_TRANSITION_TIME) {
                    // Timeout - reset
                    pState.landedBackward = false;
                    return 0.0f;
                }

                Vec velNorm = player.vel.Normalized();
                float forwardAlignment = player.rotMat.forward.Dot(velNorm);

                if (forwardAlignment > FORWARD_THRESHOLD) {
                    // Successfully recovered!
                    float reward = BASE_REWARD;

                    // Smooth transition bonus (<1.0s)
                    if (elapsed < 1.0f) {
                        reward += 1.0f;
                    }

                    // Speed retention bonus (>70%)
                    float currentSpeed = player.vel.Length();
                    float speedRetention = currentSpeed / pState.landingSpeed;
                    if (speedRetention > 0.7f) {
                        reward += 1.5f;
                    }

                    // Powerslide bonus
                    if (player.handbrakeVal > 0.1f) {
                        reward += 0.5f;
                    }

                    pState.landedBackward = false;
                    return reward;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // DIRECTIONAL FLIP RECOVERY REWARD
    // Rewards using 45° diagonal flips to change direction while maintaining momentum.
    // Key skill: Don't waste front flips when you need to turn - use diagonal flips.
    // ============================================================================
    class DirectionalFlipRecoveryReward : public Reward {
    private:
        struct DirectionalFlipState {
            bool isFlipping = false;
            Vec preFlipVel;
            float preFlipSpeed = 0.0f;
            bool wasDiagonal = false;
        };

        mutable std::map<uint32_t, DirectionalFlipState> playerStates;

        static constexpr float DIAGONAL_INPUT_THRESHOLD = 0.3f;
        static constexpr float DIRECTION_CHANGE_MAX = 30.0f;  // degrees
        static constexpr float BASE_REWARD = 3.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = DirectionalFlipState();
            }

            auto& pState = playerStates[id];

            // Detect flip start
            if (!pState.isFlipping && player.isFlipping) {
                float pitchAbs = std::abs(player.prevAction.pitch);
                float yawAbs = std::abs(player.prevAction.yaw);

                // Check if diagonal input
                if (pitchAbs > DIAGONAL_INPUT_THRESHOLD && yawAbs > DIAGONAL_INPUT_THRESHOLD) {
                    pState.isFlipping = true;
                    pState.wasDiagonal = true;
                    pState.preFlipVel = player.vel;
                    pState.preFlipSpeed = player.vel.Length();
                }
                else {
                    pState.isFlipping = true;
                    pState.wasDiagonal = false;
                }
            }

            // Detect flip end
            if (pState.isFlipping && !player.isFlipping) {
                pState.isFlipping = false;

                if (!pState.wasDiagonal) {
                    return 0.0f;
                }

                // Check momentum maintenance
                float postFlipSpeed = player.vel.Length();
                Vec preFlipDir = pState.preFlipVel.Normalized();
                Vec postFlipDir = player.vel.Normalized();

                float directionChange = std::acos(RS_CLAMP(preFlipDir.Dot(postFlipDir), -1.0f, 1.0f)) * (180.0f / 3.14159f);

                if (directionChange < DIRECTION_CHANGE_MAX) {
                    float reward = BASE_REWARD;

                    // Speed gain bonus
                    float speedGain = postFlipSpeed - pState.preFlipSpeed;
                    if (speedGain > 100.0f) {
                        reward += 2.0f;
                    }

                    // Perfect angle bonus (40-50°)
                    float pitchAbs = std::abs(player.prevAction.pitch);
                    float yawAbs = std::abs(player.prevAction.yaw);
                    float ratio = std::min(pitchAbs, yawAbs) / std::max(pitchAbs, yawAbs);
                    if (ratio > 0.8f && ratio < 1.2f) {  // Close to 45°
                        reward += 1.0f;
                    }

                    // Ground to ground bonus
                    if (player.isOnGround) {
                        reward += 1.5f;
                    }

                    return reward;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // SILVER TIER - COMPOUND ACTIONS
    // ============================================================================

    // ============================================================================
    // SMALL PAD RECOVERY ROUTE REWARD
    // Rewards collecting 3+ small pads during recovery sequences.
    // Key skill: Learn pad locations and grab them while rotating/recovering.
    // ============================================================================
    class SmallPadRecoveryRouteReward : public Reward {
    private:
        struct PadRouteState {
            std::deque<float> padPickupTimes;
            bool inRecoveryMode = false;
            float recoveryStartTime = 0.0f;
            Vec lastPadPos;
            float totalDistance = 0.0f;
        };

        mutable std::map<uint32_t, PadRouteState> playerStates;
        float currentTime = 0.0f;

        static constexpr float SMALL_PAD_AMOUNT = 12.0f;
        static constexpr float RECOVERY_WINDOW = 4.0f;
        static constexpr float AIRTIME_THRESHOLD = 0.5f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
            currentTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!state.prev) return 0.0f;

            currentTime += state.deltaTime;
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = PadRouteState();
            }

            auto& pState = playerStates[id];

            // Enter recovery mode after significant airtime
            if (player.airTime > AIRTIME_THRESHOLD && !pState.inRecoveryMode) {
                pState.inRecoveryMode = true;
                pState.recoveryStartTime = currentTime;
                pState.padPickupTimes.clear();
                pState.totalDistance = 0.0f;
            }

            // Exit recovery mode after timeout
            if (pState.inRecoveryMode && (currentTime - pState.recoveryStartTime) > RECOVERY_WINDOW) {
                pState.inRecoveryMode = false;
            }

            // Track boost pickups
            float boostGained = player.boost - state.prev->players[player.index].boost;

            if (pState.inRecoveryMode && boostGained > 10.0f && boostGained < 15.0f) {
                // Small pad pickup
                pState.padPickupTimes.push_back(currentTime);

                // Track distance for efficiency
                if (!pState.padPickupTimes.empty() && pState.padPickupTimes.size() > 1) {
                    pState.totalDistance += (player.pos - pState.lastPadPos).Length();
                }
                pState.lastPadPos = player.pos;

                // Clean old pickups
                while (!pState.padPickupTimes.empty() &&
                       (currentTime - pState.padPickupTimes.front()) > RECOVERY_WINDOW) {
                    pState.padPickupTimes.pop_front();
                }

                // Reward based on sequence length
                int padCount = pState.padPickupTimes.size();

                if (padCount >= 4) {
                    float reward = 10.0f + (padCount - 4) * 2.0f;

                    // Efficiency bonus (optimal pathing)
                    float avgDist = pState.totalDistance / (padCount - 1);
                    if (avgDist < 800.0f) {
                        reward += 3.0f;
                    }

                    return reward;
                }
                else if (padCount == 3) {
                    return 6.0f;
                }
                else if (padCount == 2) {
                    return 3.0f;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // BACKFLIP RECOVERY REWARD
    // Rewards using backflip while moving in reverse for distance/speed.
    // Key skill: When stuck reversing, backflip covers more ground than driving.
    // ============================================================================
    class BackflipRecoveryReward : public Reward {
    private:
        struct BackflipRecoveryState {
            bool movingBackward = false;
            bool flipping = false;
            float preFlipSpeed = 0.0f;
            Vec preFlipPos;
        };

        mutable std::map<uint32_t, BackflipRecoveryState> playerStates;

        static constexpr float BACKWARD_VEL_THRESHOLD = -0.5f;
        static constexpr float BACKFLIP_PITCH_THRESHOLD = -0.7f;
        static constexpr float MIN_SPEED_GAIN = 150.0f;
        static constexpr float BASE_REWARD = 4.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = BackflipRecoveryState();
            }

            auto& pState = playerStates[id];

            // Detect moving backward
            float forwardVel = player.vel.Dot(player.rotMat.forward);
            bool nowBackward = forwardVel < BACKWARD_VEL_THRESHOLD * player.vel.Length();

            // Detect backflip start
            if (nowBackward && !pState.flipping && player.isFlipping) {
                float pitchInput = player.prevAction.pitch;
                if (pitchInput < BACKFLIP_PITCH_THRESHOLD) {
                    pState.flipping = true;
                    pState.preFlipSpeed = player.vel.Length();
                    pState.preFlipPos = player.pos;
                }
            }

            // Detect backflip end
            if (pState.flipping && !player.isFlipping) {
                pState.flipping = false;

                float postFlipSpeed = player.vel.Length();
                float speedGain = postFlipSpeed - pState.preFlipSpeed;
                float distanceCovered = (player.pos - pState.preFlipPos).Length();

                if (speedGain > MIN_SPEED_GAIN) {
                    float reward = BASE_REWARD;

                    // Speed bonus (scaled)
                    float speedBonus = (speedGain - MIN_SPEED_GAIN) / 50.0f;
                    reward += RS_MIN(speedBonus, 3.0f);

                    // Distance bonus
                    if (distanceCovered > 500.0f) {
                        reward += 2.0f;
                    }

                    return reward;
                }
            }

            pState.movingBackward = nowBackward;
            return 0.0f;
        }
    };

    // ============================================================================
    // POWERSLIDE JUMP FLIP REWARD
    // Rewards the awkward landing → powerslide → jump → flip combo.
    // Key skill: Use powerslide to buy time and maintain momentum on bad landings.
    // ============================================================================
    class PowerslideJumpFlipReward : public Reward {
    private:
        enum ComboPhase { NONE, LANDED_AWKWARD, POWERSLIDE, JUMPED, FLIPPED };

        struct PowerslideComboState {
            ComboPhase phase = NONE;
            float comboStartTime = 0.0f;
            float initialSpeed = 0.0f;
        };

        mutable std::map<uint32_t, PowerslideComboState> playerStates;
        float currentTime = 0.0f;

        static constexpr float AWKWARD_THRESHOLD = 0.6f;  // upZ threshold
        static constexpr float MAX_COMBO_TIME = 2.0f;
        static constexpr float FULL_COMBO_REWARD = 8.0f;
        static constexpr float PARTIAL_REWARD = 3.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
            currentTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            currentTime += state.deltaTime;
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = PowerslideComboState();
            }

            auto& pState = playerStates[id];

            // Phase timeout
            if (pState.phase != NONE && (currentTime - pState.comboStartTime) > MAX_COMBO_TIME) {
                pState.phase = NONE;
            }

            // Phase 1: Detect awkward landing
            if (pState.phase == NONE && player.isOnGround && player.worldContact.hasContact) {
                float upZ = player.rotMat.up.z;
                if (upZ < AWKWARD_THRESHOLD) {
                    pState.phase = LANDED_AWKWARD;
                    pState.comboStartTime = currentTime;
                    pState.initialSpeed = player.vel.Length();
                }
            }

            // Phase 2: Powerslide
            if (pState.phase == LANDED_AWKWARD && player.handbrakeVal > 0.5f) {
                pState.phase = POWERSLIDE;
            }

            // Phase 3: Jump
            if (pState.phase == POWERSLIDE && player.hasJumped) {
                pState.phase = JUMPED;
            }

            // Phase 4: Flip
            if (pState.phase == JUMPED && player.isFlipping) {
                pState.phase = FLIPPED;

                float reward = FULL_COMBO_REWARD;
                float comboTime = currentTime - pState.comboStartTime;

                // Timing bonus
                if (comboTime < 1.5f) {
                    reward += 2.0f;
                }

                // Speed bonus
                float finalSpeed = player.vel.Length();
                if (finalSpeed > pState.initialSpeed + 200.0f) {
                    reward += 2.0f;
                }

                pState.phase = NONE;
                return reward;
            }

            // Partial reward for powerslide + jump
            if (pState.phase == JUMPED && !player.isOnGround && !player.isFlipping) {
                // Check if we completed jump without flip
                if (player.airTime > 0.2f) {
                    pState.phase = NONE;
                    return PARTIAL_REWARD;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // GOLD TIER - ADVANCED TECHNIQUES
    // ============================================================================

    // ============================================================================
    // BOOST DOWN RECOVERY REWARD
    // Rewards using boost to quickly return to the floor from height.
    // Key skill: Face down + boost when you need to land FAST.
    // ============================================================================
    class BoostDownRecoveryReward : public Reward {
    private:
        struct BoostDownState {
            float airborneHeight = 0.0f;
            float descentStartTime = 0.0f;
            bool wasBoosting = false;
            float boostUsed = 0.0f;
            float initialBoost = 0.0f;
        };

        mutable std::map<uint32_t, BoostDownState> playerStates;
        float currentTime = 0.0f;

        static constexpr float MIN_HEIGHT = 300.0f;
        static constexpr float DOWN_DOT_THRESHOLD = 0.6f;
        static constexpr float DESCENT_SPEED_THRESHOLD = -800.0f;
        static constexpr float BASE_REWARD = 5.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
            currentTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            currentTime += state.deltaTime;
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = BoostDownState();
            }

            auto& pState = playerStates[id];

            // Detect boost down initiation
            if (!player.isOnGround && player.pos.z > MIN_HEIGHT) {
                Vec down(0.0f, 0.0f, -1.0f);
                float facingDown = player.rotMat.up.Dot(down);

                if (facingDown > DOWN_DOT_THRESHOLD && player.prevAction.boost > 0.5f) {
                    if (!pState.wasBoosting) {
                        pState.airborneHeight = player.pos.z;
                        pState.descentStartTime = currentTime;
                        pState.initialBoost = player.boost;
                    }
                    pState.wasBoosting = true;
                    pState.boostUsed = pState.initialBoost - player.boost;
                }
                else {
                    pState.wasBoosting = false;
                }
            }

            // Detect landing after boost down
            if (pState.wasBoosting && player.isOnGround) {
                float descentTime = currentTime - pState.descentStartTime;
                float upZ = player.rotMat.up.z;

                float reward = BASE_REWARD;

                // Time bonus (quick descent)
                if (descentTime < 1.5f) {
                    reward += 3.0f;
                }

                // Landing quality bonus
                if (upZ > 0.8f) {
                    reward += 2.0f;
                }

                // Boost efficiency bonus
                if (pState.boostUsed < 30.0f) {
                    reward += 1.5f;
                }

                pState.wasBoosting = false;
                return reward;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // GOAL POST RECOVERY REWARD
    // Rewards using the opponent's net ceiling/walls to quickly rotate.
    // Key skill: Drive through net instead of slow turn-around on ground.
    // ============================================================================
    class GoalPostRecoveryReward : public Reward {
    private:
        struct GoalPostState {
            bool inGoalArea = false;
            Vec entryVel;
            float contactTime = 0.0f;
            float entryTime = 0.0f;
        };

        mutable std::map<uint32_t, GoalPostState> playerStates;
        float currentTime = 0.0f;

        static constexpr float GOAL_AREA_MARGIN = 300.0f;
        static constexpr float MIN_HEIGHT = 500.0f;
        static constexpr float MAX_HEIGHT = 642.0f;
        static constexpr float BASE_REWARD = 6.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
            currentTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            currentTime += state.deltaTime;
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = GoalPostState();
            }

            auto& pState = playerStates[id];

            // Determine opponent goal
            Vec oppGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

            float distToGoal = (player.pos - oppGoal).Length();
            bool inGoalHeight = player.pos.z > MIN_HEIGHT && player.pos.z < MAX_HEIGHT;
            bool yInGoal = std::abs(player.pos.y) > (CommonValues::BACK_WALL_Y - GOAL_AREA_MARGIN);

            // Detect entering goal area
            if (!pState.inGoalArea && yInGoal && inGoalHeight && distToGoal < 1000.0f) {
                pState.inGoalArea = true;
                pState.entryVel = player.vel;
                pState.entryTime = currentTime;
            }

            // Detect exiting goal area with recovery
            if (pState.inGoalArea && (!yInGoal || !inGoalHeight || distToGoal > 1000.0f)) {
                float timeInGoal = currentTime - pState.entryTime;

                float reward = BASE_REWARD;

                // Quick exit bonus
                if (timeInGoal < 0.8f) {
                    reward += 3.0f;
                }

                // Defensive context bonus (ball in net area)
                float ballDistToGoal = (state.ball.pos - oppGoal).Length();
                if (ballDistToGoal < 1500.0f) {
                    reward += 4.0f;
                }

                // Clean orientation bonus
                float upZ = player.rotMat.up.z;
                if (upZ > 0.7f) {
                    reward += 2.0f;
                }

                pState.inGoalArea = false;
                return reward;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // BUMPED FLIP RECOVERY REWARD
    // Rewards using saved flip after being bumped for quick recovery.
    // Key skill: After getting bumped, use your flip immediately to regain control.
    // Updated 2/16/2026 with Claude. 
    // BumpedFlipRecoveryReward (LOW_ACTIVATION → 4.76% → target: ~15-20%)

    /*Simplified bump detection to velocity loss threshold instead of complex state tracking
        Lowered BUMP_DETECTION_THRESHOLD from original logic(500 units)
        Increased RECOVERY_WINDOW from 2.0s to 3.0s for more achievable recovery window
        Removed overly strict rotation alignment requirements
        Added PreStep to properly track time
        Streamlined state machine : bump detection → recovery window → landing evaluation
        Bonus structure : base 4.0 + early recovery(1.5) + speed retention(2.0) + control(1.0) = up to 8.5*/
    // ============================================================================
    class BumpedFlipRecoveryReward : public Reward {
    private:
        struct BumpedFlipState {
            bool wasAirborne = false;
            bool inRecovery = false;
            float recoveryStartTime = 0.0f;
            float lastAirborneSpeed = 0.0f;
            Vec recoveryStartPos;
            float recoveryStartZ = 0.0f;
        };

        mutable std::map<uint32_t, BumpedFlipState> playerStates;
        float currentTime = 0.0f;

        // Thresholds tuned for better activation
        static constexpr float RECOVERY_WINDOW = 3.0f;
        static constexpr float MIN_AIRBORNE_TIME = 0.2f;
        static constexpr float FLIP_COMPLETION_THRESHOLD = 0.80f;
        static constexpr float BASE_RECOVERY_REWARD = 3.5f;
        static constexpr float SPEED_RETENTION_BONUS = 1.5f;
        static constexpr float EARLY_RECOVERY_BONUS = 1.0f;
        static constexpr float CONTROL_BONUS = 1.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
            currentTime = 0.0f;
        }

        virtual void PreStep(const GameState& state) override {
            currentTime += state.deltaTime;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = BumpedFlipState();
            }

            auto& pState = playerStates[id];

            // Track airborne state and detect bumps
            if (!player.isOnGround && !pState.wasAirborne) {
                // Just became airborne - initialize tracking
                pState.wasAirborne = true;
                pState.lastAirborneSpeed = player.vel.Length();
                return 0.0f;
            }

            // While airborne, watch for sudden significant speed loss (bump indicator)
            if (!player.isOnGround && pState.wasAirborne) {
                float currentSpeed = player.vel.Length();
                float speedLoss = pState.lastAirborneSpeed - currentSpeed;

                // Detect significant velocity drop (bump)
                if (speedLoss > 400.0f && currentSpeed > 200.0f && !pState.inRecovery) {
                    pState.inRecovery = true;
                    pState.recoveryStartTime = currentTime;
                    pState.recoveryStartPos = player.pos;
                    pState.recoveryStartZ = player.pos.z;
                }

                pState.lastAirborneSpeed = currentSpeed;
                return 0.0f;
            }

            // Detect landing after recovery
            if (player.isOnGround && pState.wasAirborne) {
                // Reset airborne tracking
                pState.wasAirborne = false;

                if (pState.inRecovery) {
                    float timeSinceRecovery = currentTime - pState.recoveryStartTime;

                    // Must have taken reasonable time for recovery (not instant)
                    if (timeSinceRecovery < MIN_AIRBORNE_TIME || timeSinceRecovery > RECOVERY_WINDOW) {
                        pState.inRecovery = false;
                        return 0.0f;
                    }

                    // Check landing orientation
                    float upZ = player.rotMat.up.z;

                    // Reward proper landing (car upright)
                    if (upZ > FLIP_COMPLETION_THRESHOLD) {
                        float reward = BASE_RECOVERY_REWARD;

                        // Early recovery bonus
                        if (timeSinceRecovery < 1.2f) {
                            reward += EARLY_RECOVERY_BONUS;
                        }

                        // Speed retention bonus
                        float currentSpeed = player.vel.Length();
                        if (currentSpeed > 600.0f) {
                            reward += SPEED_RETENTION_BONUS;
                        }

                        // Control bonus
                        if (currentSpeed > 100.0f) {
                            Vec velDir = player.vel.Normalized();
                            float controlAlignment = player.rotMat.forward.Dot(velDir);
                            if (controlAlignment > 0.5f) {
                                reward += CONTROL_BONUS;
                            }
                        }

                        pState.inRecovery = false;
                        return RS_CLAMP(reward, -1.0f, 1.0f);
                    }
                }

                pState.inRecovery = false;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // CHAMPION TIER - EXPERT COMBOS
    // ============================================================================

    // ============================================================================
    // REVERSE HALF FLIP REWARD
    // Rewards executing half flip while moving in reverse for momentum boost.
    // Key skill: Reverse + half flip is faster than normal half flip from stop.
    // ============================================================================
    class ReverseHalfFlipReward : public Reward {
    private:
        struct ReverseHalfFlipState {
            Vec preFlipForward;
            bool wasReverse = false;
            bool flipping = false;
            float preFlipSpeed = 0.0f;
        };

        mutable std::map<uint32_t, ReverseHalfFlipState> playerStates;

        static constexpr float REVERSE_SPEED_THRESHOLD = -500.0f;
        static constexpr float ROTATION_THRESHOLD = -0.8f;
        static constexpr float BASE_REWARD = 12.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = ReverseHalfFlipState();
            }

            auto& pState = playerStates[id];

            // Detect reverse movement
            float forwardVel = player.vel.Dot(player.rotMat.forward);
            bool nowReverse = forwardVel < REVERSE_SPEED_THRESHOLD;

            // Detect flip start while reversing
            if (nowReverse && !pState.flipping && player.isFlipping) {
                pState.flipping = true;
                pState.wasReverse = true;
                pState.preFlipForward = player.rotMat.forward;
                pState.preFlipSpeed = player.vel.Length();
            }

            // Detect flip end
            if (pState.flipping && !player.isFlipping && pState.wasReverse) {
                pState.flipping = false;

                // Check for 180° rotation
                float forwardAlignment = pState.preFlipForward.Dot(player.rotMat.forward);

                if (forwardAlignment < ROTATION_THRESHOLD) {
                    // Successful reverse half flip!
                    float reward = BASE_REWARD;

                    // Precision bonus (closer to -1.0 = perfect 180°)
                    if (forwardAlignment < -0.9f) {
                        reward += 4.0f;
                    }

                    // Speed retention bonus
                    float currentSpeed = player.vel.Length();
                    float speedRetention = currentSpeed / pState.preFlipSpeed;
                    if (speedRetention > 0.8f) {
                        reward += 3.0f;
                    }

                    pState.wasReverse = false;
                    return reward;
                }

                pState.wasReverse = false;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // WAVEDASH JUMP FLIP CHAIN REWARD
    // Rewards chaining wavedash → jump → flip for maximum speed boost.
    // Key skill: Don't stop after wavedash - chain it for supersonic speed.
    // ============================================================================
    class WavedashJumpFlipChainReward : public Reward {
    private:
        enum ChainPhase { NONE, WAVEDASH, JUMPED, FLIPPED };

        struct WavedashChainState {
            ChainPhase phase = NONE;
            float wavedashTime = 0.0f;
            float preWavedashSpeed = 0.0f;
            bool jumped = false;
        };

        mutable std::map<uint32_t, WavedashChainState> playerStates;
        float currentTime = 0.0f;

        static constexpr float JUMP_WINDOW = 0.3f;
        static constexpr float FLIP_WINDOW = 0.5f;
        static constexpr float BASE_REWARD = 15.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
            currentTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            currentTime += state.deltaTime;
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = WavedashChainState();
            }

            auto& pState = playerStates[id];

            // Detect wavedash (simplified - assumes wavedash was just performed)
            // In practice, you'd integrate with WavedashReward state
            if (pState.phase == NONE && player.isOnGround && player.isFlipping) {
                // Assume wavedash if flipping while landing
                pState.phase = WAVEDASH;
                pState.wavedashTime = currentTime;
                pState.preWavedashSpeed = player.vel.Length();
            }

            // Detect jump after wavedash
            if (pState.phase == WAVEDASH && player.hasJumped) {
                float timeSinceWavedash = currentTime - pState.wavedashTime;
                if (timeSinceWavedash < JUMP_WINDOW) {
                    pState.phase = JUMPED;
                }
            }

            // Detect flip after jump
            if (pState.phase == JUMPED && player.isFlipping) {
                float chainTime = currentTime - pState.wavedashTime;
                if (chainTime < FLIP_WINDOW) {
                    pState.phase = FLIPPED;

                    float reward = BASE_REWARD;

                    // Speed bonus
                    float currentSpeed = player.vel.Length();
                    float speedGain = currentSpeed - pState.preWavedashSpeed;
                    if (speedGain > 300.0f) {
                        reward += 5.0f;
                    }

                    // Timing perfection bonus
                    if (chainTime < 0.8f) {
                        reward += 4.0f;
                    }

                    pState.phase = NONE;
                    return reward;
                }
            }

            // Reset if took too long
            if (pState.phase != NONE) {
                float elapsed = currentTime - pState.wavedashTime;
                if (elapsed > FLIP_WINDOW) {
                    pState.phase = NONE;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // GRAND CHAMPION TIER - MASTER MECHANICS
    // ============================================================================

    // ============================================================================
    // CEILING DOUBLE JUMP REWARD
    // Rewards using ceiling to get flip reset, then double jumping for descent.
    // Key skill: Ceiling gives free flip - use it to control descent and land clean.
    // ============================================================================
    class CeilingDoubleJumpReward : public Reward {
    private:
        struct CeilingDoubleJumpState {
            bool wasOnCeiling = false;
            Vec ceilingExitPos;
            bool hasFlipReset = false;
            float exitTime = 0.0f;
        };

        mutable std::map<uint32_t, CeilingDoubleJumpState> playerStates;
        float currentTime = 0.0f;

        static constexpr float CEILING_THRESHOLD = 150.0f;
        static constexpr float BASE_REWARD = 18.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
            currentTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            currentTime += state.deltaTime;
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = CeilingDoubleJumpState();
            }

            auto& pState = playerStates[id];

            // Detect on ceiling
            bool onCeiling = player.pos.z > (CommonValues::CEILING_Z - CEILING_THRESHOLD);

            if (onCeiling && player.isOnGround) {
                pState.wasOnCeiling = true;
            }

            // Detect leaving ceiling with flip reset
            if (pState.wasOnCeiling && !onCeiling && player.HasFlipReset()) {
                pState.hasFlipReset = true;
                pState.ceilingExitPos = player.pos;
                pState.exitTime = currentTime;
            }

            // Detect double jump usage and landing
            if (pState.hasFlipReset && player.hasDoubleJumped && player.isOnGround) {
                float upZ = player.rotMat.up.z;

                float reward = BASE_REWARD;

                // Landing quality bonus
                if (upZ > 0.85f) {
                    reward += 4.0f;
                }

                // Height control bonus (smooth descent)
                float descentHeight = pState.ceilingExitPos.z - player.pos.z;
                if (descentHeight > 1500.0f) {
                    reward += 5.0f;
                }

                // Ball context bonus
                float distToBall = (player.pos - state.ball.pos).Length();
                if (distToBall < 800.0f) {
                    reward += 6.0f;
                }

                pState.wasOnCeiling = false;
                pState.hasFlipReset = false;
                return reward;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // GROUND TO WALL FLIP REWARD
    // Rewards jumping from ground directly onto wall with flip for elevation.
    // Key skill: Fastest way to get on wall high - flip diagonally from ground.
    // ============================================================================
    class GroundToWallFlipReward : public Reward {
    private:
        struct GroundToWallState {
            bool onGroundNearWall = false;
            Vec jumpPos;
            float nearestWallDist = 0.0f;
            bool flipping = false;
        };

        mutable std::map<uint32_t, GroundToWallState> playerStates;

        static constexpr float WALL_PROXIMITY = 600.0f;
        static constexpr float WALL_LANDING_THRESHOLD = 120.0f;
        static constexpr float MIN_WALL_HEIGHT = 150.0f;
        static constexpr float BASE_REWARD = 20.0f;

        float DistanceToNearestWall(const Vec& pos) const {
            float xDist = std::abs(std::abs(pos.x) - CommonValues::SIDE_WALL_X);
            float yDist = std::abs(std::abs(pos.y) - CommonValues::BACK_WALL_Y);
            return std::min(xDist, yDist);
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = GroundToWallState();
            }

            auto& pState = playerStates[id];

            float wallDist = DistanceToNearestWall(player.pos);

            // Detect on ground near wall
            if (player.isOnGround && wallDist < WALL_PROXIMITY && player.pos.z < 100.0f) {
                pState.onGroundNearWall = true;
                pState.jumpPos = player.pos;
                pState.nearestWallDist = wallDist;
            }

            // Detect flip toward wall
            if (pState.onGroundNearWall && player.isFlipping) {
                pState.flipping = true;
            }

            // Detect landing on wall
            if (pState.flipping && player.isOnGround) {
                float currentWallDist = DistanceToNearestWall(player.pos);

                // Check if landed on wall (close to wall + elevated)
                if (currentWallDist < WALL_LANDING_THRESHOLD && player.pos.z > MIN_WALL_HEIGHT) {
                    float reward = BASE_REWARD;

                    // Height bonus
                    if (player.pos.z > 400.0f) {
                        reward += 6.0f;
                    }

                    // Speed retention bonus
                    float speed = player.vel.Length();
                    if (speed > 1000.0f) {
                        reward += 5.0f;
                    }

                    // Offensive context bonus
                    bool inOpponentHalf = (player.team == Team::BLUE && player.pos.y > 0) ||
                                          (player.team == Team::ORANGE && player.pos.y < 0);
                    if (inOpponentHalf) {
                        reward += 8.0f;
                    }

                    pState.onGroundNearWall = false;
                    pState.flipping = false;
                    return reward;
                }

                pState.flipping = false;
                pState.onGroundNearWall = false;
            }

            return 0.0f;
        }
    };

} // namespace RLGC
