// MovementAndKickoffRewards_Enhanced.h
// Comprehensive reward shaping for movement mechanics and kickoffs
// Date: 2025-12-23
// 
// IMPROVEMENTS IMPLEMENTED:
// 1. SaveBoostReward - linearly decreasing reward, more reward when boost is low
// 2. DiscourageFlippingLowBoostReward - penalize flipping with low boost & low speed
// 3. LandingQualityReward - reward landing on all 4 wheels or wavedashing
// 4. SupersonicBoostPenalty - punish boosting when already supersonic
// 5. EnhancedKickoffReward - comprehensive kickoff improvements:
//    - Remove delayed kickoffs
//    - Focus on supersonic hits
//    - Reward follow-through toward opponent net
//    - Reward awareness of opponent faking & taking possession
//
// Place in: GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/

#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <atomic>

namespace RLGC {

	//moved saveBoost and discourage flipping low boost to BoostRewards.h

// ============================================================================
// LANDING QUALITY REWARD
// Rewards landing on all 4 wheels OR performing a wavedash
// Distinguishes between:
// - Clean 4-wheel landings (stable, efficient)
// - Wavedash landings (boost preservation technique)
// ============================================================================
class LandingQualityReward : public Reward {
private:
    struct LandingState {
        bool wasAirborne = false;
        Vec airVel;
        bool hadFlip = false;
    };
    
    mutable std::map<uint32_t, LandingState> landingStates;
    
    // Metrics
    inline static std::atomic<int> s_fourWheelLandings{0};
    inline static std::atomic<int> s_wavedashLandings{0};
    inline static std::atomic<long long> s_rewardMilli{0};
    
    static constexpr float FOUR_WHEEL_REWARD = 0.3f;
    static constexpr float WAVEDASH_REWARD = 0.8f;
    static constexpr float MIN_WAVEDASH_GAIN = 25.0f;  // Speed gain in uu/s
    static constexpr float WHEELS_DOWN_THRESHOLD = -0.6f;  // Car up dot surface normal
    
public:
    struct LandingMetrics {
        int fourWheelLandings = 0;
        int wavedashLandings = 0;
        double rewardSum = 0.0;
    };
    
    static LandingMetrics GetAndResetMetrics() {
        LandingMetrics m;
        m.fourWheelLandings = s_fourWheelLandings.exchange(0);
        m.wavedashLandings = s_wavedashLandings.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.rewardSum = double(r) / 1000.0;
        return m;
    }
    
    virtual void Reset(const GameState& initialState) override {
        landingStates.clear();
    }
    
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        auto& ls = landingStates[player.carId];
        
        bool airborne = !player.isOnGround;
        bool wasAirborne = ls.wasAirborne;
        ls.wasAirborne = airborne;
        
        // Track flip availability when airborne
        if (airborne) {
            ls.hadFlip = player.HasFlipOrJump();
            ls.airVel = player.vel;
            return 0.0f;
        }
        
        // Just landed
        if (wasAirborne && !airborne) {
            Vec carUp = player.rotMat.up;
            Vec surfaceNormal = EstimateSurfaceNormal(player.pos);
            
            // Check if wheels are facing down
            float upDotNormal = carUp.Dot(surfaceNormal);
            bool wheelsFacingDown = (upDotNormal > WHEELS_DOWN_THRESHOLD);
            
            if (!wheelsFacingDown) {
                return 0.0f;  // Poor landing orientation
            }
            
            // Check for wavedash: landing with flip active
            bool isWavedash = (player.isFlipping || (player.prev && player.prev->isFlipping));
            
            if (isWavedash) {
                // Wavedash detected - check for speed gain
                float prevSpeed = ls.airVel.Length();
                float curSpeed = player.vel.Length();
                float speedGain = curSpeed - prevSpeed;
                
                if (speedGain >= MIN_WAVEDASH_GAIN) {
                    s_wavedashLandings.fetch_add(1);
                    float gainBonus = RS_MIN((speedGain - MIN_WAVEDASH_GAIN) / 100.0f, 0.5f);
                    float reward = WAVEDASH_REWARD + gainBonus;
                    s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
                    return reward;
                }
            } else {
                // Regular 4-wheel landing
                // Estimate number of wheels in contact based on orientation
                int wheelsInContact = 0;
                if (player.wheelsWithContact[0]) wheelsInContact++;
                if (player.wheelsWithContact[1]) wheelsInContact++;
                if (player.wheelsWithContact[2]) wheelsInContact++;
                if (player.wheelsWithContact[3]) wheelsInContact++;
                
                // Reward proportional to wheels in contact
                if (wheelsInContact >= 3) {
                    s_fourWheelLandings.fetch_add(1);
                    float reward = FOUR_WHEEL_REWARD * (wheelsInContact / 4.0f);
                    s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
                    return reward;
                }
            }
        }
        
        return 0.0f;
    }
    
private:
    Vec EstimateSurfaceNormal(const Vec& pos) const {
        Vec n(0, 0, 1);  // Default to ground
        
        const float nearSurfaceEps = 140.0f;
        const float dx = CommonValues::SIDE_WALL_X - fabsf(pos.x);
        const float dy = CommonValues::BACK_WALL_Y - fabsf(pos.y);
        const float dzCeil = CommonValues::CEILING_Z - pos.z;
        
        // Closest surface determines normal
        if (dx < nearSurfaceEps) {
            n = Vec((pos.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
        }
        if (dy < nearSurfaceEps && dy < dx) {
            n = Vec(0.0f, (pos.y >= 0.0f) ? 1.0f : -1.0f, 0.0f);
        }
        if (dzCeil < nearSurfaceEps && dzCeil < dx && dzCeil < dy) {
            n = Vec(0.0f, 0.0f, -1.0f);
        }
        
        return n;
    }
};

// ============================================================================
// ENHANCED KICKOFF REWARD
// Simplified kickoff reward focusing on fundamental mechanics:
// 1. Collecting boost pads during approach
// 2. Speed toward ball
// 3. Hitting ball away from own goal
// 4. Flipping into ball contact
// ============================================================================
class EnhancedKickoffReward : public Reward {
private:
    struct KickoffState {
        bool isKickoff = false;
        float kickoffStartTime = 0.0f;
        Vec initialPlayerPos;
        Vec initialBallPos;
        int boostPadsCollected = 0;
        float previousBoost = 0.0f;
        bool hitBall = false;
        bool wasFlipping = false;
    };

    mutable std::map<uint32_t, KickoffState> kickoffStates;
    mutable float currentTime = 0.0f;

    // Metrics
    inline static std::atomic<int> s_boostPadsCollected{ 0 };
    inline static std::atomic<int> s_flippedIntoHit{ 0 };
    inline static std::atomic<int> s_goodDirection{ 0 };
    inline static std::atomic<long long> s_rewardMilli{ 0 };

    static constexpr float BALL_CENTER_THRESHOLD = 200.0f;  // Ball is centered if < this
    static constexpr float KICKOFF_PLAYER_DIST = 3000.0f;   // Players start > this from ball

    // Rewards
    static constexpr float BOOST_PAD_REWARD = 2.0f;         // Per boost pad collected
    static constexpr float SPEED_REWARD_SCALE = 5.0f;       // Max reward for speed
    static constexpr float GOOD_HIT_DIRECTION = 10.0f;      // Hit away from own goal
    static constexpr float FLIP_INTO_HIT_BONUS = 5.0f;      // Flipped into the hit

public:
    struct KickoffMetrics {
        int boostPadsCollected = 0;
        int flippedIntoHit = 0;
        int goodDirection = 0;
        double rewardSum = 0.0;
    };

    static KickoffMetrics GetAndResetMetrics() {
        KickoffMetrics m;
        m.boostPadsCollected = s_boostPadsCollected.exchange(0);
        m.flippedIntoHit = s_flippedIntoHit.exchange(0);
        m.goodDirection = s_goodDirection.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.rewardSum = double(r) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        kickoffStates.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        float reward = 0.0f;

        auto& ks = kickoffStates[player.carId];

        // ===== KICKOFF DETECTION =====
        bool ballCentered = state.ball.pos.Length() < BALL_CENTER_THRESHOLD;
        bool ballSlow = state.ball.vel.Length() < 100.0f;
        float playerDistToBall = (player.pos - state.ball.pos).Length();
        bool playerFarFromBall = playerDistToBall > KICKOFF_PLAYER_DIST;

        bool isKickoffNow = ballCentered && ballSlow && playerFarFromBall;

        // Kickoff just started
        if (isKickoffNow && !ks.isKickoff) {
            ks.isKickoff = true;
            ks.kickoffStartTime = currentTime;
            ks.initialPlayerPos = player.pos;
            ks.initialBallPos = state.ball.pos;
            ks.boostPadsCollected = 0;
            ks.previousBoost = player.boost;
            ks.hitBall = false;
            ks.wasFlipping = false;
        }

        // ===== DURING KICKOFF =====
        if (ks.isKickoff) {
            // ===== BOOST PAD COLLECTION =====
            // Detect boost pickup (boost increased without boosting)
            if (player.prev) {
                bool notBoosting = (player.prev->prevAction.boost == 0);
                float boostGain = player.boost - player.prev->boost;

                if (notBoosting && boostGain > 0) {
                    // Collected a boost pad
                    ks.boostPadsCollected++;
                    reward += BOOST_PAD_REWARD;
                    s_boostPadsCollected.fetch_add(1);
                }
            }

            // ===== SPEED TOWARD BALL =====
            // Reward velocity component toward ball
            Vec toBall = (state.ball.pos - player.pos).Normalized();
            float speedToBall = player.vel.Dot(toBall);

            // Normalize by max speed and scale
            float speedReward = (speedToBall / CommonValues::CAR_MAX_SPEED) * SPEED_REWARD_SCALE;
            speedReward = RS_MAX(speedReward, 0.0f) * state.deltaTime;  // Only positive, continuous
            reward += speedReward;

            // Track flip state
            ks.wasFlipping = player.isFlipping;

            // ===== BALL CONTACT EVALUATION =====
            if (player.ballTouchedStep && !ks.hitBall) {
                ks.hitBall = true;

                // ===== HIT DIRECTION: Away from own goal =====
                // Vector from own goal to ball
                Vec ownGoal = (player.team == Team::BLUE) ?
                    CommonValues::BLUE_GOAL_BACK :
                    CommonValues::ORANGE_GOAL_BACK;

                Vec goalToBall = (state.ball.pos - ownGoal).Normalized();
                Vec ballVelDir = state.ball.vel.Normalized();

                // Reward if ball is moving away from own goal
                float alignment = goalToBall.Dot(ballVelDir);

                if (alignment > 0.5f) {  // Moving away from own goal
                    float directionReward = GOOD_HIT_DIRECTION * alignment;
                    reward += directionReward;
                    s_goodDirection.fetch_add(1);
                }

                // ===== FLIP INTO HIT BONUS =====
                // Check if player was flipping when they hit the ball
                if (ks.wasFlipping || player.isFlipping) {
                    reward += FLIP_INTO_HIT_BONUS;
                    s_flippedIntoHit.fetch_add(1);
                }
            }

            // ===== KICKOFF END DETECTION =====
            bool ballMovedFar = (state.ball.pos - ks.initialBallPos).Length() > 1000.0f;
            bool ballMovingFast = state.ball.vel.Length() > 500.0f;
            float kickoffDuration = currentTime - ks.kickoffStartTime;
            bool kickoffTimeout = kickoffDuration > 4.0f;

            if ((ballMovedFar && ballMovingFast) || kickoffTimeout) {
                ks.isKickoff = false;
            }
        }

        // Accumulate reward
        if (reward != 0.0f) {
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        }

        return reward;
    }
};

// ============================================================================
// COMBINED MOVEMENT & KICKOFF REWARD
// Convenience class that combines all movement and kickoff rewards
// ============================================================================
class MovementAndKickoffCombinedReward : public Reward {
private:
    SaveBoostReward saveBoost;
    DiscourageFlippingLowBoostReward discourageFlip;
    LandingQualityReward landingQuality;
    SupersonicBoostPenalty supersonicPenalty;
    EnhancedKickoffReward kickoffReward;
    
    // Weights for each component
    float saveBoostWeight;
    float discourageFlipWeight;
    float landingQualityWeight;
    float supersonicPenaltyWeight;
    float kickoffWeight;
    
public:
    MovementAndKickoffCombinedReward(
        float saveBoostW = 0.1f,
        float discourageFlipW = 1.0f,
        float landingQualityW = 1.0f,
        float supersonicPenaltyW = 1.0f,
        float kickoffW = 1.0f
    ) : saveBoostWeight(saveBoostW),
        discourageFlipWeight(discourageFlipW),
        landingQualityWeight(landingQualityW),
        supersonicPenaltyWeight(supersonicPenaltyW),
        kickoffWeight(kickoffW) {}
    
    virtual void Reset(const GameState& initialState) override {
        saveBoost.Reset(initialState);
        discourageFlip.Reset(initialState);
        landingQuality.Reset(initialState);
        supersonicPenalty.Reset(initialState);
        kickoffReward.Reset(initialState);
    }
    
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float total = 0.0f;
        
        total += saveBoost.GetReward(player, state, isFinal) * saveBoostWeight;
        total += discourageFlip.GetReward(player, state, isFinal) * discourageFlipWeight;
        total += landingQuality.GetReward(player, state, isFinal) * landingQualityWeight;
        total += supersonicPenalty.GetReward(player, state, isFinal) * supersonicPenaltyWeight;
        total += kickoffReward.GetReward(player, state, isFinal) * kickoffWeight;
        
        return total;
    }
    
    // Access to metrics from sub-rewards
    LandingQualityReward::LandingMetrics GetLandingMetrics() {
        return LandingQualityReward::GetAndResetMetrics();
    }
    
    EnhancedKickoffReward::KickoffMetrics GetKickoffMetrics() {
        return EnhancedKickoffReward::GetAndResetMetrics();
    }
};

} // namespace RLGC
