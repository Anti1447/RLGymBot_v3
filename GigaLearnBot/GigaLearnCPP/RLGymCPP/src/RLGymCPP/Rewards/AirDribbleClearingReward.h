#pragma once

#include "Reward.h"
#include "../CommonValues.h"
#include <map>
#include <atomic>
#include <cmath>

namespace RLGC {

    // AirDribbleClearingReward (NEW: Defensive clearing focus)
    // - Rewards air dribbles that move ball toward opponent's side (NOT toward goal)
    // - Use case: Back wall/corner pops that set up offensive pressure
    // - Tracks progress away from own goal and toward opponent's half
    // - Higher height tolerance than offensive dribble (clearing doesn't need precision)
    // - Separate sequence tracking from offensive air dribbles
    // - Prevents overlap: Only activates when ball moving AWAY from own goal
    class AirDribbleClearingReward : public Reward {
    private:
        mutable std::map<uint32_t, int> consecutiveAirTouches;
        float minBallHeight = 250.0f;

        // Per-sequence state
        mutable std::map<uint32_t, float> seqAccumReward;
        mutable std::map<uint32_t, float> seqTime;
        mutable std::map<uint32_t, int>   seqTouchCount;
        mutable std::map<uint32_t, bool>  seqExhausted;

        // Height tracking for clearing assessment
        mutable std::map<uint32_t, float> seqStartHeight;
        mutable std::map<uint32_t, float> seqMaxHeight;
        mutable std::map<uint32_t, float> seqMinHeight;
        mutable std::map<uint32_t, bool>  inClearingZone;

        // First touch quality tracking (same as offensive)
        mutable std::map<uint32_t, Vec>   firstTouchBallPos;
        mutable std::map<uint32_t, Vec>   firstTouchBallVel;
        mutable std::map<uint32_t, bool>  firstTouchProcessed;

        // ============================================================================
        // TUNABLE CONSTANTS
        // ============================================================================

        // --- CORE REWARD SCALING ---
        const float PROGRESS_SCALE = 0.008f;
        // Reward per unit of progress away from own goal
        // More aggressive: 0.010-0.012 | More conservative: 0.005-0.006

        const float PER_TOUCH_MAX = 5.0f;
        // Maximum reward per single touch (lower than offensive dribble)
        // More aggressive: 7.0-8.0 | More conservative: 3.0-4.0

        const float SEQ_REWARD_CAP = 40.0f;
        // Total reward cap per sequence (lower than offensive)
        // More aggressive: 50.0-60.0 | More conservative: 25.0-30.0

        const float MAX_SEQ_DURATION = 12.0f;
        // Maximum sequence duration in seconds
        // More aggressive: 10.0-11.0 | More conservative: 6.0-8.0

        const int MAX_TOUCHES = 35;
        // Maximum touches before exhausting
        // More aggressive: 40-45 | More conservative: 20-25

        const float MIN_PROGRESS_UU = 10.0f;
        // Minimum progress required to award anything
        // More aggressive: 5.0 | More conservative: 15.0

        // --- DECAY CURVE ---
        const float DECAY_EXPONENT = 0.45f;
        // How fast rewards diminish (lower = softer)
        // More aggressive: 0.3-0.4 | More conservative: 0.6-0.75

        // --- BONUS REWARDS ---
        const float ENTRY_BONUS = 0.5f;
        // One-time bonus when first qualifying touch occurs
        // More aggressive: 0.7-0.9 | More conservative: 0.2-0.3

        const float SECOND_TOUCH_BONUS = 0.8f;
        // Bonus when second qualifying touch occurs
        // More aggressive: 1.0-1.2 | More conservative: 0.3-0.5

        // --- FIRST TOUCH QUALITY ---
        const float ROUGH_TOUCH_VELOCITY_THRESHOLD = 1200.0f;
        // Ball velocity change threshold for rough touch detection
        // Lower = more strict | Higher = more lenient

        const float ROUGH_TOUCH_PENALTY = 0.7f;
        // Penalty multiplier for rough/aggressive first touches
        // Applied to entry bonus only

        // --- CLEARING ZONE REWARDS ---
        const float CLEARING_ZONE_MIN_HEIGHT = 200.0f;
        // Minimum ball height for clearing zone
        // Lower tolerance than offensive dribble

        const float CLEARING_ZONE_MAX_HEIGHT = 800.0f;
        // Maximum ball height for clearing zone (more permissive)
        // Clearing doesn't need precision finishing

        const float CLEARING_ZONE_BONUS = 1.2f;
        // Reward multiplier when in clearing zone
        // More aggressive: 1.4-1.6 | More conservative: 1.0-1.1

        // --- HEIGHT PENALTY FOR SETUP PHASE ---
        const float EXCESSIVE_HEIGHT_THRESHOLD = 1500.0f;
        // Ball height above which to penalize when far from opponent goal
        // Slightly more permissive than offensive (clearing allows higher arcs)

        const float HEIGHT_PENALTY_EXPONENT = 1.2f;
        // How aggressively to penalize excessive height
        // Softer than offensive dribble (1.5)

        const float MAX_HEIGHT_PENALTY = 0.5f;
        // Cap on penalty reduction
        // More permissive than offensive (0.6)

        // --- DIRECTIONAL REQUIREMENTS ---
        const float MIN_DIR_MULTIPLIER = 0.4f;
        // Minimum directional alignment multiplier
        // Slightly more lenient than offensive (0.46)

        const float GOOD_DIR_THRESHOLD = 0.45f;
        // Alignment threshold for "good" direction (dot product)
        // More lenient than offensive (0.525)

        const float EPS_REWARD_SKIP = 1e-7f;

        // ============================================================================
        // Helper Methods
        // ============================================================================

        float ComputeDirectionMultiplier(const Player& player, const GameState& state) const {
            // Direction toward opponent's GOAL LINE (not goal center)
            // For blue team: positive Y direction
            // For orange team: negative Y direction
            Vec ownGoal = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
            Vec oppGoalLine = (player.team == Team::BLUE) ?
                Vec(0.0f, CommonValues::BACK_WALL_Y, 0.0f) :
                Vec(0.0f, -CommonValues::BACK_WALL_Y, 0.0f);

            // Direction away from own goal
            Vec awayFromGoal = (oppGoalLine - ownGoal).Normalized();
            Vec ballVelDir = state.ball.vel.Normalized();

            float alignment = ballVelDir.Dot(awayFromGoal);

            if (alignment < 0.0f) {
                return MIN_DIR_MULTIPLIER * 0.4f;
            }
            else if (alignment < GOOD_DIR_THRESHOLD) {
                float t = alignment / GOOD_DIR_THRESHOLD;
                return MIN_DIR_MULTIPLIER + (1.0f - MIN_DIR_MULTIPLIER) * t * 0.8f;
            }
            else {
                float t = (alignment - GOOD_DIR_THRESHOLD) / (1.0f - GOOD_DIR_THRESHOLD);
                return 0.8f + 0.2f * t; // 0.8 to 1.0
            }
        }

        float ComputeHeightPenalty(float currentHeight) const {
            // Softer penalty for clearing dribbles
            if (currentHeight < EXCESSIVE_HEIGHT_THRESHOLD) {
                return 1.0f;
            }

            float excessHeight = currentHeight - EXCESSIVE_HEIGHT_THRESHOLD;
            float penalty = powf(excessHeight / (CommonValues::CEILING_Z - EXCESSIVE_HEIGHT_THRESHOLD),
                HEIGHT_PENALTY_EXPONENT);
            penalty = RS_CLAMP(penalty, 0.0f, 1.0f - MAX_HEIGHT_PENALTY);

            return 1.0f - penalty;
        }

        float ComputeClearingZoneBonus(float ballHeight) const {
            if (ballHeight >= CLEARING_ZONE_MIN_HEIGHT && ballHeight <= CLEARING_ZONE_MAX_HEIGHT) {
                return CLEARING_ZONE_BONUS;
            }
            return 1.0f;
        }

        float ComputeFirstTouchQualityMultiplier(const Vec& prevBallVel, const Vec& currBallVel) const {
            float prevSpeed = prevBallVel.Length();
            float currSpeed = currBallVel.Length();
            float velocityChange = currSpeed - prevSpeed;

            if (velocityChange > ROUGH_TOUCH_VELOCITY_THRESHOLD) {
                return ROUGH_TOUCH_PENALTY;
            }

            return 1.0f;
        }

    public:
        struct AirDribbleClearingMetrics {
            // Raw counters
            int attempts = 0;
            int successful = 0;          // Made progress away from own goal
            int inClearingZone = 0;      // Times in clearing zone
            double rewardSum = 0.0;
            int roughTouchCount = 0;
            int totalTouches = 0;

            // Computed metrics (5 core metrics)
            double avgSequenceLength = 0.0;
            double softTouchRatio = 0.0;
            double clearingRate = 0.0;
            double zoneEfficiency = 0.0;
            double touchQualityScore = 0.0;
        };

        inline static std::atomic<int> s_attempts{ 0 };
        inline static std::atomic<int> s_successful{ 0 };
        inline static std::atomic<int> s_inClearingZone{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };
        inline static std::atomic<int> s_roughTouches{ 0 };
        inline static std::atomic<int> s_totalTouches{ 0 };

        static AirDribbleClearingMetrics GetAndResetMetrics() {
            AirDribbleClearingMetrics m;
            m.attempts = s_attempts.exchange(0);
            m.successful = s_successful.exchange(0);
            m.inClearingZone = s_inClearingZone.exchange(0);
            m.roughTouchCount = s_roughTouches.exchange(0);
            m.totalTouches = s_totalTouches.exchange(0);
            long long r = s_rewardMilli.exchange(0);
            m.rewardSum = double(r) / 1000.0;

            // Compute 5 core metrics
            if (m.attempts > 0) {
                // 1. Average sequence length
                m.avgSequenceLength = (double)m.totalTouches / m.attempts;

                // 2. Soft touch ratio (%)
                int softTouches = m.attempts - m.roughTouchCount;
                m.softTouchRatio = ((double)softTouches / m.attempts) * 100.0;

                // 3. Clearing rate (%)
                m.clearingRate = ((double)m.successful / m.attempts) * 100.0;

                // 4. Zone efficiency (%)
                if (m.inClearingZone > 0) {
                    m.zoneEfficiency = ((double)m.inClearingZone / m.attempts) * 100.0;
                }
                else {
                    m.zoneEfficiency = 0.0;
                }

                // 5. Touch quality score (combined [0-1])
                double normalizedSoftTouch = m.softTouchRatio / 100.0;
                double normalizedClearing = m.clearingRate / 100.0;
                double normalizedZoneEff = m.zoneEfficiency / 100.0;
                m.touchQualityScore = (normalizedSoftTouch + normalizedClearing + normalizedZoneEff) / 3.0;
            }

            return m;
        }

        virtual void Reset(const GameState& initialState) override {
            consecutiveAirTouches.clear();
            seqAccumReward.clear();
            seqTime.clear();
            seqTouchCount.clear();
            seqExhausted.clear();
            seqStartHeight.clear();
            seqMaxHeight.clear();
            seqMinHeight.clear();
            inClearingZone.clear();
            firstTouchBallPos.clear();
            firstTouchBallVel.clear();
            firstTouchProcessed.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

            // Reset on invalid conditions
            if (player.isOnGround || state.ball.pos.z < minBallHeight || player.pos.z >= state.ball.pos.z) {
                consecutiveAirTouches[id] = 0;
                seqAccumReward.erase(id);
                seqTime.erase(id);
                seqTouchCount.erase(id);
                seqExhausted.erase(id);
                seqStartHeight.erase(id);
                seqMaxHeight.erase(id);
                seqMinHeight.erase(id);
                inClearingZone.erase(id);
                firstTouchBallPos.erase(id);
                firstTouchBallVel.erase(id);
                firstTouchProcessed.erase(id);
                return 0.0f;
            }

            // Check boost requirements (same as offensive)
            if (seqTime.find(id) == seqTime.end()) {
                // New sequence: require minimum boost to initiate
                if (player.boost < 30.0f) {
                    return 0.0f;
                }
            } else {
                // Existing sequence: check if boost drops below continue threshold
                if (player.boost < 20.0f) {
                    // Reset sequence
                    seqAccumReward.erase(id);
                    seqTime.erase(id);
                    seqTouchCount.erase(id);
                    seqExhausted.erase(id);
                    seqStartHeight.erase(id);
                    seqMaxHeight.erase(id);
                    seqMinHeight.erase(id);
                    inClearingZone.erase(id);
                    firstTouchBallPos.erase(id);
                    firstTouchBallVel.erase(id);
                    firstTouchProcessed.erase(id);
                    consecutiveAirTouches[id] = 0;
                    return 0.0f;
                }
            }

            if (!state.prev || !player.ballTouchedStep)
                return 0.0f;

            // Initialize sequence state
            if (seqTime.find(id) == seqTime.end()) {
                seqTime[id] = 0.0f;
                seqAccumReward[id] = 0.0f;
                seqTouchCount[id] = 0;
                seqExhausted[id] = false;
                consecutiveAirTouches[id] = 0;
                seqStartHeight[id] = state.ball.pos.z;
                seqMaxHeight[id] = state.ball.pos.z;
                seqMinHeight[id] = state.ball.pos.z;
                inClearingZone[id] = false;
                firstTouchProcessed[id] = false;
                firstTouchBallPos[id] = state.ball.pos;
                firstTouchBallVel[id] = state.prev ? state.prev->ball.vel : state.ball.vel;
            }

            seqTime[id] += dt;

            if (seqExhausted[id])
                return 0.0f;

            if (seqTime[id] > MAX_SEQ_DURATION || seqTouchCount[id] >= MAX_TOUCHES) {
                seqExhausted[id] = true;
                return 0.0f;
            }

            s_attempts.fetch_add(1);

            int& consTouches = consecutiveAirTouches[id];
            int prevTouches = consTouches;
            consTouches++;

            // Track height stats
            if (state.ball.pos.z > seqMaxHeight[id]) {
                seqMaxHeight[id] = state.ball.pos.z;
            }
            if (state.ball.pos.z < seqMinHeight[id]) {
                seqMinHeight[id] = state.ball.pos.z;
            }

            // Check if currently in clearing zone (height-wise)
            bool nowInZone = (state.ball.pos.z >= CLEARING_ZONE_MIN_HEIGHT &&
                state.ball.pos.z <= CLEARING_ZONE_MAX_HEIGHT);
            if (nowInZone) {
                inClearingZone[id] = true;
            }

            // Compute progress (away from own goal, toward opponent's side)
            Vec ownGoal = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
            Vec oppGoalLine = (player.team == Team::BLUE) ?
                Vec(0.0f, CommonValues::BACK_WALL_Y, 0.0f) :
                Vec(0.0f, -CommonValues::BACK_WALL_Y, 0.0f);

            float prevDistFromOwn = (state.prev->ball.pos - ownGoal).Length();
            float currDistFromOwn = (state.ball.pos - ownGoal).Length();

            float progress = RS_MAX(0.0f, currDistFromOwn - prevDistFromOwn);

            // Only reward if moving away from own goal (clearing)
            if (progress < MIN_PROGRESS_UU || currDistFromOwn < prevDistFromOwn)
                return 0.0f;

            // Base per-touch reward
            float perTouch = progress * PROGRESS_SCALE;
            perTouch = RS_MIN(PER_TOUCH_MAX, perTouch);

            // Decay curve
            float usedFraction = RS_CLAMP(seqAccumReward[id] / SEQ_REWARD_CAP, 0.0f, 1.0f);
            float remainingFraction = RS_MAX(0.0f, 1.0f - usedFraction);
            float decay = powf(remainingFraction, DECAY_EXPONENT);

            float remainingCap = RS_MAX(0.0f, SEQ_REWARD_CAP - seqAccumReward[id]);
            if (remainingCap <= 0.0f) {
                seqExhausted[id] = true;
                return 0.0f;
            }

            float rewardBeforeDir = RS_MIN(perTouch * decay, remainingCap);

            // Apply directional multiplier
            float dirMul = ComputeDirectionMultiplier(player, state);

            // Apply height penalty
            float heightPenalty = ComputeHeightPenalty(state.ball.pos.z);

            // Apply clearing zone bonus
            float zoneBonusMultiplier = ComputeClearingZoneBonus(state.ball.pos.z);

            float reward = rewardBeforeDir * dirMul * heightPenalty * zoneBonusMultiplier;

            // Entry/second-touch bonuses
            if (prevTouches == 0) {
                // First touch: Check touch quality
                float touchQualityMul = 1.0f;
                if (!firstTouchProcessed[id]) {
                    touchQualityMul = ComputeFirstTouchQualityMultiplier(
                        firstTouchBallVel[id],
                        state.ball.vel
                    );
                    firstTouchProcessed[id] = true;

                    // Track rough touches for metrics
                    if (touchQualityMul < 1.0f) {
                        s_roughTouches.fetch_add(1);
                    }
                }
                reward += ENTRY_BONUS * touchQualityMul * RS_MIN(1.0f, dirMul * heightPenalty);
            }
            else if (prevTouches == 1) {
                reward += SECOND_TOUCH_BONUS * RS_MIN(1.0f, dirMul * heightPenalty);
            }

            // Track total touches for metrics
            s_totalTouches.fetch_add(1);

            if (reward <= EPS_REWARD_SKIP)
                return 0.0f;

            // Commit reward
            seqAccumReward[id] += reward;
            seqTouchCount[id] += 1;

            if (seqAccumReward[id] >= SEQ_REWARD_CAP - 1e-3f) {
                seqExhausted[id] = true;
            }

            // Metrics
            if (inClearingZone[id]) {
                s_inClearingZone.fetch_add(1);
            }
            s_successful.fetch_add(1);
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

            return reward;
        }
    };

} // namespace RLGC
