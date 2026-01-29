#pragma once

#include "Reward.h"
#include "../CommonValues.h"
#include <map>
#include <atomic>
#include <cmath>

namespace RLGC {

    // AirDribbleAccuracyReward (NEW: Accuracy-focused, prevents overshooting)
    // - Penalizes excessive height gain during dribbles (prevents ceiling shots)
    // - Rewards "scoring zone" trajectories (ball height between ~300-650 uu)
    // - Enforces strict z-height constraint ONLY when close to goal
    // - Allows high arcs when setting up from distance (back wall, corner)
    // - Emphasis on forward momentum + directional accuracy over fancy arcs
    // - Complements HighBallTouchReward to teach proper finishing
    class AirDribbleAccuracyReward : public Reward {
    private:
        mutable std::map<uint32_t, int> consecutiveAirTouches;
        float minBallHeight = 250.0f;

        // Per-sequence state
        mutable std::map<uint32_t, float> seqAccumReward;
        mutable std::map<uint32_t, float> seqTime;
        mutable std::map<uint32_t, int>   seqTouchCount;
        mutable std::map<uint32_t, bool>  seqExhausted;

        // Height tracking for accuracy assessment
        mutable std::map<uint32_t, float> seqStartHeight;
        mutable std::map<uint32_t, float> seqMaxHeight;
        mutable std::map<uint32_t, float> seqMinHeight;
        mutable std::map<uint32_t, bool>  inScoringZone;
        mutable std::map<uint32_t, bool>  enteredFinishingZone;

        // First touch quality tracking
        mutable std::map<uint32_t, Vec>   firstTouchBallPos;
        mutable std::map<uint32_t, Vec>   firstTouchBallVel;
        mutable std::map<uint32_t, bool>  firstTouchProcessed;

        // ============================================================================
        // TUNABLE CONSTANTS
        // ============================================================================

        // --- CORE REWARD SCALING ---
        const float PROGRESS_SCALE = 0.0105f;
        // Reward per unit of progress toward goal
        // More aggressive: 0.015-0.020 | More conservative: 0.008-0.010

        const float PER_TOUCH_MAX = 7.0f;
        // Maximum reward per single touch
        // More aggressive: 10.0-12.0 | More conservative: 5.0-6.0

        const float SEQ_REWARD_CAP = 65.0f;
        // Total reward cap per sequence (lower = encourages quick finishes)
        // More aggressive: 60.0-70.0 | More conservative: 30.0-40.0

        const float MAX_SEQ_DURATION = 13.0f;
        // Maximum sequence duration in seconds
        // More aggressive: 10.0-12.0 | More conservative: 5.0-6.0

        const int MAX_TOUCHES = 40;
        // Maximum touches before exhausting
        // More aggressive: 30-35 | More conservative: 15-20

        const float MIN_PROGRESS_UU = 12.0f;
        // Minimum progress required to award anything
        // More aggressive: 8.0 | More conservative: 15.0-20.0

        // --- DECAY CURVE ---
        const float DECAY_EXPONENT = 0.5f;
        // How fast rewards diminish (lower = softer)
        // More aggressive: 0.25-0.35 | More conservative: 0.55-0.70

        // --- BONUS REWARDS ---
        const float ENTRY_BONUS = 0.65f;
        // One-time bonus when first qualifying touch occurs
        // More aggressive: 0.8-1.0 | More conservative: 0.2-0.3

        const float SECOND_TOUCH_BONUS = 1.0f;
        // Bonus when second qualifying touch occurs
        // More aggressive: 1.2-1.5 | More conservative: 0.4-0.6

        // --- FIRST TOUCH QUALITY ---
        const float ROUGH_TOUCH_VELOCITY_THRESHOLD = 1200.0f;
        // Ball velocity change threshold for rough touch detection
        // If ball velocity change > this on first touch, quality penalty applied
        // Lower = more strict | Higher = more lenient

        const float ROUGH_TOUCH_PENALTY = 0.7f;
        // Penalty multiplier for rough/aggressive first touches
        // Applied to entry bonus only
        // More aggressive: 0.4-0.5 | More conservative: 0.8-0.9

        // --- SCORING ZONE REWARDS ---
        const float SCORING_ZONE_MIN_HEIGHT = 250.0f;
        // Minimum ball height for scoring zone
        // Relaxed from 150 to allow more natural dribble progression
        // Conservative approach: 250-700uu range

        const float SCORING_ZONE_MAX_HEIGHT = 630.0f;
        // Maximum ball height for scoring zone
        // Relaxed from 640 to prevent early abandonment
        // Goal rim at 642.775; 700uu provides forgiving tolerance

        const float SCORING_ZONE_BONUS = 1.3f;
        // Reward multiplier when in scoring zone
        // More aggressive: 1.5-1.8 | More conservative: 1.1-1.2

        // --- FINISHING ZONE DETECTION ---
        const float FINISHING_ZONE_RADIUS = 900.0f;
        // XY-plane distance from goal center to enter "finishing zone"
        // When inside: enforce strict z-height constraints
        // When outside: allow high arcs

        const float GOAL_CENTER_X = 0.0f;
        const float GOAL_CENTER_Y = 5120.0f;
        // Blue goal at y=5120, Orange at y=-5120

        // --- HEIGHT CONSTRAINT FOR FINISHING ZONE ---
        const float MAX_BALL_Z_FOR_GOAL = 640.f;
        // Maximum ball center z-height when in finishing zone
        // = Goal height (642.775) + tolerance (75)
        // Prevents overshooting

        const float HEIGHT_CONSTRAINT_PENALTY = 0.5f;
        // Penalty multiplier for overshooting in finishing zone
        // More aggressive: 0.1-0.2 | More conservative: 0.5-0.7

        // --- HEIGHT PENALTY FOR SETUP DRIBBLES ---
        const float EXCESSIVE_HEIGHT_THRESHOLD = 1800.0f;
        // Ball height above which to penalize when FAR from goal
        // Used during setup phase only
        // Tightened from 2000 to enforce cleaner arc control

        const float HEIGHT_PENALTY_EXPONENT = 1.5f;
        // How aggressively to penalize excessive height
        // Increased from 1.0 to 1.5 for stronger penalty on tall arcs
        // Encourages more controlled setup plays

        const float MAX_HEIGHT_PENALTY = 0.6f;
        // Cap on penalty reduction far from goal
        // Reduced from 0.8 to 0.6 to be more restrictive
        // Prevents excessive height during setup

        // --- DIRECTIONAL REQUIREMENTS ---
        const float MIN_DIR_MULTIPLIER = 0.46f;
        // Minimum directional alignment multiplier
        // More aggressive: 0.4-0.5 | More conservative: 0.65-0.75

        const float GOOD_DIR_THRESHOLD = 0.525f;
        // Alignment threshold for "good" direction (dot product)
        // More aggressive: 0.4-0.5 | More conservative: 0.65-0.75

        // --- BOOST REQUIREMENTS ---
        const float MINIMUM_BOOST_TO_INITIATE = 30.0f;
        // Minimum boost to start an air dribble attempt
        // Prevents wasteful low-boost attempts

        const float MINIMUM_BOOST_TO_CONTINUE = 20.0f;
        // Minimum boost to continue existing sequence
        // Below this threshold: reset sequence and return 0

        const float EPS_REWARD_SKIP = 1e-7f;

        // ============================================================================
        // Helper Methods
        // ============================================================================

        Vec GetGoalCenter(Team team) const {
            return Vec(GOAL_CENTER_X,
                (team == Team::BLUE) ? GOAL_CENTER_Y : -GOAL_CENTER_Y,
                0.0f);
        }

        bool IsInFinishingZone(const Vec& ballPos, Team team) const {
            Vec goalCenter = GetGoalCenter(team);
            float distXY = sqrtf((ballPos.x - goalCenter.x) * (ballPos.x - goalCenter.x) +
                (ballPos.y - goalCenter.y) * (ballPos.y - goalCenter.y));
            return distXY <= FINISHING_ZONE_RADIUS;
        }

        float ComputeDirectionMultiplier(const Player& player, const GameState& state) const {
            Vec oppGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

            Vec ballToGoal = (oppGoal - state.ball.pos).Normalized();
            Vec ballVelDir = state.ball.vel.Normalized();

            float alignment = ballVelDir.Dot(ballToGoal);

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

        float ComputeFinishingZoneHeightPenalty(float currentHeight) const {
            if (currentHeight <= MAX_BALL_Z_FOR_GOAL) {
                return 1.0f; // Perfect height for scoring
            }

            float excessHeight = currentHeight - MAX_BALL_Z_FOR_GOAL;
            float maxExcess = CommonValues::CEILING_Z - MAX_BALL_Z_FOR_GOAL;
            float penalty = RS_CLAMP(excessHeight / maxExcess, 0.0f, 1.0f - HEIGHT_CONSTRAINT_PENALTY);

            return 1.0f - penalty;
        }

        float ComputeSetupHeightPenalty(float currentHeight) const {
            if (currentHeight < EXCESSIVE_HEIGHT_THRESHOLD) {
                return 1.0f;
            }

            float excessHeight = currentHeight - EXCESSIVE_HEIGHT_THRESHOLD;
            float penalty = powf(excessHeight / (CommonValues::CEILING_Z - EXCESSIVE_HEIGHT_THRESHOLD),
                HEIGHT_PENALTY_EXPONENT);
            penalty = RS_CLAMP(penalty, 0.0f, 1.0f - MAX_HEIGHT_PENALTY);

            return 1.0f - penalty;
        }

        float ComputeScoringZoneBonus(float ballHeight) const {
            if (ballHeight >= SCORING_ZONE_MIN_HEIGHT && ballHeight <= SCORING_ZONE_MAX_HEIGHT) {
                return SCORING_ZONE_BONUS;
            }
            return 1.0f;
        }

        float ComputeFirstTouchQualityMultiplier(const Vec& prevBallVel, const Vec& currBallVel) const {
            // Detect rough/aggressive first touches based on velocity change
            // Soft touch (controlled): Small velocity change
            // Rough touch (aggressive): Large velocity change (especially with boost held)

            float prevSpeed = prevBallVel.Length();
            float currSpeed = currBallVel.Length();
            float velocityChange = currSpeed - prevSpeed;

            if (velocityChange > ROUGH_TOUCH_VELOCITY_THRESHOLD) {
                // Aggressive touch: reduce entry bonus
                return ROUGH_TOUCH_PENALTY;
            }

            return 1.0f; // Soft/controlled touch
        }

    public:
        struct AirDribbleAccuracyMetrics {
            // Raw counters
            int attempts = 0;
            int onTarget = 0;
            int inScoringZone = 0;
            int overshotCount = 0;
            double rewardSum = 0.0;
            int roughTouchCount = 0;
            int totalTouches = 0;

            // Computed metrics (5 core metrics)
            double avgSequenceLength = 0.0;         // Avg touches per attempt
            double softTouchRatio = 0.0;            // % of soft (non-rough) first touches
            double completionRate = 0.0;            // % of attempts that made progress
            double scoringZoneEfficiency = 0.0;     // % of scoring zone attempts that didn't overshoot
            double touchQualityScore = 0.0;         // Combined [0-1] quality indicator
        };

        inline static std::atomic<int> s_attempts{ 0 };
        inline static std::atomic<int> s_onTarget{ 0 };
        inline static std::atomic<int> s_inScoringZone{ 0 };
        inline static std::atomic<int> s_overshot{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };
        inline static std::atomic<int> s_roughTouches{ 0 };
        inline static std::atomic<int> s_totalTouches{ 0 };

        static AirDribbleAccuracyMetrics GetAndResetMetrics() {
            AirDribbleAccuracyMetrics m;
            m.attempts = s_attempts.exchange(0);
            m.onTarget = s_onTarget.exchange(0);
            m.inScoringZone = s_inScoringZone.exchange(0);
            m.overshotCount = s_overshot.exchange(0);
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

                // 3. Completion rate (%)
                m.completionRate = ((double)m.onTarget / m.attempts) * 100.0;

                // 4. Scoring zone efficiency (%)
                if (m.inScoringZone > 0) {
                    int completedInZone = m.inScoringZone - m.overshotCount;
                    m.scoringZoneEfficiency = ((double)completedInZone / m.inScoringZone) * 100.0;
                }
                else {
                    m.scoringZoneEfficiency = 0.0; // No scoring zone entries to evaluate
                }

                // 5. Touch quality score (combined [0-1])
                // Normalize each metric to [0-1] and average
                double normalizedSoftTouch = m.softTouchRatio / 100.0;
                double normalizedCompletion = m.completionRate / 100.0;
                double normalizedZoneEff = m.scoringZoneEfficiency / 100.0;
                m.touchQualityScore = (normalizedSoftTouch + normalizedCompletion + normalizedZoneEff) / 3.0;
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
            inScoringZone.clear();
            enteredFinishingZone.clear();
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
                inScoringZone.erase(id);
                enteredFinishingZone.erase(id);
                firstTouchBallPos.erase(id);
                firstTouchBallVel.erase(id);
                firstTouchProcessed.erase(id);
                return 0.0f;
            }

            // Check boost requirements
            if (seqTime.find(id) == seqTime.end()) {
                // New sequence: require minimum boost to initiate
                if (player.boost < MINIMUM_BOOST_TO_INITIATE) {
                    return 0.0f;
                }
            } else {
                // Existing sequence: check if boost drops below continue threshold
                if (player.boost < MINIMUM_BOOST_TO_CONTINUE) {
                    // Reset sequence
                    seqAccumReward.erase(id);
                    seqTime.erase(id);
                    seqTouchCount.erase(id);
                    seqExhausted.erase(id);
                    seqStartHeight.erase(id);
                    seqMaxHeight.erase(id);
                    seqMinHeight.erase(id);
                    inScoringZone.erase(id);
                    enteredFinishingZone.erase(id);
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
                inScoringZone[id] = false;
                enteredFinishingZone[id] = false;
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

            // Check if currently in finishing zone
            bool inFinishing = IsInFinishingZone(state.ball.pos, player.team);
            if (inFinishing) {
                enteredFinishingZone[id] = true;
            }

            // Track height stats
            if (state.ball.pos.z > seqMaxHeight[id]) {
                seqMaxHeight[id] = state.ball.pos.z;
            }
            if (state.ball.pos.z < seqMinHeight[id]) {
                seqMinHeight[id] = state.ball.pos.z;
            }

            // Check if currently in scoring zone (height-wise)
            bool nowInZone = (state.ball.pos.z >= SCORING_ZONE_MIN_HEIGHT &&
                state.ball.pos.z <= SCORING_ZONE_MAX_HEIGHT);
            if (nowInZone) {
                inScoringZone[id] = true;
            }

            // Compute progress
            Vec opponentGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
            float prevDist = (state.prev->ball.pos - opponentGoal).Length();
            float curDist = (state.ball.pos - opponentGoal).Length();

            float progress = RS_MAX(0.0f, prevDist - curDist);

            if (progress < MIN_PROGRESS_UU)
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

            // Apply distance-aware height penalty
            float heightPenalty = 1.0f;
            if (inFinishing) {
                // CLOSE TO GOAL: Enforce strict z-height constraint
                heightPenalty = ComputeFinishingZoneHeightPenalty(state.ball.pos.z);
            }
            else {
                // FAR FROM GOAL: Allow arcs but penalize excessive height
                heightPenalty = ComputeSetupHeightPenalty(state.ball.pos.z);
            }

            // Apply scoring zone bonus
            float zoneBonusMultiplier = ComputeScoringZoneBonus(state.ball.pos.z);

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
                    if (touchQualityMul < 1.0f) { // Rough touch detected
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
            if (inScoringZone[id]) {
                s_inScoringZone.fetch_add(1);
            }
            if (enteredFinishingZone[id] && seqMaxHeight[id] > MAX_BALL_Z_FOR_GOAL) {
                s_overshot.fetch_add(1);
            }

            s_onTarget.fetch_add(1);
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

            return reward;
        }
    };

} // namespace RLGC#pragma once
