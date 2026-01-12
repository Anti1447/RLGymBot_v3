/*
 * File: BoostManagementRewards.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Comprehensive boost management reward functions for all game modes.
 *   Covers pad pickups (small/big with accuracy), boost efficiency, kickoff
 *   retention, wall dashing, supersonic mechanics, boost waste penalties,
 *   and 1v1 starvation tactics. Uses RocketSim math and structures.
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
#include <atomic>
#include "RewardMath.h"
#include <cmath>
#include "../CommonValues.h"

namespace RLGC {

    // ============================================================================
    // BOOST EFFICIENCY REWARD
    // Rewards productive boost usage (high speed, ball contact, aerial play)
    // ============================================================================
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
    // LOW BOOST OPPONENT HALF FIFTY PENALTY
    // Penalizes 50/50 challenges with low boost (<25) in opponent half.
    // Max punishment in opponent corners (highest concede risk).
    // ============================================================================
    class LowBoostOppHalfFiftyPunish : public Reward {
    private:
        float lowBoostThresh = 25.0f;
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
            const float xAbs = fabs(ballPos.x);
            const float yAbs = fabs(ballPos.y);

            float xFrac = RS_CLAMP((xAbs - 2500.0f) / (CommonValues::SIDE_WALL_X - 2500.0f), 0.0f, 1.0f);
            float yFrac = RS_CLAMP((yAbs - 3500.0f) / (CommonValues::BACK_WALL_Y - 3500.0f), 0.0f, 1.0f);

            return RS_CLAMP(xFrac * yFrac, 0.0f, 1.0f);
        }

    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!IsFiftyFiftyTouch(player, state)) return 0.0f;
            if (player.boost >= lowBoostThresh) return 0.0f;

            const bool ballInOwnHalf = (player.team == Team::BLUE) ?
                (state.ball.pos.y <= 0.0f) : (state.ball.pos.y >= 0.0f);
            if (ballInOwnHalf) return 0.0f;

            float corner = OppCornerFactor(player, state.ball.pos);
            return minPunish + (maxPunish - minPunish) * corner;
        }
    };

    // ============================================================================
    // SMALL PAD COLLECTION REWARD
    // Reward consecutive small pad pickups in quick succession.
    // Escalates with sequences.
    // ============================================================================
    class SmallPadCollectionReward : public Reward {
    private:
        mutable std::map<uint32_t, std::deque<float>> recentBoostPickups;
        static constexpr float SEQUENCE_WINDOW = 3.0f;  // seconds
        static constexpr float SMALL_PAD_MAX = 15.0f;   // 12 boost pads

    public:
        virtual void Reset(const GameState& initialState) override {
            recentBoostPickups.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!state.prev) return 0.0f;

            float prevBoost = state.prev->players[player.index].boost;
            float curBoost = player.boost;
            float boostGain = curBoost - prevBoost;

            if (boostGain > 0.0f && boostGain < SMALL_PAD_MAX) {
                // Picked up small pad
                auto& pickups = recentBoostPickups[player.carId];
                pickups.push_back(state.deltaTime);

                // Keep only recent pickups
                while (!pickups.empty() && (state.deltaTime - pickups.front()) > SEQUENCE_WINDOW) {
                    pickups.pop_front();
                }

                // Escalating reward for sequences
                if (pickups.size() >= 3) {
                    return 0.2f * pickups.size();  // 0.6f for 3 pads, 0.8f for 4, etc.
                }
                else if (pickups.size() >= 2) {
                    return 0.15f;  // 2 pads in sequence
                }

                return 0.05f;  // Single small pad
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // DRIBBLE BOOST INEFFICIENCY PUNISHMENT
    // Punishes boosting while dribbling slowly (<1300 UU/s) without speed gain.
    // Encourages: boost to accelerate past 1500 UU/s, or setup flick at 1410+ UU/s
    // ============================================================================
    class DribbleBoostInefficencyPunish : public Reward {
    private:
        static constexpr float DRIBBLE_SPEED_THRESHOLD = 1300.0f;  // UU/s
        static constexpr float FLICK_READY_SPEED = 1410.0f;        // UU/s
        static constexpr float TARGET_BOOST_SPEED = 1500.0f;       // UU/s
        static constexpr float DRIBBLE_DISTANCE = 300.0f;          // Close to ball = dribbling
        static constexpr float PENALTY = -0.08f;                   // Small, incremental penalty

        mutable std::map<uint32_t, float> previousSpeed;

    public:
        virtual void Reset(const GameState& initialState) override {
            previousSpeed.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float speed = player.vel.Length();
            float ballDist = player.pos.Dist(state.ball.pos);
            bool isDribbling = ballDist < DRIBBLE_DISTANCE && player.isOnGround;

            // Only penalize if dribbling
            if (!isDribbling) {
                previousSpeed[player.carId] = speed;
                return 0.0f;
            }

            // OK to dribble slowly if setting up flick (speed >= 1410)
            if (speed >= FLICK_READY_SPEED) {
                previousSpeed[player.carId] = speed;
                return 0.0f;
            }

            // Penalize boosting while slow dribbling (< 1300) without acceleration
            bool isBoosting = (player.prevAction.boost != 0);
            if (!isBoosting) {
                previousSpeed[player.carId] = speed;
                return 0.0f;
            }

            // Check for speed gain
            float prevSpeed = previousSpeed[player.carId];
            float speedGain = speed - prevSpeed;

            // If accelerating toward target (1500), no penalty
            if (speedGain > 50.0f && speed < TARGET_BOOST_SPEED) {
                previousSpeed[player.carId] = speed;
                return 0.0f;  // Good boost usage, accelerating
            }

            // Penalty: boosting without meaningful speed gain while dribbling slow
            previousSpeed[player.carId] = speed;
            return PENALTY;
        }
    };

    // ============================================================================
    // BIG BOOST PAD CENTER ACCURACY REWARD
    // High reward for driving over pad center (100 boost pad pickup).
    // Scales down with distance from center.
    // ============================================================================
    class BigBoostPadCenterReward : public Reward {
    private:
        const std::vector<Vec> bigBoostPads = {
            Vec(-3072.0f, -4096.0f, 73.0f),
            Vec(3072.0f, -4096.0f, 73.0f),
            Vec(-3584.0f, 0.0f, 73.0f),
            Vec(3584.0f, 0.0f, 73.0f),
            Vec(-3072.0f, 4096.0f, 73.0f),
            Vec(3072.0f, 4096.0f, 73.0f)
        };

        mutable std::map<uint32_t, float> lastBoostAmount;

        static constexpr float BIG_PAD_THRESHOLD = 90.0f;  // Detect 100 boost pickup
        static constexpr float CENTER_RADIUS = 28.0f;      // Lock-on radius (optimal)
        static constexpr float BOX_WIDTH = 160.0f;         // Collection boundary
        static constexpr float MAX_REWARD = 1.0f;          // Perfect center hit
        static constexpr float MIN_REWARD = 0.2f;          // Edge of box

    public:
        virtual void Reset(const GameState& initialState) override {
            lastBoostAmount.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (lastBoostAmount.find(player.carId) == lastBoostAmount.end()) {
                lastBoostAmount[player.carId] = player.boost;
                return 0.0f;
            }

            float prevBoost = lastBoostAmount[player.carId];
            float boostGain = player.boost - prevBoost;

            // Detect big pad pickup (100 boost)
            if (boostGain < BIG_PAD_THRESHOLD) {
                lastBoostAmount[player.carId] = player.boost;
                return 0.0f;
            }

            // Find closest big pad
            float closestDist = std::numeric_limits<float>::max();
            for (const auto& pad : bigBoostPads) {
                float dist = pad.Dist(player.pos);
                if (dist < closestDist) {
                    closestDist = dist;
                }
            }

            // Score based on distance from pad center
            float reward = 0.0f;

            if (closestDist <= CENTER_RADIUS) {
                // Perfect center hit
                reward = MAX_REWARD;
            }
            else if (closestDist <= BOX_WIDTH) {
                // Scale down from center to edge
                float ratio = 1.0f - (closestDist - CENTER_RADIUS) / (BOX_WIDTH - CENTER_RADIUS);
                reward = MIN_REWARD + (MAX_REWARD - MIN_REWARD) * ratio;
            }
            else {
                // Shouldn't happen, but small reward for pickup
                reward = 0.1f;
            }

            lastBoostAmount[player.carId] = player.boost;
            return reward;
        }
    };

    // ============================================================================
    // LOW BOOST DEFENSE REWARD
    // Rewards defending goal with low boost (<30).
    // Only triggers when positioned between ball and goal.
    // ============================================================================
    class LowBoostDefenseReward : public Reward {
    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.boost > 30.0f) return 0.0f;

            Vec myGoal = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            float distToGoal = (player.pos - myGoal).Length();
            float ballDistToGoal = (state.ball.pos - myGoal).Length();

            if (distToGoal < ballDistToGoal && ballDistToGoal < 3000.0f) {
                return 2.0f;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // BOOST PATH REWARD
    // Rewards efficient boost pad collection by tracking path efficiency.
    // Short distances between consecutive pads = optimal routing.
    // ============================================================================
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
                Vec padPos = player.pos;

                auto& path = recentPadPositions[player.carId];
                path.push_back(padPos);

                if (path.size() > 5) {
                    path.pop_front();
                }

                if (path.size() >= 3) {
                    float totalDist = 0.0f;
                    for (size_t i = 1; i < path.size(); i++) {
                        totalDist += path[i].Dist(path[i - 1]);
                    }

                    float avgDist = totalDist / (path.size() - 1);

                    if (avgDist < 1500.0f) {
                        return 5.0f;
                    }
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // LOW BOOST OFFENSE REWARD
    // Rewards attacking/ball contact with low boost (<30).
    // Encourages making plays without relying on boost.
    // ============================================================================
    class LowBoostOffenseReward : public Reward {
    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.boost > 30.0f) return 0.0f;

            Vec theirGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

            float ballDistToGoal = (state.ball.pos - theirGoal).Length();
            float myDistToBall = (player.pos - state.ball.pos).Length();

            if (ballDistToGoal < 3000.0f && myDistToBall < 1500.0f) {
                if (player.ballTouchedStep) {
                    return 8.0f;
                }
                return 2.0f;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // BOOST STEAL REWARD
    // Rewards picking up big boost pads on opponent's side.
    // Bonus for corner pads and desperation steals (low own boost).
    // ============================================================================
    class BoostStealReward : public Reward {
    private:
        const std::vector<Vec> bigBoostPads = {
            Vec(-3072.0f, -4096.0f, 73.0f),
            Vec(3072.0f, -4096.0f, 73.0f),
            Vec(-3584.0f, 0.0f, 73.0f),
            Vec(3584.0f, 0.0f, 73.0f),
            Vec(-3072.0f, 4096.0f, 73.0f),
            Vec(3072.0f, 4096.0f, 73.0f)
        };

        mutable std::map<uint32_t, float> lastBoostAmounts;

        bool IsOpposingSide(const Vec& padPos, Team team) const {
            if (team == Team::BLUE) {
                return padPos.y > 1000.0f;
            }
            else {
                return padPos.y < -1000.0f;
            }
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            lastBoostAmounts.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float reward = 0.0f;

            if (lastBoostAmounts.find(player.carId) == lastBoostAmounts.end()) {
                lastBoostAmounts[player.carId] = player.boost;
                return 0.0f;
            }

            float boostGained = player.boost - lastBoostAmounts[player.carId];

            if (boostGained > 90.0f) {
                for (const auto& pad : bigBoostPads) {
                    float dist = pad.Dist(player.pos);

                    if (dist < 200.0f) {
                        if (IsOpposingSide(pad, player.team)) {
                            reward += 0.4f;

                            bool cornerPad = abs(pad.y) > 3500.0f && abs(pad.x) > 2500.0f;
                            if (cornerPad) {
                                reward += 0.3f;
                            }

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
    // Scales linearly with INVERSE boost amount.
    // More reward when boost is lower (encourages conservation).
    // ============================================================================
    class SaveBoostReward : public Reward {
    public:
        float exponent;
        SaveBoostReward(float exponent = 1.0f) : exponent(exponent) {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float boostFraction = player.boost / 100.0f;
            float inverseBoost = 1.0f - boostFraction;
            return RS_CLAMP(powf(inverseBoost, exponent), 0.0f, 1.0f);
        }
    };

    // ============================================================================
    // DISCOURAGE FLIPPING LOW BOOST REWARD
    // Penalizes flipping when both boost and speed are low.
    // ============================================================================
    class DiscourageFlippingLowBoostReward : public Reward {
    private:
        mutable std::map<uint32_t, bool> wasFlipping;

        static constexpr float SPEED_THRESHOLD = 1300.0f;
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

            if (currentlyFlipping && !previouslyFlipping) {
                float speed = player.vel.Length();
                float boost = player.boost;

                if (speed < SPEED_THRESHOLD && boost < BOOST_THRESHOLD) {
                    return PENALTY;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // SUPERSONIC REWARD
    // Rewards maintaining supersonic speed (2300+ uu/s).
    // Base reward + duration bonus for sustained supersonic.
    // ============================================================================
    class SupersonicReward : public Reward {
    private:
        mutable std::map<uint32_t, float> supersonicDuration;

        inline static std::atomic<long long> s_supersonicTimeMilli{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };

        static constexpr float SUPERSONIC_THRESHOLD = 2300.0f;
        static constexpr float BASE_REWARD_PER_SECOND = 0.5f;
        static constexpr float MAX_DURATION_BONUS = 2.0f;
        static constexpr float DURATION_SCALE = 5.0f;

    public:
        struct SupersonicMetrics {
            double supersonicTime = 0.0;
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
                duration += state.deltaTime;

                float baseReward = BASE_REWARD_PER_SECOND * state.deltaTime;
                float durationBonus = RS_MIN(duration / DURATION_SCALE, 1.0f) * MAX_DURATION_BONUS;
                float sustainBonus = durationBonus * state.deltaTime;

                float totalReward = baseReward + sustainBonus;

                s_supersonicTimeMilli.fetch_add(static_cast<long long>(state.deltaTime * 1000.0f));
                s_rewardMilli.fetch_add(static_cast<long long>(totalReward * 1000.0f));

                return totalReward;
            }
            else {
                duration = 0.0f;
                return 0.0f;
            }
        }
    };

    // ============================================================================
    // SUPERSONIC BOOST PENALTY
    // Penalizes boosting when already supersonic on ground.
    // ============================================================================
    class SupersonicBoostPenalty : public Reward {
    private:
        static constexpr float SUPERSONIC_THRESHOLD = 2300.0f;
        static constexpr float PENALTY_PER_TICK = -0.05f;

    public:
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (!player.isOnGround) {
                return 0.0f;
            }

            float speed = player.vel.Length();
            if (speed < SUPERSONIC_THRESHOLD) {
                return 0.0f;
            }

            bool isBoosting = (player.prevAction.boost != 0);
            if (!isBoosting) {
                return 0.0f;
            }

            return PENALTY_PER_TICK;
        }
    };

    // ============================================================================
    // BOOST WASTE PENALTY REWARD
    // Penalizes inefficient boost consumption (low speed gain per boost spent).
    // Context-aware: mitigates penalty during defense, dribbles, and contested plays.
    // ============================================================================
    class BoostWastePenaltyReward : public Reward {
    private:
        struct WasteConfig {
            float basePenaltyPerSecond = -1.0f;
            float aerialWasteMultiplier = 1.5f;
            float supersonicWasteMultiplier = 2.0f;
            float highBoostConsumptionRate = 25.0f;
            float boostDampingThreshold = 100.0f;
            float minBoostEfficiency = 0.3f;
            float ballContestDistance = 1500.0f;
            float ballContestHeight = 300.0f;
            float defensiveReductionFactor = 0.5f;
            float dribbleReductionFactor = 0.3f;
            float rotationReductionFactor = 0.7f;
            bool enablePenalty = true;
        };

        mutable std::map<uint32_t, float> previousBoost;
        mutable std::map<uint32_t, float> previousSpeed;
        mutable std::map<uint32_t, float> wasteAccumulator;

        WasteConfig config;

        bool IsInContestableBallPosition(const Player& player, const GameState& state) const {
            float ballDist = player.pos.Dist(state.ball.pos);
            float ballHeight = state.ball.pos.z;

            bool isClose = ballDist < config.ballContestDistance;
            bool isReachable = ballHeight < 400.0f;

            return isClose && isReachable;
        }

        bool IsDefending(const Player& player, const GameState& state) const {
            float ownGoalY = (player.team == Team::BLUE) ? -4000.0f : 4000.0f;
            return abs(player.pos.y - ownGoalY) < 2500.0f;
        }

        bool IsDribbling(const Player& player, const GameState& state) const {
            float ballDist = player.pos.Dist(state.ball.pos);
            return ballDist < 250.0f &&
                player.isOnGround &&
                player.vel.Length() < 1000.0f;
        }

        bool IsRotatingThroughOpponent(const Player& player, const GameState& state) const {
            bool onOpponentSide = (player.team == Team::BLUE && player.pos.y > 1000.0f) ||
                (player.team == Team::ORANGE && player.pos.y < -1000.0f);

            float speed = player.vel.Length();
            return onOpponentSide && speed < 1800.0f;
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
            return 1.0f;
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

            if (consumption < 1.0f) return 0.0f;

            float efficiency = speedGain / consumption;
            float efficiencyTarget = config.minBoostEfficiency;

            if (efficiency < efficiencyTarget) {
                float inefficiencyFactor = (efficiencyTarget - efficiency) / efficiencyTarget;
                float penalty = config.basePenaltyPerSecond * inefficiencyFactor;

                if (isSupertronic) {
                    penalty *= config.supersonicWasteMultiplier;
                }
                else if (isAerial) {
                    penalty *= config.aerialWasteMultiplier;
                }

                return penalty;
            }

            return 0.0f;
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

            if (previousBoost.find(player.carId) == previousBoost.end()) {
                previousBoost[player.carId] = currentBoost;
                previousSpeed[player.carId] = currentSpeed;
                wasteAccumulator[player.carId] = 0.0f;
                return 0.0f;
            }

            float prev_boost = previousBoost[player.carId];
            float prev_speed = previousSpeed[player.carId];

            float boostConsumed = CalculateBoostConsumption(currentBoost, prev_boost);
            float speedGain = CalculateSpeedGain(currentSpeed, prev_speed);

            bool isAerial = !player.isOnGround;
            bool isSupertronic = currentSpeed > CommonValues::SUPERSONIC_THRESHOLD;
            bool inContestablePosition = IsInContestableBallPosition(player, state);

            if (boostConsumed < 1.0f) {
                previousBoost[player.carId] = currentBoost;
                previousSpeed[player.carId] = currentSpeed;
                return 0.0f;
            }

            if (inContestablePosition) {
                previousBoost[player.carId] = currentBoost;
                previousSpeed[player.carId] = currentSpeed;
                return 0.0f;
            }

            float efficiencyPenalty = EvaluateBoostEfficiency(
                boostConsumed,
                speedGain,
                isAerial,
                isSupertronic
            );

            float mitigationFactor = GetMitigationFactor(player, state);
            float finalPenalty = efficiencyPenalty * mitigationFactor;

            wasteAccumulator[player.carId] += std::abs(finalPenalty);

            previousBoost[player.carId] = currentBoost;
            previousSpeed[player.carId] = currentSpeed;

            return finalPenalty;
        }

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

        float GetWasteAccumulation(const Player& player) const {
            auto it = wasteAccumulator.find(player.carId);
            if (it == wasteAccumulator.end()) return 0.0f;
            return it->second;
        }
    };

    // ============================================================================
    // BOOST MANAGEMENT REWARD (Advanced Techniques)
    // Tracks and rewards: pad pickups, boost usage context, kickoff retention,
    // wall dashing, feathering, recoveries, and low-boost discipline.
    // Uses RocketSim math/structures.
    // ============================================================================
    class AdvancedBoostManagementReward : public Reward {
    private:
        struct BoostManagementConfig {
            float ball_speed_threat = 1500.0f;
            float opponent_distance_threat = 2000.0f;
            float consecutive_pad_timeout = 5.0f;
            float aerial_pitch_threshold = 0.785f;  // ~45 degrees in radians
            float supersonic_speed = 2300.0f;
            float big_pad_lock_on_radius = 28.0f;
            float big_pad_box_width = 160.0f;
            float small_pad_lock_on_radius = 144.0f;
            float small_pad_box_width = 96.0f;
            float wall_dash_min_speed = 2000.0f;
            float wall_dash_max_pitch = 0.262f;    // ~15 degrees in radians
            float wall_dash_air_roll_threshold = 0.8f;
            float wall_dash_jump_spam_rate = 0.05f;
            float kickoff_target_boost = 11.0f;
            float low_boost_threshold = 25.0f;
            float feather_boost_speed = 1500.0f;
        };

        struct BoostManagementState {
            float last_small_pad_time = 0.0f;
            int consecutive_small_pads = 0;
            float last_big_pad_pickup_time = 0.0f;
            bool recent_big_pad_grab = false;
            float boost_usage_timestamp = 0.0f;
            int big_pad_lock_on_count = 0;
            // Wall dash
            float wall_dash_start_time = 0.0f;
            bool in_wall_dash_attempt = false;
            int consecutive_jumps = 0;
            float last_jump_time = 0.0f;
            float wall_dash_speed_peak = 0.0f;
        };

        mutable std::map<uint32_t, BoostManagementState> playerStates;
        mutable std::map<uint32_t, float> lastBoostAmount;
        BoostManagementConfig config;
        float current_time = 0.0f;

        bool is_threatening_situation(const Player& player, const GameState& state) const {
            for (const auto& opponent : state.players) {
                if (opponent.team == player.team) continue;
                float opp_to_ball = opponent.pos.Dist(state.ball.pos);
                if (opp_to_ball < config.opponent_distance_threat) {
                    Vec toGoal = player.pos - opponent.pos;
                    if (opponent.vel.Dot(toGoal.Normalized()) > 0) {
                        return true;
                    }
                }
            }

            if (state.ball.vel.Length() > config.ball_speed_threat) {
                float ball_to_player = state.ball.pos.Dist(player.pos);
                if (ball_to_player < 3000.0f && state.ball.vel.Dot(player.pos - state.ball.pos) > 0) {
                    return true;
                }
            }

            return false;
        }

        bool is_ball_pursuit_boost(const Player& player, const GameState& state) const {
            Vec to_ball = state.ball.pos - player.pos;
            float distance = to_ball.Length();

            if (distance > 500.0f && distance < 5000.0f) {
                if (player.vel.Length() > state.ball.vel.Length()) {
                    if (player.vel.Dot(to_ball) > 0) {
                        return true;
                    }
                }
            }
            return false;
        }

        bool is_minor_adjustment(const Player& player) const {
            if (player.vel.Length() < 1000.0f && player.isOnGround) {
                float angular_mag = player.angVel.Length();
                if (angular_mag < 2.0f) {
                    return true;
                }
            }
            return false;
        }

    public:
        AdvancedBoostManagementReward() : config() {}

        explicit AdvancedBoostManagementReward(const BoostManagementConfig& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
            lastBoostAmount.clear();
            current_time = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            current_time += state.deltaTime;

            if (playerStates.find(player.carId) == playerStates.end()) {
                playerStates[player.carId] = BoostManagementState();
                lastBoostAmount[player.carId] = player.boost;
            }

            auto& pState = playerStates[player.carId];
            float reward = 0.0f;

            // Update consecutive small pad state
            if (current_time - pState.last_small_pad_time > config.consecutive_pad_timeout) {
                pState.consecutive_small_pads = 0;
            }

            // Track pad pickups
            if (!state.prev) {
                lastBoostAmount[player.carId] = player.boost;
                return 0.0f;
            }

            float prevBoost = lastBoostAmount[player.carId];
            float boostGain = player.boost - prevBoost;

            if (boostGain > 0.0f && boostGain < 15.0f) {
                // Small pad pickup
                pState.consecutive_small_pads++;
                pState.last_small_pad_time = current_time;

                reward += 0.2f; // Base reward
                if (pState.consecutive_small_pads >= 2) {
                    reward += 0.1f * (pState.consecutive_small_pads - 1); // Consecutive bonus
                }
            }
            else if (boostGain > 90.0f) {
                // Big pad pickup
                pState.recent_big_pad_grab = true;
                pState.last_big_pad_pickup_time = current_time;
                reward += 0.5f;
            }

            // Boost usage efficiency
            // Convert RotMat to Angle to get pitch
            RocketSim::Angle carAngle = RocketSim::Angle::FromRotMat(player.rotMat);
            bool is_aerial = std::abs(carAngle.pitch) > config.aerial_pitch_threshold;
            bool is_supersonic = player.vel.Length() >= config.supersonic_speed;

            if (is_supersonic && player.isOnGround && player.prevAction.boost != 0) {
                reward -= 0.2f; // Penalize supersonic ground boost
            }

            bool in_threat = is_threatening_situation(player, state);
            bool pursuing = is_ball_pursuit_boost(player, state);

            if (in_threat || is_aerial || pursuing) {
                reward += 0.3f;
            }
            else if (is_minor_adjustment(player)) {
                reward -= 0.1f;
            }

            // Low boost discipline
            if (!in_threat && player.boost < config.low_boost_threshold) {
                reward += 0.05f;
            }

            lastBoostAmount[player.carId] = player.boost;
            return reward;
        }

        void SetConfig(const BoostManagementConfig& cfg) {
            config = cfg;
        }
    };

    // ============================================================================
    // WALL DASH REWARD (Lenient)
    // Rewards wall dash technique with intermediate rewards for partial success.
    // ============================================================================
    class WallDashReward : public Reward {
    private:
        struct WallDashState {
            bool on_wall_last_frame = false;
            int consecutive_jumps = 0;
            float last_jump_time = 0.0f;
            float wall_entry_speed = 0.0f;
            bool giving_reward_this_attempt = false;
        };

        mutable std::map<uint32_t, WallDashState> playerStates;

        // Lenient thresholds
        static constexpr float WALL_TILT_MIN = 0.087f;     // ~5 degrees (very lenient)
        static constexpr float WALL_PITCH_MAX = 0.349f;    // ~20 degrees (very lenient)
        static constexpr float WALL_DASH_MIN_SPEED = 1800.0f;
        static constexpr float JUMP_SPAM_RATE = 0.15f;     // 150ms window (3 frames at 120 Hz)
        static constexpr float SPEED_GAIN_THRESHOLD = 1.05f;  // Only 5% speed gain needed

        bool is_on_wall_lenient(const Player& player) const {
            if (!player.isOnGround) return false;

            RocketSim::Angle carAngle = RocketSim::Angle::FromRotMat(player.rotMat);
            float pitch = std::abs(carAngle.pitch);
            float roll = std::abs(carAngle.roll);

            // Very lenient: any tilt counts
            return (pitch > WALL_TILT_MIN || roll > WALL_TILT_MIN) && player.isOnGround;
        }

        bool is_car_aligned_to_wall(const Player& player) const {
            RocketSim::Angle carAngle = RocketSim::Angle::FromRotMat(player.rotMat);
            return std::abs(carAngle.pitch) < WALL_PITCH_MAX;
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
        }

        void register_jump(const Player& player, float currentTime) {
            if (playerStates.find(player.carId) == playerStates.end()) {
                playerStates[player.carId] = WallDashState();
            }

            auto& wState = playerStates[player.carId];

            if (currentTime - wState.last_jump_time < JUMP_SPAM_RATE) {
                wState.consecutive_jumps++;
            }
            else {
                wState.consecutive_jumps = 1;
            }

            wState.last_jump_time = currentTime;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (playerStates.find(player.carId) == playerStates.end()) {
                playerStates[player.carId] = WallDashState();
            }

            auto& wState = playerStates[player.carId];
            bool on_wall = is_on_wall_lenient(player);
            bool aligned = is_car_aligned_to_wall(player);
            float speed = player.vel.Length();
            bool high_speed = speed > WALL_DASH_MIN_SPEED;

            float reward = 0.0f;

            // Intermediate reward 1: Just being on wall with decent alignment
            if (on_wall && aligned && high_speed && !wState.on_wall_last_frame) {
                reward += 0.1f;  // Small reward for entering wall dash state
                wState.wall_entry_speed = speed;
            }

            // Intermediate reward 2: Jump attempts on wall
            if (on_wall && aligned && wState.consecutive_jumps >= 1) {
                reward += 0.05f * std::min(wState.consecutive_jumps, 5);  // Reward up to 5 jumps
            }

            // Final reward: Sustained speed or speed gain
            if (on_wall && speed >= wState.wall_entry_speed * SPEED_GAIN_THRESHOLD) {
                reward += 0.15f;  // Maintained or gained speed
            }

            // Alignment bonus
            RocketSim::Angle carAngle = RocketSim::Angle::FromRotMat(player.rotMat);
            float pitch_offset = std::abs(carAngle.pitch);
            if (pitch_offset < 0.087f) {  // ~5 degrees
                reward += 0.08f;
            }

            wState.on_wall_last_frame = on_wall;

            return reward;
        }
    };
    // Incentivizes denying opponent boost to control game pace.
    // Continuous reward while opponent boost is below threshold.
    // ============================================================================
    class BoostStarvationReward : public Reward {
    private:
        struct StarvationConfig {
            float starvedThreshold = 50.0f;
            float rewardPerSecondStarved = 0.5f;
            float possessionMultiplier = 1.5f;
            float rotationMultiplier = 1.2f;
            float cornerBoostValue = 1.0f;
            float midBoostValue = 0.8f;
            float backBoostValue = 0.5f;
            bool usePickupTracking = true;
        };

        mutable std::map<uint32_t, float> lastOpponentBoost;
        mutable std::map<uint32_t, bool> wasStarvedLastFrame;
        mutable std::map<uint32_t, float> starvationRewardAccum;

        StarvationConfig config;

        bool IsPlayerInPossession(const Player& player, const GameState& state) const {
            float ballDist = player.pos.Dist(state.ball.pos);
            return ballDist < 250.0f;
        }

        bool IsPlayerRotatingThroughOpponentSide(const Player& player, const GameState& state) const {
            bool onOpponentSide = (player.team == Team::BLUE && player.pos.y > 0) ||
                (player.team == Team::ORANGE && player.pos.y < 0);

            if (!onOpponentSide) return false;

            Vec goalPos = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_LINE_CENTER :
                CommonValues::BLUE_GOAL_LINE_CENTER;

            Vec toGoal = goalPos - player.pos;
            float forwardVel = player.vel.Dot(toGoal.Normalized());

            return forwardVel > 200.0f;
        }

        float GetGameStateMultiplier(const Player& player, const GameState& state) const {
            if (IsPlayerInPossession(player, state)) {
                return config.possessionMultiplier;
            }
            else if (IsPlayerRotatingThroughOpponentSide(player, state)) {
                return config.rotationMultiplier;
            }
            return 1.0f;
        }

        float EvaluateOpponentBoostLevel(const Player& opponent) const {
            if (opponent.boost < config.starvedThreshold) {
                float deficit = config.starvedThreshold - opponent.boost;
                float starvationFactor = deficit / config.starvedThreshold;
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
            const Player* opponent = nullptr;
            for (const auto& p : state.players) {
                if (p.team != player.team) {
                    opponent = &p;
                    break;
                }
            }

            if (!opponent) return 0.0f;

            float gameStateMultiplier = GetGameStateMultiplier(player, state);
            float starvationReward = EvaluateOpponentBoostLevel(*opponent) * gameStateMultiplier;

            if (starvationReward > 0) {
                starvationRewardAccum[player.carId] += starvationReward;
                wasStarvedLastFrame[player.carId] = true;
            }
            else {
                wasStarvedLastFrame[player.carId] = false;
            }

            return starvationReward;
        }

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

    // ============================================================================
    // BOOST DURING FLIP REWARD
    // Bonus reward when using boost during a flip (flick/dodge).
    // Encourages aggressive playmaking.
    // ============================================================================
    class BoostDuringFlipReward : public Reward {
    private:
        mutable std::map<uint32_t, bool> wasFlipping;
        static constexpr float FLIP_BOOST_BONUS = 0.15f;

    public:
        virtual void Reset(const GameState& initialState) override {
            wasFlipping.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            bool currentlyFlipping = player.isFlipping;
            bool previouslyFlipping = wasFlipping[player.carId];
            wasFlipping[player.carId] = currentlyFlipping;

            // During flip, reward if using boost
            if (currentlyFlipping && player.prevAction.boost != 0) {
                return FLIP_BOOST_BONUS;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // POST-KICKOFF MID BOOST GRAB REWARD
    // ============================================================================
    // Rewards grabbing mid boost pads after kickoff when ball is in mid area.
    // Encourages proper post-kickoff positioning and boost collection.
    //
    // Logic:
    // - Detects kickoff end (ball moves away from center)
    // - Rewards grabbing mid boost pads if ball is in mid area
    // - Scales reward by ball proximity to mid
    // - Only rewards during post-kickoff window (3-8 seconds after kickoff)
    //
    // Mid boost pad indices: 15, 16, 17, 18 (the 4 large pads near midfield)
    // 2025-01-10 - Rev 1.0 (NJH)
    // ============================================================================
    class PostKickoffMidBoostReward : public Reward {
    private:
        mutable std::map<uint32_t, float> lastBoost;
        mutable std::map<uint32_t, bool> wasInKickoff;
        mutable std::map<uint32_t, float> timeSinceKickoffEnd;

        // Kickoff detection
        float kickoffBallPosRadius = 300.0f;
        float kickoffBallVelThreshold = 200.0f;

        // Post-kickoff window
        float minTimeAfterKickoff = 3.0f;  // Start rewarding 3s after kickoff ends
        float maxTimeAfterKickoff = 8.0f;  // Stop rewarding 8s after kickoff ends

        // Mid boost pad indices (large pads near midfield)
        static constexpr int MID_BOOST_INDICES[] = { 15, 16, 17, 18 };
        static constexpr int NUM_MID_BOOSTS = 4;

        // Reward scaling
        float baseReward = 0.5f;           // Base reward for mid boost grab
        float ballProximityBonus = 1.0f;   // Bonus when ball is in mid area
        float maxBallDistForBonus = 3000.0f; // Max distance from mid for bonus

        bool IsKickoffActive(const GameState& state) const {
            return (state.ball.pos.Length() < kickoffBallPosRadius &&
                    state.ball.vel.Length() < kickoffBallVelThreshold);
        }

        bool IsMidBoostPad(int padIndex) const {
            for (int i = 0; i < NUM_MID_BOOSTS; i++) {
                if (padIndex == MID_BOOST_INDICES[i]) {
                    return true;
                }
            }
            return false;
        }

        float GetBallDistanceFromMid(const GameState& state) const {
            // Mid is roughly at Y=0, so just check how far from Y=0
            Vec midPoint(0.0f, 0.0f, 70.0f);
            return (state.ball.pos - midPoint).Length();
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            lastBoost.clear();
            wasInKickoff.clear();
            timeSinceKickoffEnd.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            uint32_t id = player.carId;

            // Initialize tracking
            if (lastBoost.find(id) == lastBoost.end()) {
                lastBoost[id] = player.boost;
                wasInKickoff[id] = false;
                timeSinceKickoffEnd[id] = 999.0f; // Large value = not tracking
            }

            bool currentlyInKickoff = IsKickoffActive(state);

            // Detect kickoff end
            if (wasInKickoff[id] && !currentlyInKickoff) {
                // Kickoff just ended
                timeSinceKickoffEnd[id] = 0.0f;
            }

            wasInKickoff[id] = currentlyInKickoff;

            // Update timer if we're tracking post-kickoff
            if (timeSinceKickoffEnd[id] < maxTimeAfterKickoff) {
                timeSinceKickoffEnd[id] += dt;
            }

            // Check if we're in the reward window
            if (timeSinceKickoffEnd[id] < minTimeAfterKickoff ||
                timeSinceKickoffEnd[id] > maxTimeAfterKickoff) {
                lastBoost[id] = player.boost;
                return 0.0f;
            }

            // Detect boost pickup
            float boostGained = player.boost - lastBoost[id];
            lastBoost[id] = player.boost;

            // Only reward large boost pickups (100 boost)
            if (boostGained < 90.0f) {
                return 0.0f;
            }

            // Check if it was a mid boost pad
            // We need to check which boost pad was just collected
            // The player doesn't tell us which pad, so we check proximity to mid pads
            bool isMidBoostGrab = false;
            for (int i = 0; i < NUM_MID_BOOSTS; i++) {
                int padIdx = MID_BOOST_INDICES[i];
                Vec padPos = CommonValues::BOOST_LOCATIONS[padIdx];
                float distToPad = (player.pos - padPos).Length();

                // If player is very close to a mid boost pad (within 150 units)
                if (distToPad < 150.0f) {
                    isMidBoostGrab = true;
                    break;
                }
            }

            if (!isMidBoostGrab) {
                return 0.0f;
            }

            // Calculate reward based on ball position
            float ballDistFromMid = GetBallDistanceFromMid(state);
            float proximityFactor = 1.0f - RS_CLAMP(ballDistFromMid / maxBallDistForBonus, 0.0f, 1.0f);

            // Base reward + bonus if ball is in mid area
            float reward = baseReward + (ballProximityBonus * proximityFactor);

            return reward;
        }
    };

    // ============================================================================
    // DefensiveRotationBoostReward
    // ============================================================================
    // Rewards boost pickups (large or small pads) when rotating back to defense
    // Detects defensive rotation by checking if:
    // - Player is moving toward their own goal
    // - Ball is closer to opponent goal or in mid/opponent half
    // - Player is behind the ball (defensive positioning)
    // ============================================================================

    class DefensiveRotationBoostReward : public Reward {
    private:
        struct Config {
            float largeBoostReward = 0.8f;      // Reward for grabbing large boost pad
            float smallBoostReward = 0.3f;      // Reward for grabbing small boost pad

            // Defensive rotation detection
            float minVelocityTowardGoal = 500.0f;  // Must be moving toward own goal
            float behindBallThreshold = 200.0f;     // Must be this far behind ball (toward own goal)

            // Boost detection
            float largeBoostGainThreshold = 80.0f;  // Large pad gives ~100 boost
            float smallBoostGainThreshold = 10.0f;  // Small pads give 12 boost
        };

        Config config;
        mutable std::map<uint32_t, float> previousBoost;

        /**
         * Check if player is rotating defensively
         */
        bool IsRotatingDefensively(const Player& player, const GameState& state, const Vec& ownGoal) const {
            // Direction toward own goal
            Vec toOwnGoal = (ownGoal - player.pos).Normalized();

            // Check if moving toward own goal
            float velocityTowardGoal = player.vel.Dot(toOwnGoal);
            if (velocityTowardGoal < config.minVelocityTowardGoal) {
                return false;
            }

            // Check if player is behind the ball (toward own goal)
            float playerDistToGoal = (player.pos - ownGoal).Length();
            float ballDistToGoal = (state.ball.pos - ownGoal).Length();

            // Player should be closer to own goal than ball (behind the ball)
            if (playerDistToGoal > ballDistToGoal + config.behindBallThreshold) {
                return false;
            }

            return true;
        }

    public:
        DefensiveRotationBoostReward() : config() {}
        explicit DefensiveRotationBoostReward(const Config& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            previousBoost.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Don't reward during kickoff
            bool isKickoff = (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 200.0f);
            if (isKickoff) {
                return 0.0f;
            }

            uint32_t id = player.carId;

            // Initialize previous boost
            if (previousBoost.find(id) == previousBoost.end()) {
                previousBoost[id] = player.boost;
                return 0.0f;
            }

            // Determine own goal position
            Vec ownGoal = (player.team == Team::BLUE) ?
                Vec(0.0f, -CommonValues::GOAL_LINE_Y, 0.0f) :
                Vec(0.0f, CommonValues::GOAL_LINE_Y, 0.0f);

            // Check if rotating defensively
            if (!IsRotatingDefensively(player, state, ownGoal)) {
                previousBoost[id] = player.boost;
                return 0.0f;
            }

            // Check for boost gain
            float boostGain = player.boost - previousBoost[id];
            previousBoost[id] = player.boost;

            if (boostGain <= 0.0f) {
                return 0.0f;
            }

            // Determine if large or small boost pad
            if (boostGain >= config.largeBoostGainThreshold) {
                return config.largeBoostReward;
            }
            else if (boostGain >= config.smallBoostGainThreshold) {
                return config.smallBoostReward;
            }

            return 0.0f;
        }
    };

} // namespace RLGC