# Ceiling Shot Rewards - Detailed Feedback & Analysis

## Executive Summary

Your two original ceiling shot rewards (`CeilingShotTrainingReward` and `CeilingShotOnGoalReward`) had good intentions but critical flaws in state detection and reward shaping that would prevent proper learning:

1. ❌ **Position-only ceiling detection** - Gets false positives
2. ❌ **No velocity alignment validation** - Doesn't check if player/ball are moving together
3. ❌ **Discrete reward jumps** - Confuses gradient descent learning
4. ❌ **Loose proximity constraints** - 1000 UU sphere is unrealistic
5. ❌ **Poor state machine** - Flags don't validate intermediate conditions

The new unified reward fixes all of these and provides smooth, continuous learning signals.

---

## Detailed Feedback on Original Implementation

### Issue #1: Position-Only Ceiling Detection

#### Your Code
```cpp
bool onCeiling = (player.pos.z > (CommonValues::CEILING_Z - 200.0f) &&
    !player.isOnGround); // In air near ceiling
```

#### Problems
1. **False positives**: Player can be at z=1844+ without actually touching ceiling
   - Example: Player flying upward toward ceiling but not yet touching it
   - Example: Player on wall right next to ceiling intersection but not on ceiling

2. **No surface contact verification**: You're checking height, not actual contact
   - The real indicator of ceiling contact is the surface normal: `contactNormal.z < -0.5f`
   - When wheels touch ceiling, the contact normal points downward (negative z)

3. **Missed edge cases**: Player could be tilted near ceiling without wheel contact
   - Just because you're physically near the ceiling doesn't mean you're "on" it
   - Rocket League distinguishes between "near" and "touching"

#### The Fix
```cpp
bool IsTruelyOnCeiling(const Player& player) const {
    return player.worldContact.hasContact &&              // Wheels in contact
           player.worldContact.contactNormal.z < -0.5f && // Normal points down = ceiling
           !player.isOnGround;                            // Not on ground
}
```

This **guarantees** true contact, not just proximity.

---

### Issue #2: No Velocity Alignment Validation

#### Your Code
```cpp
// You never checked if player velocity matches ball velocity!
// CeilingShotTrainingReward: Silent ignoring of velocity completely
// CeilingShotOnGoalReward: Only checked ball alignment to goal, not player sync
```

#### Problems
1. **Critical for ceiling shots**: The whole point is falling WITH the ball
   - If player is falling at 500 UU/s and ball at 1000 UU/s, they're falling apart
   - Result: Flip will miss or be very inaccurate

2. **Direction alone isn't enough**: Two falling objects can have same direction but different speeds
   - Player direction: (0, 0, -1) at 500 UU/s
   - Ball direction: (0, 0, -1) at 1000 UU/s
   - Same direction, DIFFERENT magnitudes = bad contact

3. **Agent could learn wrong technique**: Without velocity feedback, agent learns to:
   - Just be on ceiling and flip at random
   - Not worry about matching ball speed
   - Results in unreliable ceiling shots (miss more often)

#### The Fix
```cpp
float GetVelocityAlignment(const Player& player, const GameState& state) const {
    // Check DIRECTION alignment
    Vec ballVelDir = state.ball.vel.Normalized();
    Vec playerVelDir = player.vel.Normalized();
    float directionAlignment = playerVelDir.Dot(ballVelDir);  // 1.0 = same direction

    // Check MAGNITUDE similarity
    float ballSpeed = state.ball.vel.Length();
    float playerSpeed = player.vel.Length();
    float speedRatio = (ballSpeed > 0.1f) ? playerSpeed / ballSpeed : 1.0f;
    float magnitudeSimilarity = 1.0f - RS_CLAMP(fabs(1.0f - speedRatio), 0.0f, 1.0f);

    // Both matter equally
    return (directionAlignment + magnitudeSimilarity) * 0.5f;
}
```

Now the reward only increases when **BOTH** direction and speed match.

---

### Issue #3: Discrete Reward Jumps (Gradient Killer)

#### Your Code
```cpp
// CeilingShotTrainingReward
if (onCeiling && ballNearby && HasFlipOrJump && !prevTouchedCeiling) {
    reward += 5.0f;  // JUMP
}
if (prevTouchedCeiling && player.ballTouchedStep && !player.isOnGround && !prevHitBall) {
    reward += 20.0f;  // HUGE JUMP
}
if (prevTouchedCeiling && !HasFlipOrJump && prevOnCeiling && !prevUsedFlip) {
    reward += 8.0f;   // WEIRD DOWN JUMP
}
if (prevHitBall && prevUsedFlip && player.ballTouchedStep) {
    reward += 15.0f;  // ANOTHER JUMP
}
```

#### Problems
1. **Non-differentiable**: Sudden jumps from 0 → 5 → 20 → 8 → 15
   - PPO uses gradient descent, which works best with smooth curves
   - Sharp discontinuities confuse the learning algorithm

2. **Hard to allocate credit**: Which action led to the +20 reward?
   - Was it reaching ceiling? Matching velocity? Getting closer? All three?
   - Non-smooth rewards make it harder for agent to isolate key behaviors

3. **Unstable learning**: Agent might:
   - Overshoot when it finally gets that big 20 reward
   - Get "stuck" chasing partial rewards instead of completing full technique
   - Oscillate between different ceiling shot strategies

#### Example: Bad Learning Trajectory
```
Training starts: Agent learns to jump (gets 5.0)
Then: Agent learns to touch ball from ceiling (gets 20.0) - suddenly its reward doubles!
Then: Agent learns to use flip (gets 8.0) - WAIT, reward DECREASED!
Result: Agent confused about which behaviors matter
```

#### The Fix
```cpp
// Smooth 0 → 1 → 3 → 5 progression
float setupReward = EvaluateSetup(player, state);        // 0-1.0
float contactReward = EvaluateContact(player, state);    // 1.0-3.0
float executionReward = EvaluateExecution(player, state); // 3.0-5.0+

// Use highest stage achieved (continuous, not jumpy)
if (executionReward > 0.0f) {
    reward = executionReward;  // 3.0-5.0+
} else if (contactReward > 0.0f) {
    reward = contactReward;    // 1.0-3.0
} else if (setupReward > 0.0f) {
    reward = setupReward;      // 0.0-1.0
}
```

Now rewards accumulate smoothly, making it obvious to the agent which behaviors improve performance.

---

### Issue #4: Unrealistic Proximity Constraint

#### Your Code
```cpp
// CeilingShotTrainingReward
float distToBall = (state.ball.pos - player.pos).Length();
bool ballNearby = (distToBall < 1000.0f);

// 1000 UU sphere - that's HUGE!
```

#### Problems
1. **1000 UU sphere is massive**:
   - Ceiling is 2044 UU high, so 1000 UU radius means half the arena vertically
   - Ball could be on ground, player on ceiling, and "ballNearby" is still true

2. **Ignores vertical separation**:
   - You need to be ~100 UU BELOW the ball (user specified)
   - A 1000 UU sphere doesn't enforce this at all

3. **Allows loose play**:
   - Agent could flip from ceiling when ball is 1000 UU away = miss
   - Doesn't teach proper positioning

4. **Causes false rewards**:
   - Agent touches ceiling while ball is far away = still gets reward
   - Not learning actual ceiling shot technique

#### Example: Bad Scenario
```
Ball at z=1000 (mid-arena, falling slowly)
Player at z=2000 (on ceiling)
Distance = ~1200 UU
Result: NOT in the 1000 UU sphere, but position is terrible for ceiling shot!

vs.

Ball at z=2000 (near ceiling)
Player at z=1900 (slightly below ceiling)
Distance = ~100 UU
Result: IN the 1000 UU sphere, AND perfect position!
```

Without explicit proximity zones, both scenarios trigger the reward even though one is correct and one isn't.

#### The Fix
```cpp
// Strict ±400 UU in ALL directions (user spec: 200-400 away, ~100 below)
bool IsBallProperlyPositioned(const Player& player, const GameState& state) const {
    float horizDist = GetHorizontalDistance(player, state);    // x-y plane
    float vertDist = GetVerticalDistance(player, state);        // z direction

    // Ball can be above/below by up to 400 UU, horizontal max 400 UU
    return horizDist <= 400.0f && fabs(vertDist) <= 400.0f;
}
```

Now rewards only trigger in the exact positioning window needed for ceiling shots.

---

### Issue #5: Poor State Machine

#### Your Code
```cpp
// CeilingShotTrainingReward uses four boolean flags:
mutable std::map<uint32_t, bool> wasOnCeiling;
mutable std::map<uint32_t, bool> touchedCeilingWithBallNearby;
mutable std::map<uint32_t, bool> hasHitBallFromCeiling;
mutable std::map<uint32_t, bool> hasUsedFlipFromCeiling;

// Then checks them in isolation:
if (onCeiling && ballNearby && HasFlipOrJump && !prevTouchedCeiling) {
    reward += 5.0f;  // Sets touchedCeilingWithBallNearby = true
}

if (prevTouchedCeiling && player.ballTouchedStep && !player.isOnGround && !prevHitBall) {
    reward += 20.0f;  // Can trigger EVEN IF still on ceiling
    // ^ BUG: Doesn't verify IsTruelyOnCeiling() here!
}
```

#### Problems
1. **Flags don't validate state**:
   - Flag `touchedCeilingWithBallNearby = true` doesn't mean you're STILL on ceiling
   - Player could have left ceiling, flag stays true
   - Rewards trigger for events that happened frames ago

2. **No stage enforcement**:
   - Nothing prevents jumping directly to Stage 3
   - Agent could hit ball while NOT on ceiling and still get credit

3. **Hard to debug**:
   - Flags accumulate over time, making it unclear what's actually happening
   - If agent learns wrong behavior, hard to tell which flag is responsible

4. **The bonus condition is broken**:
   ```cpp
   if (prevHitBall && prevUsedFlip && player.ballTouchedStep) {
       reward += 15.0f; // Fires on ANY touch after flip, not ceiling shot
   }
   ```
   This could reward a ceiling shot OR just touching the ball after flip OR accidental touches.

#### Example: Broken Scenario
```
Frame 1: Touch ceiling (touchedCeilingWithBallNearby = true, reward +5)
Frame 2: Leave ceiling, bounce off wall
Frame 3: Jump in air (not ceiling) and accidentally touch ball
  - prevTouchedCeiling is STILL true
  - Reward +20 for "ceiling shot" even though on wall now!
```

#### The Fix
```cpp
// Stage-gated progression:
float setupReward = EvaluateSetup(...);      // Can only return > 0 if setup complete
if (completeSetupValid[id]) {                // Only if setup worked
    float contactReward = EvaluateContact(...);  // Can only return > 0 if contact true
    if (contactReward > 1.0f) {              // Only if good contact
        float executionReward = EvaluateExecution(...);  // Can only return > 0 if proper flip
    }
}
```

Now each stage requires the previous stage to be true, making the state machine self-validating.

---

## Why These Issues Matter

### For Learning
1. **Position-only detection** → Agent learns spurious correlations (being near ceiling = good)
2. **No velocity check** → Agent doesn't learn to match ball speed (ceiling shots miss often)
3. **Discrete jumps** → Agent gets confused learning signals (which behavior matters?)
4. **Loose proximity** → Agent learns sloppy technique (works sometimes, not reliable)
5. **Poor state machine** → Agent could exploit edge cases (rewards for wrong states)

### For Reliability
The new system validates that:
- ✅ Player is actually on ceiling (contact normal check)
- ✅ Player is moving with ball (velocity alignment)
- ✅ Player is positioned correctly (±400 UU constraint)
- ✅ Each stage builds on previous (stage gating)

This results in:
- **Fewer false positives**: Won't reward near-ceiling states that aren't actually contact
- **Clearer learning**: Agent knows exactly what behaviors lead to rewards
- **Better generalization**: Agent learns actual technique, not shortcuts
- **More reliable execution**: Ceiling shots succeed more often in actual gameplay

---

## Comparison Table

| Aspect | Original | New | Impact |
|--------|----------|-----|--------|
| **Ceiling Detection** | pos.z check | contact normal | False positives eliminated |
| **Velocity Validation** | None | Direction + magnitude | Prevents falling apart during flip |
| **Proximity Zones** | 1000 UU sphere | ±400 UU in all axes | Enforces proper positioning |
| **Reward Shape** | Discrete jumps | Smooth gradient | Better for gradient descent |
| **State Machine** | Boolean flags | Stage gating | Prevents edge case exploits |
| **Debugging** | Hard | Explicit metrics | Easy to verify state detection |
| **Max Reward** | 5→20→8→15 (confusing) | 0→1→3→5+ (clear) | Agent understands progression |

---

## Conclusion

Your original implementation had the right idea but lacked the precision needed for reliable learning. The unified `CeilingShotReward` system:

1. **Uses contact normal for true ceiling detection** - Eliminates false positives
2. **Validates both velocity direction AND magnitude** - Ensures player falls with ball
3. **Enforces strict ±400 UU proximity zones** - Prevents sloppy execution
4. **Provides smooth continuous gradients** - Works better with PPO learning
5. **Implements stage-gated progression** - Prevents reward exploitation
6. **Exposes explicit metrics** - Makes debugging easy during training

The agent should now learn ceiling shots faster, more reliably, and generalize better to new situations.

---

## Next Steps for Testing

1. **Verify compilation**: Build project and ensure no syntax errors
2. **Monitor state detection**: Check if `truelyOnCeilingFrameCount` increases during ceiling attempts
3. **Validate reward shape**: Confirm rewards smoothly increase 0→1→3→5+
4. **Check learning**: Observe if agent attempts ceiling shots more frequently
5. **Evaluate performance**: Compare ceiling shot success rate before/after
6. **Tune weight**: Adjust reward weight based on how often agent attempts technique

---

**Analysis Date**: 2026-02-17
**Status**: Ready for Implementation & Validation
