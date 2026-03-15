# VelocityBallToGoal Reward: Analysis & Improvement Strategies

## Current Implementation
```cpp
class VelocityBallToGoalReward : public Reward {
public:
    bool ownGoal = false;
    VelocityBallToGoalReward(bool ownGoal = false) : ownGoal(ownGoal) {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
        bool targetOrangeGoal = player.team == Team::BLUE;
        if (ownGoal)
            targetOrangeGoal = !targetOrangeGoal;

        Vec targetPos = targetOrangeGoal ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        
        Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
        return ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
    }
};
```

---

## Current Behavior & Problems

### ✓ Strengths
- **Simple and efficient**: O(1) computation, no state tracking needed
- **Directional signal**: Rewards velocity *toward* the goal, not just any fast movement
- **Normalized**: Returns [-1, 1] range via dot product
- **Team-aware**: Correctly targets opponent goal by team

### ✗ Weaknesses

1. **No distance weighting**
   - Rewards goal-ward velocity equally whether the ball is 500 UU or 5000 UU away
   - A pass from deep field receives same reward as a shot from the box
   - *Impact*: Bot may reward low-percentage long passes over intelligent positioning

2. **No context gating**
   - Rewards whenever ball *appears* to move toward goal
   - Doesn't validate it's a *deliberate* action by the player
   - *Example*: Ball bounces toward goal after a poor touch → still rewarded
   - *Impact*: Trains careless play that happens to look momentarily positive

3. **Conflates velocity with intention**
   - High dot product could mean:
     - (Good) Player just shot toward goal
     - (Bad) Player touched ball and it happened to roll goal-ward
     - (Neutral) Ball naturally rolling toward goal, player did nothing
   - *Impact*: Weaker signal for deliberate skill application

4. **No action validation**
   - Doesn't check if player actually touched the ball this frame
   - Doesn't penalize defensive actions that momentarily appear goal-ward
   - *Impact*: Can reward passive/unintentional behavior

5. **Linear reward scaling**
   - A 0.5 dot product (ball at 60° angle) is half as good as 1.0 (straight toward goal)
   - But a shot at 60° angle might have vastly higher success probability depending on distance/angle
   - *Impact*: Doesn't account for shot quality in expected-goals sense

6. **No speed consideration relative to distance**
   - A slow roll from 3000 UU away might get same reward as a fast shot from close range
   - Doesn't differentiate "likely to score" vs. "moving in right direction"
   - *Impact*: Can't teach shot selection or when to pass vs. shoot

---

## Improvement Strategies

### Strategy 1: Distance-Weighted Velocity (Simple Enhancement)
Reward goal-ward velocity **more heavily** when the ball is closer to the goal.

```cpp
class VelocityBallToGoalReward_DistanceWeighted : public Reward {
private:
    static constexpr float OPTIMAL_SHOT_DISTANCE = 2000.0f;  // Peak reward at box range
    static constexpr float MAX_DISTANCE = 6000.0f;           // No reward beyond this
    
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
        Vec targetPos = (player.team == Team::BLUE) 
            ? CommonValues::ORANGE_GOAL_BACK 
            : CommonValues::BLUE_GOAL_BACK;
        
        Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
        float goalAlignment = ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
        
        if (goalAlignment < 0.0f) return 0.0f;  // Penalize away motion
        
        // Distance weighting: peak at box range, decay beyond
        float distToGoal = (state.ball.pos - targetPos).Length();
        float distanceFactor;
        
        if (distToGoal < OPTIMAL_SHOT_DISTANCE) {
            // Closer = higher multiplier (up to 2x at goal line)
            distanceFactor = 1.0f + (1.0f - distToGoal / OPTIMAL_SHOT_DISTANCE);
        } else if (distToGoal < MAX_DISTANCE) {
            // Farther = decay (0.5x at max distance)
            distanceFactor = 0.5f + 0.5f * (1.0f - (distToGoal - OPTIMAL_SHOT_DISTANCE) / (MAX_DISTANCE - OPTIMAL_SHOT_DISTANCE));
        } else {
            return 0.0f;  // Too far, no reward
        }
        
        return goalAlignment * distanceFactor;  // Range: [0, ~2]
    }
};
```

**Pros**: Simple, preserves current logic, adds tactical weighting  
**Cons**: Still doesn't validate player intent, arbitrary distance constants

---

### Strategy 2: Intent-Gated Velocity (Action-Based)
Only reward goal-ward velocity if the player **actively touched the ball** this frame.

```cpp
class VelocityBallToGoalReward_IntentGated : public Reward {
private:
    static constexpr float TOUCH_MULTIPLIER = 1.5f;  // Boost reward for active touches
    
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
        Vec targetPos = (player.team == Team::BLUE) 
            ? CommonValues::ORANGE_GOAL_BACK 
            : CommonValues::BLUE_GOAL_BACK;
        
        Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
        float goalAlignment = ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
        
        // Base reward: passive goal-ward motion
        float reward = RS_MAX(0.0f, goalAlignment);  // Clamp to [0, 1]
        
        // Boost if player actively touched the ball
        if (player.ballTouchedStep) {
            reward *= TOUCH_MULTIPLIER;  // Up to 1.5x
        }
        
        return reward;
    }
};
```

**Pros**: Prioritizes active play, simple to implement  
**Cons**: Over-rewards any touch toward goal (even bad ones)

---

### Strategy 3: Expected-Goals-Aware Velocity (Distance + Speed)
Reward goal-ward velocity weighted by **shot quality** (combination of distance, angle, speed).

```cpp
class VelocityBallToGoalReward_ExpectedGoals : public Reward {
private:
    static constexpr float OPTIMAL_DISTANCE = 2000.0f;
    static constexpr float MIN_SHOT_SPEED = 500.0f;
    static constexpr float OPTIMAL_SHOT_SPEED = 2000.0f;
    
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
        Vec targetPos = (player.team == Team::BLUE) 
            ? CommonValues::ORANGE_GOAL_BACK 
            : CommonValues::BLUE_GOAL_BACK;
        
        Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
        float goalAlignment = ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
        
        if (goalAlignment < 0.3f) return 0.0f;  // Not aimed at goal
        
        // Distance quality: peak at close range
        float distToGoal = (state.ball.pos - targetPos).Length();
        float distanceQuality = 1.0f - RS_CLAMP(distToGoal / 5000.0f, 0.0f, 1.0f);
        
        // Speed quality: peak around optimal, penalize slow rolls and supersonic
        float ballSpeed = state.ball.vel.Length();
        float speedQuality;
        if (ballSpeed < MIN_SHOT_SPEED) {
            speedQuality = ballSpeed / MIN_SHOT_SPEED;  // Ramping up
        } else if (ballSpeed < OPTIMAL_SHOT_SPEED) {
            speedQuality = 1.0f;
        } else {
            speedQuality = 1.0f - RS_CLAMP((ballSpeed - OPTIMAL_SHOT_SPEED) / 2000.0f, 0.0f, 0.5f);
        }
        
        // Angle quality: perfect alignment = 1.0, 45° angle = 0.7, 90° = 0.0
        float angleQuality = (goalAlignment + 1.0f) / 2.0f;  // Map [-1,1] → [0,1]
        
        // Composite expected goals signal
        return angleQuality * (0.4f * distanceQuality + 0.3f * speedQuality + 0.3f * goalAlignment);
    }
};
```

**Pros**: Shot quality aware, discoursages low-probability plays, realistic weighting  
**Cons**: More hyperparameters to tune, harder to debug

---

### Strategy 4: Composite Approach (Recommended)
Combine the best elements: **distance weighting + intent gating + position context**.

```cpp
class VelocityBallToGoalReward_Enhanced : public Reward {
private:
    static constexpr float TOUCH_BONUS = 0.3f;           // +30% for active touches
    static constexpr float OPTIMAL_SHOT_DISTANCE = 2000.0f;
    static constexpr float MAX_REWARD_DISTANCE = 5000.0f;
    static constexpr float MIN_ALIGNMENT = 0.2f;         // Must be somewhat aimed
    
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
        Vec targetPos = (player.team == Team::BLUE) 
            ? CommonValues::ORANGE_GOAL_BACK 
            : CommonValues::BLUE_GOAL_BACK;
        
        Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
        float goalAlignment = ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
        
        // Must be at least somewhat aimed at goal
        if (goalAlignment < MIN_ALIGNMENT) return 0.0f;
        
        // Clamp to [0, 1] for conservative reward
        float baseAlignment = RS_CLAMP(goalAlignment, 0.0f, 1.0f);
        
        // Distance weighting: favor closer shots
        float distToGoal = (state.ball.pos - targetPos).Length();
        float distanceWeight = 1.0f;
        
        if (distToGoal < OPTIMAL_SHOT_DISTANCE) {
            // Sweet spot: full reward
            distanceWeight = 1.0f;
        } else if (distToGoal < MAX_REWARD_DISTANCE) {
            // Decay beyond optimal distance
            float decay = (distToGoal - OPTIMAL_SHOT_DISTANCE) / (MAX_REWARD_DISTANCE - OPTIMAL_SHOT_DISTANCE);
            distanceWeight = 1.0f - 0.6f * decay;  // 1.0x at optimal, 0.4x at max
        } else {
            return 0.0f;  // Too far
        }
        
        float reward = baseAlignment * distanceWeight;
        
        // Intent bonus: active touches get extra reward
        if (player.ballTouchedStep) {
            reward += TOUCH_BONUS;
        }
        
        return reward;  // Range: [0, ~1.3]
    }
};
```

**Pros**: Balanced improvements, preserves core signal, adds tactical weighting  
**Cons**: Slightly more complex

---

## Integration Checklist

### Phase-Based Rollout (Recommended)
1. **Phase 5 (Current - Verification)**
   - Profile current VelocityBallToGoalReward reward signal
   - Check W&B for average reward value and histogram shape
   - Identify if bot is farming the reward (low-quality plays scoring high)

2. **Phase 5→6 (Testing)**
   - Implement Strategy 4 (Composite) in parallel instance
   - Run overfitting test: set weight to 100x normal for 1B steps
   - If bot learns better shots immediately, keep it
   - If behavior degrades, revert and try distance-weighting only

3. **Phase 6 (Gradual Integration)**
   - If overfitting test succeeds, integrate with **half the normal weight** initially
   - Gradually scale weight back to normal over 5-10B steps
   - Monitor W&B reward signal for sanity

4. **Phase 6+ (Refinement)**
   - Tune distance constants based on bot's shot selection behavior
   - Adjust TOUCH_BONUS based on whether intent gating helps

---

## Tuning Recommendations

### Initial Hyperparameters (Strategy 4)
```cpp
TOUCH_BONUS = 0.3f              // Modest intent boost
OPTIMAL_SHOT_DISTANCE = 2000.0f // Rocket League goal box ~1500-2500 UU
MAX_REWARD_DISTANCE = 5000.0f   // Rough mid-field range
MIN_ALIGNMENT = 0.2f            // ~78° angle minimum (still fairly forgiving)
```

### Overfitting Test Protocol
1. Set `VelocityBallToGoalReward` weight to 100x normal for all phases
2. Train for 500M-1B steps (sample against current bot baseline)
3. Check replays:
   - Does bot take higher-quality shots?
   - Does bot avoid low-percentage plays?
   - Or does it just spam shots indiscriminately?
4. **Good sign**: Bot improves shot placement, becomes more selective  
   **Bad sign**: Bot shoots constantly regardless of situation

---

## Expected Impact

### Conservative Estimate (Distance Weighting Only)
- **Positive**: ~2-5% improvement in shot quality, slightly better positioning
- **Neutral**: No major risk, preserves most of current signal
- **Time to integrate**: 1-2B steps

### Aggressive Estimate (Composite with Tuning)
- **Positive**: ~5-15% improvement in shot accuracy, better decision-making under pressure
- **Risk**: Over-gating could reduce aggressive play in some phases
- **Time to integrate**: 5-10B steps (with gradual scaling)

---

## Alternative: Pearl / Replay-Based Rewards
As mentioned in your roadmap, **Pearl** (replay-based NGP with Shapley value rewards) would eventually replace hand-crafted signals like this. For now, these improvements buy you higher-quality learning signal with your current PPO pipeline.

---

## Questions to Answer Before Implementation

1. **What's the current W&B average for VelocityBallToGoalReward?** (Should be ~0.3-0.5 if well-scaled)
2. **Do replays show the bot taking low-probability shots?** (Indicates over-reward of any goal-ward motion)
3. **How aggressive should intent gating be?** (Should passive rolling be rewarded at all?)
4. **Penalty for backward motion?** (Should defensive clearances that go toward own goal be explicitly punished?)

