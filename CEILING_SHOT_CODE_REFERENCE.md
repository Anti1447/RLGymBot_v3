# Ceiling Shot Reward - Code Reference Guide

## Quick Reference: Key Methods

### 1. Ceiling Contact Detection (THE FIX)
```cpp
// OLD (BROKEN): Checked position only
bool onCeiling = (player.pos.z > (CommonValues::CEILING_Z - 200.0f) && !player.isOnGround);

// NEW (CORRECT): Checks actual wheel contact
bool IsTruelyOnCeiling(const Player& player) const {
    return player.worldContact.hasContact &&
           player.worldContact.contactNormal.z < -0.5f &&
           !player.isOnGround;
}
```

**Why This Matters**: A player can be at z=1844+ without actually touching the ceiling. This checks the surface normal (which points downward when on ceiling) to verify true contact.

---

### 2. Velocity Alignment (THE BIG WIN)
```cpp
// OLD: No velocity checks at all
// NEW: Both direction AND magnitude matter
float GetVelocityAlignment(const Player& player, const GameState& state) const {
    // Direction: are we falling in the same direction as the ball?
    Vec ballVelDir = state.ball.vel.Normalized();
    Vec playerVelDir = player.vel.Normalized();
    float directionAlignment = playerVelDir.Dot(ballVelDir);  // 1.0 = perfect match

    // Magnitude: are we falling at the same speed?
    float ballSpeed = state.ball.vel.Length();
    float playerSpeed = player.vel.Length();
    float speedRatio = (ballSpeed > 0.1f) ? playerSpeed / ballSpeed : 1.0f;
    float magnitudeSimilarity = 1.0f - RS_CLAMP(fabs(1.0f - speedRatio), 0.0f, 1.0f);

    // Both must be high for proper ceiling shot
    // Example: if direction=0.9 and speed=0.8, result = 0.85 (ready to flip!)
    return (directionAlignment + magnitudeSimilarity) * 0.5f;
}
```

**Why This Matters**: A ceiling shot requires falling WITH the ball, not just being on the ceiling. If direction is good but speed is wrong (or vice versa), the shot will be inaccurate. This validates both conditions.

---

### 3. Proper Proximity Zones
```cpp
// OLD: 1000 UU sphere (unrealistic)
// NEW: Strict ±400 UU in all directions
bool IsBallProperlyPositioned(const Player& player, const GameState& state) const {
    float horizDist = GetHorizontalDistance(player, state);    // x-y plane
    float vertDist = GetVerticalDistance(player, state);        // z direction

    return horizDist <= 400.0f && fabs(vertDist) <= 400.0f;
}

float GetHorizontalDistance(const Player& player, const GameState& state) const {
    float dx = player.pos.x - state.ball.pos.x;
    float dy = player.pos.y - state.ball.pos.y;
    return sqrt(dx * dx + dy * dy);
}

float GetVerticalDistance(const Player& player, const GameState& state) const {
    // Positive = player below ball (correct positioning)
    return player.pos.z - state.ball.pos.z;
}
```

**Why This Matters**: The user specified ceiling shots need the player 200-400 UU away. This enforces that constraint and prevents "cheese" solutions where reward triggers from far away.

---

### 4. Three-Stage Evaluation (THE GRADIENT SMOOTHING)

#### Stage 1: Setup (0 → 1.0)
```cpp
float EvaluateSetup(const Player& player, const GameState& state) const {
    // Checks:
    // 1. Player on wall (near SIDE_WALL_X or BACK_WALL_Y)
    // 2. Ball elevated (z > CEILING_Z - 200)
    // 3. Player moving toward ball (velocity dot > 500)

    if (!IsOnWall(player) || !IsBallElevated(state)) {
        return 0.0f;
    }

    if (IsPlayerMovingTowardBall(player, state)) {
        return 1.0f;  // Setup complete
    }

    return 0.3f;  // Partial credit for being in position
}
```

#### Stage 2: Contact & Alignment (1.0 → 3.0)
```cpp
float EvaluateContact(const Player& player, const GameState& state) const {
    // Checks:
    // 1. True ceiling contact (contact normal)
    // 2. Ball proximity (±400 UU)
    // 3. Velocity alignment (direction + magnitude)
    // 4. Flip available (hasn't been used yet)

    if (!IsTruelyOnCeiling(player)) {
        return 0.0f;
    }

    float contactReward = 1.0f;  // Base for being on ceiling

    if (IsBallProperlyPositioned(player, state)) {
        contactReward += 0.5f;  // Proximity bonus
    }

    float alignmentScore = GetVelocityAlignment(player, state);
    if (alignmentScore > 0.8f) {
        // Scale from 0.8→1.0 = 0.0→1.0 reward
        contactReward += RS_CLAMP(alignmentScore - 0.8f, 0.0f, 1.0f);
    }

    if (player.HasFlipOrJump()) {
        contactReward += 0.3f;
    }

    return RS_CLAMP(contactReward, 1.0f, 3.0f);
}
```

#### Stage 3: Execution (3.0 → 5.0+)
```cpp
float EvaluateExecution(const Player& player, const GameState& state) const {
    // Checks:
    // 1. Just touched ball (!player.isOnGround && player.ballTouchedStep)
    // 2. Flip was used (!player.HasFlipOrJump())
    // 3. Ball headed toward goal (alignment > 0.6)
    // 4. Ball speed bonuses (1500+, 2000+ UU/s)

    if (player.isOnGround || !player.ballTouchedStep) {
        return 0.0f;
    }

    float executionReward = 3.0f;  // Base

    if (!player.HasFlipOrJump()) {
        executionReward += 1.5f;  // Flip was used
    }

    Vec targetGoal = (player.team == Team::BLUE) ?
        CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

    Vec ballToGoal = (targetGoal - state.ball.pos).Normalized();
    float goalAlignment = state.ball.vel.Normalized().Dot(ballToGoal);

    if (goalAlignment > 0.6f) {
        executionReward += 0.5f;
        if (goalAlignment > 0.85f) {
            executionReward += 0.5f;
        }
    }

    float ballSpeed = state.ball.vel.Length();
    if (ballSpeed > 1500.0f) executionReward += 0.25f;
    if (ballSpeed > 2000.0f) executionReward += 0.25f;

    return executionReward;
}
```

---

### 5. Main GetReward() Logic
```cpp
virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
    uint32_t id = player.carId;

    // Initialize per-player tracking
    if (stageProgress.find(id) == stageProgress.end()) {
        stageProgress[id] = 0.0f;
        completeSetupValid[id] = false;
        onCeilingPreviousFrame[id] = false;
    }

    // Track explicit metrics for debugging
    if (IsTruelyOnCeiling(player)) {
        truelyOnCeilingFrameCount[id]++;

        if (IsBallProperlyPositioned(player, state)) {
            onCeilingWithBallNearbyCount[id]++;
        }
    }

    float reward = 0.0f;

    // Evaluate each stage
    float setupReward = EvaluateSetup(player, state);
    if (setupReward > 0.0f) {
        completeSetupValid[id] = true;
    }

    // Only advance through stages if prior conditions are met
    if (completeSetupValid[id]) {
        float contactReward = EvaluateContact(player, state);
        float executionReward = 0.0f;

        // Only allow execution if contact is established
        if (contactReward > 1.0f) {
            executionReward = EvaluateExecution(player, state);
        }

        // Use highest-stage reward achieved (continuous accumulation)
        if (executionReward > 0.0f) {
            reward = executionReward;
        } else if (contactReward > 0.0f) {
            reward = contactReward;
        } else if (setupReward > 0.0f) {
            reward = setupReward;
        }
    } else {
        // Still trying to set up
        reward = setupReward;
    }

    // Reset if player lands or falls off ceiling
    if (player.isOnGround) {
        completeSetupValid[id] = false;
        stageProgress[id] = 0.0f;
    }

    onCeilingPreviousFrame[id] = IsTruelyOnCeiling(player);

    return reward;
}
```

**The Key Pattern**: Only progress to next stage if previous stage's conditions are met. This prevents the agent from getting lucky rewards without actually learning the technique.

---

### 6. Metrics Accessors
```cpp
// These can be logged to W&B or inspected during training
uint64_t GetTruelyOnCeilingFrames(const Player& player) const {
    auto it = truelyOnCeilingFrameCount.find(player.carId);
    return (it != truelyOnCeilingFrameCount.end()) ? it->second : 0;
}

uint64_t GetCeilingWithBallNearbyFrames(const Player& player) const {
    auto it = onCeilingWithBallNearbyCount.find(player.carId);
    return (it != onCeilingWithBallNearbyCount.end()) ? it->second : 0;
}
```

**Usage**: If these metrics aren't incrementing, state detection is broken and needs debugging.

---

## Comparison: Old vs New Reward Shape

### Old System (Discrete Jumps)
```
Frame 5:  Setup complete → 5.0 reward (JUMP!)
Frame 10: Ball touched   → 20.0 reward (JUMP!)
Frame 12: Flip used      → 8.0 reward (WEIRD JUMP DOWN)
Frame 13: All conditions → 15.0 reward (ANOTHER JUMP)

Result: Confusing learning signal, non-differentiable, hard for gradient descent
```

### New System (Smooth Gradient)
```
Frame 1:  Setup partial   → 0.3 reward
Frame 5:  Setup complete  → 1.0 reward (smooth transition)
Frame 6:  Contact made    → 1.5 reward (smooth transition)
Frame 10: Aligned well    → 2.8 reward (smooth transition)
Frame 12: Executing       → 5.2 reward (smooth transition)

Result: Clean learning signal, differentiable, easy for gradient descent
```

The gradient is continuous throughout, letting the PPO algorithm get clear learning signals at every step.

---

## Common Issues & Fixes

### Issue 1: "Reward never triggers"
**Check**: Are `truelyOnCeilingFrameCount` metrics incrementing?
- If NO: `IsTruelyOnCeiling()` isn't working. Debug contact normal.
- If YES but reward is still 0: Check `completeSetupValid` flag. Setup stage may not be completing.

### Issue 2: "Agent doesn't attempt ceiling shots"
**Check**: Is setup reward (0-1.0) triggering?
- If NO: Agent never enters the ceiling shot pipeline. Debug `IsOnWall()` and `IsBallElevated()`.
- If YES but agent ignores it: Reward weight too low. Increase from default.

### Issue 3: "Reward spikes are too frequent"
**Check**: Is `IsTruelyOnCeiling()` firing too often?
- Possible causes: Contact normal threshold too loose, or player is grazing ceiling unintentionally.
- Fix: Increase threshold to require more solid contact.

### Issue 4: "Agent gets stuck trying ceiling shots"
**Check**: Is stage progression enforced?
- If agent gets stuck at contact stage: Velocity alignment may be hard. Lower the 0.8 threshold temporarily for training.
- If agent gets stuck at execution: Goal alignment check may be too strict. Lower 0.6 threshold.

---

## Tuning Guide

### Reward Magnitudes
The system is designed to produce rewards in this range:
- **Setup**: 0.0 - 1.0
- **Contact**: 1.0 - 3.0
- **Execution**: 3.0 - 5.5

If all rewards are too high (agent does nothing else), divide by 2.
If all rewards are too low (agent ignores ceiling shots), multiply by 2.

### Thresholds for Debugging
Key thresholds that control activation:

| Threshold | Current | To Loosen | To Tighten |
|-----------|---------|-----------|-----------|
| Ceiling contact normal | z < -0.5 | z < -0.3 | z < -0.7 |
| Proximity range | ±400 UU | ±600 UU | ±200 UU |
| Velocity alignment ready | >= 0.8 | >= 0.7 | >= 0.9 |
| Goal alignment | > 0.6 | > 0.4 | > 0.8 |
| Wall proximity | < 500 UU | < 800 UU | < 300 UU |
| Ball elevation | z > 1844 | z > 1700 | z > 1900 |

---

## Testing Checklist

- [ ] Code compiles without errors
- [ ] `IsTruelyOnCeiling()` activates only with proper contact normal (test with position vs actual contact)
- [ ] `GetVelocityAlignment()` returns high values (0.8+) only when direction AND speed match
- [ ] Stage progression enforces order (can't jump to execution without setup)
- [ ] Metrics increment correctly during ceiling shot attempts
- [ ] Reward smoothly increases 0 → 1 → 3 → 5+ (no sudden jumps)
- [ ] Agent learns to attempt ceiling shots more frequently over time
- [ ] Win rate improves after ceiling shot training activates

---

**Last Updated**: 2026-02-17
**Status**: Ready for Integration & Testing
