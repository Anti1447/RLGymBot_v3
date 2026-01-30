# CutOutplayReward: State Machine vs. Multi-Branch Comparison

**Comparison Date**: 2026-01-30
**Current Implementation**: Multi-Branch (163 lines)
**Previous Implementation**: State Machine (Rev 4.0, ~280 lines)

---

## Quick Summary Table

| Aspect | State Machine | Multi-Branch |
|--------|---------------|--------------|
| **Code Size** | ~280 lines | ~163 lines |
| **Memory per Player** | 3 maps (1.5 KB+) | 0 (stateless) |
| **Complexity** | High (state tracking) | Medium (branching) |
| **Learning Curve** | Steep (4 phases) | Gentle (3 responses) |
| **Tuning Difficulty** | Hard (interdependent params) | Easy (independent params) |
| **Training Stability** | More stable (outcome-aware) | May overfire (immediate) |
| **Debugging** | Hard (state machines) | Easy (clear branches) |
| **Execution Speed** | O(1) per frame | O(1) per frame |
| **Reward Granularity** | 6 outcome tiers | 3 response tiers + additive |

---

## Detailed Analysis

### STATE MACHINE (Rev 4.0)

**Architecture**: Three-phase execution tracking
```
PHASE 1: Detect valid cut (0.15s possession time + challenger approaching)
  ↓
PHASE 2: Validate mechanics (30° direction change, ball acceleration, ground-level)
  ↓
PHASE 3: Track outcome over 0.6s (whiff/regain/shoot/trajectory)
  ↓
6 reward tiers based on outcome
```

**Pros**:
1. ✅ **Outcome-aware**: Verifies opponent actually whiffs, not just direction change
2. ✅ **Higher accuracy**: Won't reward failed cuts that look good initially
3. ✅ **Sophisticated evaluation**: Distinguishes successful vs failed attempts
4. ✅ **Anti-farming robust**: Checks opponent commit (hasJumped/isFlipping)
5. ✅ **Trajectory scoring**: Evaluates ball direction post-cut quality
6. ✅ **Regain tracking**: Only counts first regain (prevents spam)
7. ✅ **Proven in training**: Already tested and refined (Rev 4.0)

**Cons**:
1. ❌ **280+ lines**: Significantly larger codebase
2. ❌ **3 state maps**: Expensive memory per player (1.5 KB+)
3. ❌ **Complex state management**: Harder to debug state transitions
4. ❌ **Interdependent parameters**: Change one, affects 3+ other values
5. ❌ **Steep learning curve**: New developers need to understand state phases
6. ❌ **Tight coupling**: Outcome logic tightly bound to detection logic
7. ❌ **Over-specification**: Assumes opponent behavior (may miss some valid cuts)

**Key Parameters** (many interdependent):
```cpp
minOnBallTime = 0.15f
challengerApproachDist = 850.0f
minPrevBallVelocity = 300.0f
minDirectionChange01 = 0.165f
minBallSpeedAfterCut = 800.0f
maxBallHeightAtCut = 350.0f
rewardCutSuccessfulWhiff = 12.0f          // 6 different reward tiers
rewardCutRegainedPossession = 9.0f        // (interdependent scaling)
rewardCutShotAttempt = 11.0f
rewardCutBallTowardOpponentGoal = 6.0f
rewardCutBallNeutral = 1.5f
rewardCutBallTowardOurGoal = 0.5f
```

**Training Signal**:
- Only fires after 0.6s outcome evaluation
- High reward for successful whiffs (12.0)
- Medium reward for regains (9.0)
- Lower reward for trajectory (6.0-1.5)
- Delayed feedback loop

---

### MULTI-BRANCH (New Implementation)

**Architecture**: Three explicit response types detected per-frame
```
THREAT CHECK: Opponent distance < 1500 + velocity > 100
  ↓
CUT RESPONSE:     angle > 0.4, ground, away from opp     → 0.0-0.6
POP RESPONSE:     Vz > 300 or dz > 10, direction change  → 0.0-0.8
FAKE RESPONSE:    angle > 0.3, velocity DROPS            → 0.0-0.3
  ↓
Sum & Clamp to 0-1.0 (additive)
```

**Pros**:
1. ✅ **Simple & clean**: 163 lines vs 280+
2. ✅ **Stateless**: Zero memory overhead per player
3. ✅ **Immediate feedback**: Rewards fire same frame
4. ✅ **Easy tuning**: Each response parameter independent
5. ✅ **Clear intent**: Three distinct, easy-to-understand branches
6. ✅ **Additive system**: Multi-technique execution naturally rewarded
7. ✅ **Low learning curve**: Easy for new developers to understand
8. ✅ **Debuggable**: No state maps to inspect, just input→output

**Cons**:
1. ❌ **No outcome verification**: Rewards direction change immediately
2. ❌ **May overfire**: Can reward failed cuts that "looked good"
3. ❌ **No whiff detection**: Doesn't verify opponent actually committed
4. ❌ **Single-frame evaluation**: Misses temporal context
5. ❌ **Limited anti-farming**: Relies only on immediate constraints
6. ❌ **No trajectory tracking**: Doesn't verify ball goes away from opp goal
7. ❌ **Oversimplifies**: Treats all 30° angle changes equally

**Key Parameters** (all independent):
```cpp
THREAT_DISTANCE = 1500.0f          // Can tune independently
THREAT_VELOCITY = 100.0f            // Can tune independently
ANGLE_THRESHOLD_CUT = 0.40f         // Can tune independently
ANGLE_THRESHOLD_FAKE = 0.30f        // Can tune independently
VELOCITY_DROP_THRESHOLD = 0.90f     // Can tune independently
POP_MIN_VELOCITY_Z = 300.0f         // Can tune independently
POP_MIN_HEIGHT_GAIN = 10.0f         // Can tune independently
```

**Training Signal**:
- Immediate feedback (same frame as touch)
- Rewards any valid direction change
- No outcome verification
- Fast learning but potentially less discriminative

---

## Critical Differences

### 1. **Outcome Verification**

**State Machine**:
```
Touch → Direction change → Wait 0.6s → Check if opponent whiffed
         → Only reward if whiff occurred
```

**Multi-Branch**:
```
Touch → Direction change → Immediate reward
         → No verification opponent actually failed
```

**Impact**: State Machine prevents false positives (direction changes that fail). Multi-Branch rewards any valid direction change regardless of outcome.

### 2. **Anti-Farming Robustness**

**State Machine**:
- Checks opponent commit status (`hasJumped || isFlipping`)
- Verifies opponent didn't touch ball during window
- Tracks regain count (only count first)
- Evaluates ball trajectory afterward

**Multi-Branch**:
- Checks threat (distance + velocity)
- Checks direction change magnitude
- Checks away-movement
- No temporal verification

**Winner**: State Machine significantly stronger against farming.

### 3. **Reward Granularity**

**State Machine**: 6 tiers
- Successful whiff (12.0)
- Regained possession (9.0)
- Shot attempt (11.0)
- Toward opponent goal (6.0)
- Neutral trajectory (1.5)
- Toward our goal (0.5)

**Multi-Branch**: 3 tiers + additive
- CUT (0.0-0.6)
- POP (0.0-0.8)
- FAKE (0.0-0.3)
- Combine for 0.0-1.0

**Winner**: State Machine provides more nuanced feedback; Multi-Branch simpler but less discriminative.

### 4. **Feedback Latency**

**State Machine**: 0.6s (delayed, outcome-aware)
**Multi-Branch**: 0 frames (immediate, local-aware)

**Winner**: Multi-Branch for fast learning; State Machine for accurate learning.

---

## User's Preference: "Shorter Code, Less Complex"

Given your stated preference for **simplicity**, here's the evaluation:

| Criterion | Winner | Reason |
|-----------|--------|--------|
| **Code Length** | Multi-Branch | 163 vs 280+ lines |
| **Memory** | Multi-Branch | 0 maps vs 3 maps |
| **Complexity** | Multi-Branch | Branching vs state machines |
| **Learning Curve** | Multi-Branch | Clear branches vs phase tracking |
| **Debugging** | Multi-Branch | No state inspection needed |
| **Tuning** | Multi-Branch | Independent parameters |

**Multi-Branch wins on simplicity by a large margin.**

---

## Recommendation

### **Use Multi-Branch** if:
- ✅ Code simplicity is the priority
- ✅ You want immediate feedback loops
- ✅ Memory efficiency matters
- ✅ Easy tuning is important
- ✅ Legibility for future developers matters
- ✅ You prefer stateless designs

### **Use State Machine** if:
- ✅ Accuracy of "successful cuts" is critical
- ✅ You want to prevent false positive farming
- ✅ Outcome verification is important for learning
- ✅ You have memory budget for tracking
- ✅ Delayed feedback loops are acceptable

---

## My Suggested Decision

**RECOMMENDATION: Keep Multi-Branch Implementation**

### Reasoning:

1. **You explicitly asked for "shorter code, less complex"**
   - Multi-Branch: 163 lines, stateless
   - State Machine: 280+ lines, 3 state maps
   - Multi-Branch is 40%+ shorter

2. **Stateless design is cleaner**
   - Multi-Branch: No state management complexity
   - State Machine: Requires careful state transition debugging
   - Multi-Branch easier to reason about

3. **Tuning is independent**
   - Multi-Branch: Adjust threat distance without affecting response weights
   - State Machine: Change one param, affects multiple reward tiers
   - Multi-Branch much more maintainable

4. **Immediate feedback is valuable**
   - Multi-Branch fires same-frame, allowing quick iteration
   - State Machine's 0.6s delay may slow training discovery
   - Multi-Branch better for exploratory learning

5. **Anti-farming is "good enough"**
   - Multi-Branch has 4 simultaneous constraint checks:
     - Threat verification (distance + velocity)
     - Direction change magnitude
     - Away-movement
     - Ball speed
   - This is solid protection against trivial farming

### Risk Mitigation:

If you're concerned about Multi-Branch not catching failed cuts:

**Hybrid Option**: Add a lightweight trajectory check in Multi-Branch:
```cpp
// In GetReward(), before returning reward:
Vec oppGoal = (player.team == Team::BLUE) ?
    CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
Vec dirToGoal = SafeNorm(oppGoal - state.ball.pos);
float towardGoal = curV.Dot(dirToGoal);
if (towardGoal < -500.0f) return 0.0f;  // Ball toward our goal = bad cut
```

**This adds 5 lines but catches the most obvious failure mode.**

---

## Conclusion

**Current Implementation (Multi-Branch) is the better choice** for your stated goals:
- ✅ Shorter: 163 vs 280+ lines
- ✅ Simpler: Stateless vs 3-map state machine
- ✅ Cleaner: Clear branching vs phase tracking
- ✅ Maintainable: Independent parameters
- ✅ Debuggable: No state inspection needed

**Keep the Multi-Branch implementation.** If training results suggest it's too permissive, add the simple 5-line trajectory check mentioned above.

---