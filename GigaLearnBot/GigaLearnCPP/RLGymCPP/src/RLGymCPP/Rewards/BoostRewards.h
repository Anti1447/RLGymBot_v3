#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <deque>
#include <vector>
#include <atomic>
#include "RewardMath.h"
#include <cmath>
#include "../CommonValues.h"

namespace RLGC {

    // Boost management - using boost efficiently
    class BoostEfficiencyReward : public Reward {
    private:
        mutable std::map<uint32_t, float> lastBoost;

    public:
        virtual void Reset(const GameState& initialState) override {
            lastBoost.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float prevBoost = lastBoost[player.carId];
            lastBoost[player.carId] = player.boost;

            if (prevBoost == 0.0f) return 0.0f;

            float boostUsed = prevBoost - player.boost;
            if (boostUsed <= 0.0f) return 0.0f;

            // Reward boost usage that results in productive actions
            float speed = player.vel.Length();
            if (speed > 1000.0f && boostUsed > 0.0f) {
                // Using boost while moving fast = good
                return 0.5f;
            }

            // Penalize boost wastage (using boost while slow)
            if (speed < 500.0f && boostUsed > 1.0f) {
                return -0.2f;
            }

            return 0.0f;
        }
    };

    // ============================================================================
// LowBoostOppHalfFiftyPunish
// 2025-12-16 - Rev 1 (NJH):
//  - Punish 50/50 challenges with low boost (<25) in opponent half.
//  - Max punishment in opponent corners (highest concede risk).
// ============================================================================
    class LowBoostOppHalfFiftyPunish : public Reward {
    private:
        float lowBoostThresh = 25.0f;

        // punishment range (negative)
        float minPunish = -2.0f;   // center-ish opp half
        float maxPunish = -8.0f;   // deep opp corner

        bool IsFiftyFiftyTouch(const Player& player, const GameState& state) const {
            if (!player.ballTouchedStep) return false;
            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;
                if (opp.ballTouchedStep) return true;
            }
            return false;
        }

        float OppCornerFactor(const Player& player, const Vec& ballPos) const {
            // Opponent corners are near (+/-SIDE_WALL_X, +/-BACK_WALL_Y) in the opponent half.
            // We compute “corner-ness” by how far out in X and how deep in opponent Y.
            const float xAbs = fabs(ballPos.x);
            const float yAbs = fabs(ballPos.y);

            float xFrac = RS_CLAMP((xAbs - 2500.0f) / (CommonValues::SIDE_WALL_X - 2500.0f), 0.0f, 1.0f);
            float yFrac = RS_CLAMP((yAbs - 3500.0f) / (CommonValues::BACK_WALL_Y - 3500.0f), 0.0f, 1.0f);

            // Multiply so “both conditions” matter (true corners)
            return RS_CLAMP(xFrac * yFrac, 0.0f, 1.0f);
        }

    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!IsFiftyFiftyTouch(player, state)) return 0.0f;
            if (player.boost >= lowBoostThresh) return 0.0f;

            const bool ballInOwnHalf = (player.team == Team::BLUE) ? (state.ball.pos.y <= 0.0f) : (state.ball.pos.y >= 0.0f);
            if (ballInOwnHalf) return 0.0f; // only punish in opponent half

            float corner = OppCornerFactor(player, state.ball.pos);
            return minPunish + (maxPunish - minPunish) * corner; // more negative as corner->1
        }
    };

    // Small pad collection - picking up small boost pads
    class SmallPadCollectionReward : public Reward {
    private:
        mutable std::map<uint32_t, std::deque<float>> recentBoostPickups;

    public:
        virtual void Reset(const GameState& initialState) override {
            recentBoostPickups.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!state.prev) return 0.0f;

            float prevBoost = state.prev->players[player.index].boost;
            float curBoost = player.boost;
            float boostGain = curBoost - prevBoost;

            if (boostGain > 0.0f && boostGain < 15.0f) {
                // Picked up small pad (12 boost)
                auto& pickups = recentBoostPickups[player.carId];
                pickups.push_back(state.deltaTime);

                // Keep only recent pickups (last 3 seconds)
                while (!pickups.empty() && (state.deltaTime - pickups.front()) > 3.0f) {
                    pickups.pop_front();
                }

                // Reward for collecting multiple small pads in sequence
                if (pickups.size() >= 3) {
                    return 5.0f * pickups.size(); // Escalating reward
                }

                return 2.0f; // Base reward for small pad
            }

            return 0.0f;
        }
    };

    // Low boost defense - defending with low boost
    class LowBoostDefenseReward : public Reward {
    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.boost > 30.0f) return 0.0f; // Not low boost

            Vec myGoal = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            // Check if in defensive position
            float distToGoal = (player.pos - myGoal).Length();
            float ballDistToGoal = (state.ball.pos - myGoal).Length();

            if (distToGoal < ballDistToGoal && ballDistToGoal < 3000.0f) {
                // In good defensive position with low boost
                return 2.0f;
            }

            return 0.0f;
        }
    };

    // Boost pad path learning - efficient boost collection
    class BoostPathReward : public Reward {
    private:
        mutable std::map<uint32_t, std::deque<Vec>> recentPadPositions;

    public:
        virtual void Reset(const GameState& initialState) override {
            recentPadPositions.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!state.prev) return 0.0f;

            float boostGain = player.boost - state.prev->players[player.index].boost;

            if (boostGain > 0.0f) {
                // Find which pad was collected
                Vec padPos = player.pos; // Approximate

                auto& path = recentPadPositions[player.carId];
                path.push_back(padPos);

                // Keep last 5 pads
                if (path.size() > 5) {
                    path.pop_front();
                }

                // Reward for following boost path (pads close together)
                if (path.size() >= 3) {
                    float totalDist = 0.0f;
                    for (size_t i = 1; i < path.size(); i++) {
                        totalDist += (path[i] - path[i - 1]).Length();
                    }

                    float avgDist = totalDist / (path.size() - 1);

                    // Reward efficient pathing (short distances between pads)
                    if (avgDist < 1500.0f) {
                        return 5.0f;
                    }
                }
            }

            return 0.0f;
        }
    };

    // Low boost offense - attacking with minimal boost
    class LowBoostOffenseReward : public Reward {
    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.boost > 30.0f) return 0.0f;

            Vec theirGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

            float ballDistToGoal = (state.ball.pos - theirGoal).Length();
            float myDistToBall = (player.pos - state.ball.pos).Length();

            // In offensive position with low boost
            if (ballDistToGoal < 3000.0f && myDistToBall < 1500.0f) {
                if (player.ballTouchedStep) {
                    return 8.0f; // Offensive touch with low boost
                }
                return 2.0f; // Offensive positioning with low boost
            }

            return 0.0f;
        }
    };

    // Boost Stealing Reward
    class BoostStealReward : public Reward {
    private:
        // Big boost pad locations
        const std::vector<Vec> bigBoostPads = {
            Vec(-3072.0f, -4096.0f, 73.0f),  // Blue back left
            Vec(3072.0f, -4096.0f, 73.0f),   // Blue back right
            Vec(-3584.0f, 0.0f, 73.0f),      // Mid left
            Vec(3584.0f, 0.0f, 73.0f),       // Mid right
            Vec(-3072.0f, 4096.0f, 73.0f),   // Orange back left
            Vec(3072.0f, 4096.0f, 73.0f)     // Orange back right
        };

        mutable std::map<uint32_t, float> lastBoostAmounts;

        bool IsOpposingSide(const Vec& padPos, Team team) const {
            if (team == Team::BLUE) {
                return padPos.y > 1000.0f;  // In orange half
            }
            else {
                return padPos.y < -1000.0f;  // In blue half
            }
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            lastBoostAmounts.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float reward = 0.0f;

            // Initialize tracking
            if (lastBoostAmounts.find(player.carId) == lastBoostAmounts.end()) {
                lastBoostAmounts[player.carId] = player.boost;
                return 0.0f;
            }

            float boostGained = player.boost - lastBoostAmounts[player.carId];

            // Check if player gained boost (picked up pad)
            if (boostGained > 90.0f) {  // Big boost pad threshold
                // Find which pad was likely picked up
                for (const auto& pad : bigBoostPads) {
                    float dist = (pad - player.pos).Length();

                    if (dist < 200.0f) {
                        if (IsOpposingSide(pad, player.team)) {
                            // Stole boost from opponent side!
                            reward += 0.4f;

                            // Extra reward if in opponent corner
                            bool cornerPad = abs(pad.y) > 3500.0f && abs(pad.x) > 2500.0f;
                            if (cornerPad) {
                                reward += 0.3f;
                            }

                            // Bonus based on how low on boost
                            float desperation = 1.0f - (lastBoostAmounts[player.carId] / 100.0f);
                            reward += desperation * 0.2f;
                        }
                        break;
                    }
                }
            }

            lastBoostAmounts[player.carId] = player.boost;
            return reward;
        }
    };

    // ============================================================================
// SAVE BOOST REWARD
// Scales linearly with INVERSE boost amount - more reward when boost is lower
// Based on: https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/misc_rewards.py
// ============================================================================
    class SaveBoostReward : public Reward {
    public:
        float exponent;
        SaveBoostReward(float exponent = 1.0f) : exponent(exponent) {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Linear scaling: 100 boost = 0.0 reward, 0 boost = 1.0 reward
            // With exponent = 1.0, this is perfectly linear
            // Lower exponent (e.g., 0.5) gives more reward at higher boost levels
            float boostFraction = player.boost / 100.0f;
            float inverseBoost = 1.0f - boostFraction;
            return RS_CLAMP(powf(inverseBoost, exponent), 0.0f, 1.0f);
        }
    };

    // ============================================================================
    // DISCOURAGE FLIPPING LOW BOOST REWARD
    // Penalizes flipping when both boost and speed are low
    // Speed threshold: 1300 uu/s (≈47 kph)
    // Boost threshold: 12
    // ============================================================================
    class DiscourageFlippingLowBoostReward : public Reward {
    private:
        mutable std::map<uint32_t, bool> wasFlipping;

        static constexpr float SPEED_THRESHOLD = 1300.0f;  // uu/s
        static constexpr float BOOST_THRESHOLD = 12.0f;
        static constexpr float PENALTY = -0.5f;

    public:
        virtual void Reset(const GameState& initialState) override {
            wasFlipping.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            bool currentlyFlipping = player.isFlipping;
            bool previouslyFlipping = wasFlipping[player.carId];
            wasFlipping[player.carId] = currentlyFlipping;

            // Detect flip initiation
            if (currentlyFlipping && !previouslyFlipping) {
                float speed = player.vel.Length();
                float boost = player.boost;

                // Penalize if both speed AND boost are low
                if (speed < SPEED_THRESHOLD && boost < BOOST_THRESHOLD) {
                    return PENALTY;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
// SUPERSONIC REWARD
// Rewards maintaining supersonic speed (2300+ uu/s)
// Encourages high-speed gameplay and boost efficiency
// ============================================================================
    class SupersonicReward : public Reward {
    private:
        mutable std::map<uint32_t, float> supersonicDuration;

        // Metrics
        inline static std::atomic<long long> s_supersonicTimeMilli{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };

        static constexpr float SUPERSONIC_THRESHOLD = 2300.0f;  // uu/s
        static constexpr float BASE_REWARD_PER_SECOND = 0.5f;
        static constexpr float MAX_DURATION_BONUS = 2.0f;       // Caps at 5 seconds
        static constexpr float DURATION_SCALE = 5.0f;           // Time to reach max bonus

    public:
        struct SupersonicMetrics {
            double supersonicTime = 0.0;    // Total time spent supersonic (seconds)
            double rewardSum = 0.0;
        };

        static SupersonicMetrics GetAndResetMetrics() {
            SupersonicMetrics m;
            long long t = s_supersonicTimeMilli.exchange(0);
            m.supersonicTime = double(t) / 1000.0;
            long long r = s_rewardMilli.exchange(0);
            m.rewardSum = double(r) / 1000.0;
            return m;
        }

        virtual void Reset(const GameState& initialState) override {
            supersonicDuration.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float speed = player.vel.Length();
            float& duration = supersonicDuration[player.carId];

            if (speed >= SUPERSONIC_THRESHOLD) {
                // Accumulate supersonic time
                duration += state.deltaTime;

                // Base reward for being supersonic
                float baseReward = BASE_REWARD_PER_SECOND * state.deltaTime;

                // Bonus for sustained supersonic (encourages maintaining it)
                // Scales from 0 to MAX_DURATION_BONUS over DURATION_SCALE seconds
                float durationBonus = RS_MIN(duration / DURATION_SCALE, 1.0f) * MAX_DURATION_BONUS;
                float sustainBonus = durationBonus * state.deltaTime;

                float totalReward = baseReward + sustainBonus;

                // Track metrics
                s_supersonicTimeMilli.fetch_add(static_cast<long long>(state.deltaTime * 1000.0f));
                s_rewardMilli.fetch_add(static_cast<long long>(totalReward * 1000.0f));

                return totalReward;
            }
            else {
                // Reset duration when not supersonic
                duration = 0.0f;
                return 0.0f;
            }
        }
    };


    // ============================================================================
    // SUPERSONIC BOOST PENALTY
    // Penalizes boosting when already at supersonic speed (2300 uu/s)
    // Encourages boost conservation at max speed
    // ============================================================================
    class SupersonicBoostPenalty : public Reward {
    private:
        static constexpr float SUPERSONIC_THRESHOLD = 2300.0f;  // uu/s
        static constexpr float PENALTY_PER_TICK = -0.05f;

    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Only penalize when on ground - aerial boosting can be useful
            if (!player.isOnGround) {
                return 0.0f;
            }

            // Check if player is supersonic
            float speed = player.vel.Length();
            if (speed < SUPERSONIC_THRESHOLD) {
                return 0.0f;
            }

            // Check if player is boosting
            bool isBoosting = (player.prevAction.boost != 0);
            if (!isBoosting) {
                return 0.0f;
            }

            // Penalize wasteful boosting
            return PENALTY_PER_TICK;
        }
    };

    class BoostWastePenaltyReward : public Reward {
    private:
        struct WasteConfig {
            // Penalty magnitude
            float basePenaltyPerSecond = -1.0f;
            float aerialWasteMultiplier = 1.5f;  // Worse in air
            float supersonicWasteMultiplier = 2.0f;  // Really bad waste

            // Thresholds
            float highBoostConsumptionRate = 25.0f;  // Boost/sec is "high"
            float boostDampingThreshold = 100.0f;    // Max speed for efficient use
            float minBoostEfficiency = 0.3f;         // Ratio of speed gain to boost spent

            // Ball contest context
            float ballContestDistance = 1500.0f;     // Distance to consider ball "contestable"
            float ballContestHeight = 300.0f;        // Height to consider ball "in air"

            // Mitigation factors (reduce penalty in these cases)
            float defensiveReductionFactor = 0.5f;   // Less penalty when defending
            float dribbleReductionFactor = 0.3f;     // Less penalty during dribble
            float rotationReductionFactor = 0.7f;    // Slightly reduced during rotation

            bool enablePenalty = true;
        };

        mutable std::map<uint32_t, float> previousBoost;
        mutable std::map<uint32_t, float> previousSpeed;
        mutable std::map<uint32_t, float> wasteAccumulator;

        WasteConfig config;

        bool IsInContestableBallPosition(const Player& player, const GameState& state) const {
            float ballDist = player.pos.Dist(state.ball.pos);
            float ballHeight = state.ball.pos.z;

            // Ball is contestable if close horizontally and in reachable height
            bool isClose = ballDist < config.ballContestDistance;
            bool isReachable = ballHeight < 400.0f;  // Standard car reach

            return isClose && isReachable;
        }

        bool IsDefending(const Player& player, const GameState& state) const {
            // Near own goal
            float ownGoalY = (player.team == Team::BLUE) ? -4000.0f : 4000.0f;
            return abs(player.pos.y - ownGoalY) < 2500.0f;
        }

        bool IsDribbling(const Player& player, const GameState& state) const {
            // Close to ball, low speed, on ground
            float ballDist = player.pos.Dist(state.ball.pos);
            return ballDist < 250.0f &&
                player.isOnGround &&
                player.vel.Length() < 1000.0f;
        }

        bool IsRotatingThroughOpponent(const Player& player, const GameState& state) const {
            // On opponent side, moving toward goal, relatively low speed
            bool onOpponentSide = (player.team == Team::BLUE && player.pos.y > 1000.0f) ||
                (player.team == Team::ORANGE && player.pos.y < -1000.0f);

            float speed = player.vel.Length();
            return onOpponentSide && speed < 1800.0f;  // Controlled approach
        }

        float GetMitigationFactor(const Player& player, const GameState& state) const {
            if (IsDefending(player, state)) {
                return config.defensiveReductionFactor;
            }
            else if (IsDribbling(player, state)) {
                return config.dribbleReductionFactor;
            }
            else if (IsRotatingThroughOpponent(player, state)) {
                return config.rotationReductionFactor;
            }
            return 1.0f;  // Full penalty in other contexts
        }

        float CalculateBoostConsumption(float currentBoost, float previousBoost) const {
            return std::max(0.0f, previousBoost - currentBoost);
        }

        float CalculateSpeedGain(float currentSpeed, float previousSpeed) const {
            return std::max(0.0f, currentSpeed - previousSpeed);
        }

        float EvaluateBoostEfficiency(
            float consumption,
            float speedGain,
            bool isAerial,
            bool isSupertronic) const {

            // Avoid division by zero
            if (consumption < 1.0f) return 0.0f;

            // Base inefficiency calculation
            float efficiency = speedGain / consumption;
            float efficiencyTarget = config.minBoostEfficiency;

            // Penalize if below minimum efficiency
            if (efficiency < efficiencyTarget) {
                float inefficiencyFactor = (efficiencyTarget - efficiency) / efficiencyTarget;

                float penalty = config.basePenaltyPerSecond * inefficiencyFactor;

                // Multipliers for worse waste scenarios
                if (isSupertronic) {
                    penalty *= config.supersonicWasteMultiplier;
                }
                else if (isAerial) {
                    penalty *= config.aerialWasteMultiplier;
                }

                return penalty;
            }

            return 0.0f;  // No penalty if efficient
        }

    public:
        BoostWastePenaltyReward() : config() {}

        explicit BoostWastePenaltyReward(const WasteConfig& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            previousBoost.clear();
            previousSpeed.clear();
            wasteAccumulator.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!config.enablePenalty) return 0.0f;

            float currentBoost = player.boost;
            float currentSpeed = player.vel.Length();

            // Initialize if first frame
            if (previousBoost.find(player.carId) == previousBoost.end()) {
                previousBoost[player.carId] = currentBoost;
                previousSpeed[player.carId] = currentSpeed;
                wasteAccumulator[player.carId] = 0.0f;
                return 0.0f;
            }

            float prev_boost = previousBoost[player.carId];
            float prev_speed = previousSpeed[player.carId];

            // Calculate this frame's metrics
            float boostConsumed = CalculateBoostConsumption(currentBoost, prev_boost);
            float speedGain = CalculateSpeedGain(currentSpeed, prev_speed);

            // Determine situation
            bool isAerial = !player.isOnGround;
            bool isSupertronic = currentSpeed > CommonValues::SUPERSONIC_THRESHOLD;
            bool inContestablePosition = IsInContestableBallPosition(player, state);

            // If boost consumption is low, no waste penalty
            if (boostConsumed < 1.0f) {
                previousBoost[player.carId] = currentBoost;
                previousSpeed[player.carId] = currentSpeed;
                return 0.0f;
            }

            // Only penalize waste if we're NOT in a contestable position
            if (inContestablePosition) {
                previousBoost[player.carId] = currentBoost;
                previousSpeed[player.carId] = currentSpeed;
                return 0.0f;  // Boost spent on contested ball is always justified
            }

            // Evaluate efficiency of this boost consumption
            float efficiencyPenalty = EvaluateBoostEfficiency(
                boostConsumed,
                speedGain,
                isAerial,
                isSupertronic
            );

            // Apply mitigation factor based on game state
            float mitigationFactor = GetMitigationFactor(player, state);
            float finalPenalty = efficiencyPenalty * mitigationFactor;

            // Track for metrics
            wasteAccumulator[player.carId] += std::abs(finalPenalty);

            // Update history
            previousBoost[player.carId] = currentBoost;
            previousSpeed[player.carId] = currentSpeed;

            return finalPenalty;
        }

        // Configuration setters
        void SetPenaltyMagnitude(float base, float aerial, float supersonic) {
            config.basePenaltyPerSecond = base;
            config.aerialWasteMultiplier = aerial;
            config.supersonicWasteMultiplier = supersonic;
        }

        void SetThresholds(float consumption, float damping, float efficiency) {
            config.highBoostConsumptionRate = consumption;
            config.boostDampingThreshold = damping;
            config.minBoostEfficiency = efficiency;
        }

        void SetBallContestThresholds(float distance, float height) {
            config.ballContestDistance = distance;
            config.ballContestHeight = height;
        }

        void SetMitigationFactors(float defensive, float dribble, float rotation) {
            config.defensiveReductionFactor = defensive;
            config.dribbleReductionFactor = dribble;
            config.rotationReductionFactor = rotation;
        }

        void SetAllConfig(
            float basePenalty,
            float aerialMult,
            float supersonicMult,
            float defMit,
            float driMit,
            float rotMit,
            float efficiency) {

            config.basePenaltyPerSecond = basePenalty;
            config.aerialWasteMultiplier = aerialMult;
            config.supersonicWasteMultiplier = supersonicMult;
            config.defensiveReductionFactor = defMit;
            config.dribbleReductionFactor = driMit;
            config.rotationReductionFactor = rotMit;
            config.minBoostEfficiency = efficiency;
        }

        void SetEnable(bool enable) {
            config.enablePenalty = enable;
        }

        // Metrics
        float GetWasteAccumulation(const Player& player) const {
            auto it = wasteAccumulator.find(player.carId);
            if (it == wasteAccumulator.end()) return 0.0f;
            return it->second;
        }
    };

    // ============================================================================
    // BoostStarvationReward - 1V1 SPECIFIC
    // Incentivizes denying opponent boost to control game pace
    // 
    // Strategy: Take opponent's corner/mid boosts during possession/rotation
    // to starve them and prevent high-speed challenges
    // 
    // 2025-12-28 | v1.1 | NJH (FIXED API)
    // ============================================================================
    class BoostStarvationReward : public Reward {
    private:
        struct StarvationConfig {
            // Boost threshold below which opponent is "starved"
            float starvedThreshold = 50.0f;      // Below 50: opponent cannot challenge effectively

            // Reward magnitude
            float rewardPerSecondStarved = 0.5f; // Continuous reward while opponent is starved

            // Multiplier based on game state
            float possessionMultiplier = 1.5f;   // More valuable when we have ball
            float rotationMultiplier = 1.2f;     // Still valuable during rotation through their side

            // Location weights for boost pad denial
            float cornerBoostValue = 1.0f;       // High value targets
            float midBoostValue = 0.8f;
            float backBoostValue = 0.5f;

            // Prevent spam - only reward once per pickup
            bool usePickupTracking = true;
        };

        mutable std::map<uint32_t, float> lastOpponentBoost;  // Track opponent boost level
        mutable std::map<uint32_t, bool> wasStarvedLastFrame;
        mutable std::map<uint32_t, float> starvationRewardAccum;

        StarvationConfig config;

        bool IsPlayerInPossession(const Player& player, const GameState& state) const {
            // Check if player is within ball control range
            float ballDist = player.pos.Dist(state.ball.pos);
            return ballDist < 250.0f;  // Close enough for possession
        }

        bool IsPlayerRotatingThroughOpponentSide(const Player& player, const GameState& state) const {
            // Check if player is on opponent side moving toward goal
            bool onOpponentSide = (player.team == Team::BLUE && player.pos.y > 0) ||
                (player.team == Team::ORANGE && player.pos.y < 0);

            if (!onOpponentSide) return false;

            // Check if moving toward opponent goal
            Vec goalPos = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_LINE_CENTER :
                CommonValues::BLUE_GOAL_LINE_CENTER;

            Vec toGoal = goalPos - player.pos;
            float forwardVel = player.vel.Dot(toGoal.Normalized());

            return forwardVel > 200.0f;  // Moving toward goal
        }

        float GetGameStateMultiplier(const Player& player, const GameState& state) const {
            if (IsPlayerInPossession(player, state)) {
                return config.possessionMultiplier;
            }
            else if (IsPlayerRotatingThroughOpponentSide(player, state)) {
                return config.rotationMultiplier;
            }
            return 1.0f;  // Baseline
        }

        float EvaluateOpponentBoostLevel(const Player& opponent) const {
            // How starved is opponent? Lower = more starved = more reward
            if (opponent.boost < config.starvedThreshold) {
                // Exponential reward as they drop below threshold
                float deficit = config.starvedThreshold - opponent.boost;
                float starvationFactor = deficit / config.starvedThreshold;  // 0-1 scale
                return starvationFactor * config.rewardPerSecondStarved;
            }
            return 0.0f;
        }

    public:
        BoostStarvationReward() : config() {}

        explicit BoostStarvationReward(const StarvationConfig& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            lastOpponentBoost.clear();
            wasStarvedLastFrame.clear();
            starvationRewardAccum.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Find opponent with different team
            const Player* opponent = nullptr;
            for (const auto& p : state.players) {
                if (p.team != player.team) {  // Opposite team
                    opponent = &p;
                    break;
                }
            }

            if (!opponent) return 0.0f;  // No opponent to starve

            float gameStateMultiplier = GetGameStateMultiplier(player, state);
            float starvationReward = EvaluateOpponentBoostLevel(*opponent) * gameStateMultiplier;

            // Track continuous starvation for debugging
            if (starvationReward > 0) {
                starvationRewardAccum[player.carId] += starvationReward;
                wasStarvedLastFrame[player.carId] = true;
            }
            else {
                wasStarvedLastFrame[player.carId] = false;
            }

            return starvationReward;
        }

        // Configuration setters
        void SetStarvedThreshold(float threshold) {
            config.starvedThreshold = threshold;
        }

        void SetRewardPerSecondStarved(float reward) {
            config.rewardPerSecondStarved = reward;
        }

        void SetMultipliers(float possession, float rotation) {
            config.possessionMultiplier = possession;
            config.rotationMultiplier = rotation;
        }

        void SetAllConfig(float threshold, float rewardPerSec, float posMult, float rotMult) {
            config.starvedThreshold = threshold;
            config.rewardPerSecondStarved = rewardPerSec;
            config.possessionMultiplier = posMult;
            config.rotationMultiplier = rotMult;
        }

        // Debug/metrics
        float GetOpponentStarvationLevel(const GameState& state, const Player& player) const {
            const Player* opponent = nullptr;
            for (const auto& p : state.players) {
                if (p.team != player.team) {
                    opponent = &p;
                    break;
                }
            }
            if (!opponent) return 0.0f;
            return (config.starvedThreshold - opponent->boost) / config.starvedThreshold;
        }
    };
} // namespace RLGC