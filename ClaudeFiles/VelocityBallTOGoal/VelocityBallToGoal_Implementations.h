#pragma once
#include "Reward.h"
#include "../Math.h"

namespace RLGC {

// ============================================================================
// ORIGINAL (for reference)
// ============================================================================
class VelocityBallToGoalReward_Original : public Reward {
public:
    bool ownGoal = false;
    VelocityBallToGoalReward_Original(bool ownGoal = false) : ownGoal(ownGoal) {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool targetOrangeGoal = player.team == Team::BLUE;
        if (ownGoal)
            targetOrangeGoal = !targetOrangeGoal;

        Vec targetPos = targetOrangeGoal ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        
        Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
        return ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
    }
};

// ============================================================================
// VARIANT 1: Distance-Weighted (Simple Enhancement)
// - Favors goal-ward velocity more heavily when ball is closer
// - Good middle ground: minimal changes, meaningful improvement
// ============================================================================
class VelocityBallToGoalReward_DistanceWeighted : public Reward {
private:
    static constexpr float OPTIMAL_SHOT_DISTANCE = 2000.0f;  // Peak at box range
    static constexpr float MAX_DISTANCE = 6000.0f;           // Decay beyond this
    static constexpr float MIN_ALIGNMENT = 0.1f;             // Any positive motion fine
    
public:
    bool ownGoal = false;
    VelocityBallToGoalReward_DistanceWeighted(bool ownGoal = false) : ownGoal(ownGoal) {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool targetOrangeGoal = player.team == Team::BLUE;
        if (ownGoal)
            targetOrangeGoal = !targetOrangeGoal;

        Vec targetPos = targetOrangeGoal ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        
        Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
        float goalAlignment = ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
        
        // Clamp to [0, 1] - don't reward backward motion
        goalAlignment = RS_MAX(0.0f, goalAlignment);
        
        // Distance-based weighting
        float distToGoal = (state.ball.pos - targetPos).Length();
        float distanceFactor = 1.0f;
        
        if (distToGoal < OPTIMAL_SHOT_DISTANCE) {
            // Close range: full or higher reward
            distanceFactor = 1.0f;
        } else if (distToGoal < MAX_DISTANCE) {
            // Decay from optimal to max distance
            float t = (distToGoal - OPTIMAL_SHOT_DISTANCE) / (MAX_DISTANCE - OPTIMAL_SHOT_DISTANCE);
            distanceFactor = 1.0f - 0.6f * t;  // 1.0x at optimal, 0.4x at max
        } else {
            return 0.0f;  // Too far away
        }
        
        return goalAlignment * distanceFactor;
    }
};

// ============================================================================
// VARIANT 2: Intent-Gated (Action-Focused)
// - Prioritizes active touches; passive rolling has lower reward
// - Stronger signal that player is making deliberate choices
// ============================================================================
class VelocityBallToGoalReward_IntentGated : public Reward {
private:
    static constexpr float TOUCH_MULTIPLIER = 1.3f;  // 30% boost for active touches
    
public:
    bool ownGoal = false;
    VelocityBallToGoalReward_IntentGated(bool ownGoal = false) : ownGoal(ownGoal) {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool targetOrangeGoal = player.team == Team::BLUE;
        if (ownGoal)
            targetOrangeGoal = !targetOrangeGoal;

        Vec targetPos = targetOrangeGoal ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        
        Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
        float goalAlignment = ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
        
        // Base: passive motion toward goal
        float reward = RS_MAX(0.0f, goalAlignment);
        
        // Intent bonus: actively touched the ball this frame
        if (player.ballTouchedStep) {
            reward = RS_MIN(1.0f, reward * TOUCH_MULTIPLIER);  // Cap at 1.0
        }
        
        return reward;
    }
};

// ============================================================================
// VARIANT 3: Expected-Goals Aware (Shot Quality Focused)
// - Incorporates distance + speed + angle into reward
// - Trains better shot selection and suppresses low-probability plays
// - Most sophisticated; requires careful tuning
// ============================================================================
class VelocityBallToGoalReward_ExpectedGoals : public Reward {
private:
    static constexpr float OPTIMAL_DISTANCE = 2000.0f;   // Peak reward distance
    static constexpr float MIN_SHOT_SPEED = 500.0f;      // Slow rolls
    static constexpr float OPTIMAL_SHOT_SPEED = 2000.0f; // Peak shot speed
    static constexpr float MAX_DISTANCE = 5500.0f;       // Cutoff distance
    
public:
    bool ownGoal = false;
    VelocityBallToGoalReward_ExpectedGoals(bool ownGoal = false) : ownGoal(ownGoal) {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool targetOrangeGoal = player.team == Team::BLUE;
        if (ownGoal)
            targetOrangeGoal = !targetOrangeGoal;

        Vec targetPos = targetOrangeGoal ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        
        Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
        float goalAlignment = ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
        
        // Must be aimed somewhat at goal
        if (goalAlignment < 0.2f) return 0.0f;
        
        // Angle quality: [0.2, 1.0] aligned -> rescale to [0.1, 1.0] quality
        float angleQuality = (goalAlignment + 1.0f) / 2.0f;  // [-1, 1] -> [0, 1]
        angleQuality = RS_MAX(0.1f, angleQuality);           // Floor at 0.1
        
        // Distance quality: peak at OPTIMAL_DISTANCE, decay beyond MAX_DISTANCE
        float distToGoal = (state.ball.pos - targetPos).Length();
        float distanceQuality;
        
        if (distToGoal < OPTIMAL_DISTANCE) {
            // Closer than optimal: full quality
            distanceQuality = 1.0f;
        } else if (distToGoal < MAX_DISTANCE) {
            // Decay range
            float t = (distToGoal - OPTIMAL_DISTANCE) / (MAX_DISTANCE - OPTIMAL_DISTANCE);
            distanceQuality = 1.0f - 0.7f * t;  // 1.0x at optimal, 0.3x at max
        } else {
            return 0.0f;  // Too far
        }
        
        // Speed quality: peak around OPTIMAL, penalize very slow and very fast
        float ballSpeed = state.ball.vel.Length();
        float speedQuality;
        
        if (ballSpeed < MIN_SHOT_SPEED) {
            // Slow rolls: ramp up
            speedQuality = ballSpeed / MIN_SHOT_SPEED;
        } else if (ballSpeed < OPTIMAL_SHOT_SPEED) {
            // Good range: full quality
            speedQuality = 1.0f;
        } else {
            // Too fast: slightly penalize (don't go over goal)
            float overage = ballSpeed - OPTIMAL_SHOT_SPEED;
            speedQuality = 1.0f - RS_CLAMP(overage / 2000.0f, 0.0f, 0.4f);
        }
        
        // Composite: weight angle heavily, then distance and speed
        return angleQuality * (0.5f * distanceQuality + 0.3f * speedQuality);
    }
};

// ============================================================================
// VARIANT 4: Composite Enhanced (RECOMMENDED)
// - Distance weighting + Intent gating + Conservative alignment requirement
// - Best balance of simplicity vs. improvement
// - Easiest to tune and debug
// ============================================================================
class VelocityBallToGoalReward_Enhanced : public Reward {
private:
    static constexpr float TOUCH_BONUS = 0.25f;             // +25% for active touches
    static constexpr float OPTIMAL_SHOT_DISTANCE = 2000.0f; // Peak reward distance
    static constexpr float MAX_REWARD_DISTANCE = 5000.0f;   // Decay cutoff
    static constexpr float MIN_ALIGNMENT = 0.15f;           // ~82° angle min
    
public:
    bool ownGoal = false;
    VelocityBallToGoalReward_Enhanced(bool ownGoal = false) : ownGoal(ownGoal) {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        bool targetOrangeGoal = player.team == Team::BLUE;
        if (ownGoal)
            targetOrangeGoal = !targetOrangeGoal;

        Vec targetPos = targetOrangeGoal ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        
        Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
        float goalAlignment = ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
        
        // Must be at least somewhat aimed at goal
        if (goalAlignment < MIN_ALIGNMENT) return 0.0f;
        
        // Clamp to [0, 1] for conservative reward
        float baseAlignment = RS_CLAMP(goalAlignment, 0.0f, 1.0f);
        
        // Distance weighting: favor shots closer to goal
        float distToGoal = (state.ball.pos - targetPos).Length();
        float distanceWeight = 1.0f;
        
        if (distToGoal < OPTIMAL_SHOT_DISTANCE) {
            // In optimal range: full reward
            distanceWeight = 1.0f;
        } else if (distToGoal < MAX_REWARD_DISTANCE) {
            // Decay beyond optimal
            float decay = (distToGoal - OPTIMAL_SHOT_DISTANCE) / (MAX_REWARD_DISTANCE - OPTIMAL_SHOT_DISTANCE);
            distanceWeight = 1.0f - 0.6f * decay;  // 1.0x at optimal, 0.4x at max
        } else {
            return 0.0f;  // Too far, no reward
        }
        
        float reward = baseAlignment * distanceWeight;
        
        // Intent bonus: reward active touches more heavily
        if (player.ballTouchedStep) {
            reward = RS_CLAMP(reward + TOUCH_BONUS, 0.0f, 1.3f);
        }
        
        return reward;
    }
};

// ============================================================================
// TESTING HARNESS: Wrap any variant for overfitting test
// ============================================================================
class VelocityBallToGoalReward_OverfitTest : public Reward {
private:
    Reward* innerReward;
    float weightMultiplier;
    
public:
    VelocityBallToGoalReward_OverfitTest(Reward* inner, float multiplier = 100.0f)
        : innerReward(inner), weightMultiplier(multiplier) {}
    
    virtual ~VelocityBallToGoalReward_OverfitTest() {
        delete innerReward;
    }
    
    virtual void Reset(const GameState& initialState) override {
        if (innerReward) innerReward->Reset(initialState);
    }
    
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!innerReward) return 0.0f;
        return innerReward->GetReward(player, state, isFinal) * weightMultiplier;
    }
};

// ============================================================================
// QUICK COMPARISON TABLE
// ============================================================================
/*
Variant              | Complexity | Improvement | Integration Risk | Tuning Effort
---------------------|------------|-------------|------------------|---------------
Original             | Very Low   | Baseline    | Low              | None
Distance-Weighted    | Low        | Moderate    | Very Low         | Minimal
Intent-Gated         | Low        | Moderate    | Low              | Minimal
Expected-Goals       | High       | High        | Moderate         | High
Enhanced (Rec.)      | Moderate   | High        | Low              | Moderate

Best for Phase 5-6: Enhanced
  - Preserves most original signal
  - Adds meaningful distance + intent weighting
  - Easy to debug if issues arise
  - Gradual tuning: start with reduced TOUCH_BONUS (0.15f), scale up

Overfitting Protocol:
  1. Wrap chosen variant in OverfitTest with multiplier=100.0f
  2. Train 500M-1B steps
  3. Review replays: are shots higher quality?
  4. If yes -> integrate with weight=1.0x at normal scale
  5. If no -> try different variant or revert
*/

}  // namespace RLGC
