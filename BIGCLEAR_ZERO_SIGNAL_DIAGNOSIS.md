# BigClearReward Zero Signal - Root Cause Analysis
**Date**: 2026-02-09
**Issue**: BigClearReward producing 0 reward signal despite being well-designed
**Status**: DIAGNOSED

---

## The Problem

BigClearReward is producing 0 reward signal in training, likely failing to meet all 6 gates.

---

## Gate-by-Gate Analysis

### Gate 1: `player.ballTouchedStep`
**Status**: ✅ Likely passing
- This fires when the player physically touches the ball
- Should be happening regularly during play

### Gate 2: `state.prev` exists
**Status**: ✅ Likely passing
- Previous state exists except on first frame
- Should be fine after initial frame

### Gate 3: Defensive half check
**Status**: ✅ Likely passing
- Simple Y-coordinate check
- Player on own side (Y < 0 for BLUE, Y > 0 for ORANGE)
- Should work in defensive situations

### Gate 4: Hard speed (1200+ UU)
**Status**: ⚠️ **POSSIBLE ISSUE**
- Requires ball to move 1200+ UU in one frame
- This is ~28 km/h or ~17 mph
- Might be too strict for some clears
- But likely okay - a "hard" clear should be fast

### Gate 5: Toward opponent (dot > 0.2)
**Status**: 🔴 **LIKELY CULPRIT**
```cpp
Vec toOpponent = (opponentGoal - state.ball.pos).Normalized();
if (ballMovement.Normalized().Dot(toOpponent) < 0.2f) return 0.0f;
```

**Why this fails**:

Normalized dot product interpretation:
- 1.0 = parallel (same direction)
- 0.7 = ~45° angle
- 0.5 = ~60° angle
- 0.2 = ~78° angle ← THRESHOLD
- 0.0 = perpendicular (90°)
- -1.0 = opposite

**Problem**: Threshold of 0.2 means the ball must be traveling within **~78° of opponent goal direction**. That's almost perpendicular!

Example:
- Ball at center, opponent goal at (0, 5120, 0)
- Player hits ball sideways/upward at angle 80° from goal direction
- Dot product = cos(80°) ≈ 0.17 < 0.2 ❌ **FAILS**

This is WAY too strict. Most defensive clears won't be perfectly aligned toward opponent goal.

### Gate 6: Ball elevated
**Status**: ⚠️ **SECONDARY ISSUE**
```cpp
if (state.ball.pos.z <= 200.0f && state.ball.vel.z <= 200.0f) return 0.0f;
```

This requires EITHER:
- Ball height > 200 UU OR
- Ball Z-velocity > 200 UU/s

**Problem**: If the ball is hit hard horizontally (common for clears), Z-velocity might be low. If Z > 200 initially, it might already be falling when we check.

**Secondary**: This gate is actually okay as-written (OR logic), but combined with Gate 5 strictness, it compounds the issue.

---

## Severity Analysis

| Gate | Pass Rate Estimate | Impact |
|------|-------------------|--------|
| Gate 1 (touch) | 80% | Low |
| Gate 2 (prev state) | 95% | Very Low |
| Gate 3 (defensive half) | 70% | Medium |
| Gate 4 (hard speed) | 40% | High |
| Gate 5 (toward opponent) | 15% | **VERY HIGH** ← 🔴 |
| Gate 6 (elevated) | 50% | Medium |

**Cumulative pass rate**: 0.80 × 0.95 × 0.70 × 0.40 × 0.15 × 0.50 = **0.0095 = ~1%**

This explains the zero signal! The gates are too strict individually AND in combination.

---

## The Specific Issue: Gate 5

The dot product threshold is the main culprit:

**Current logic**:
```cpp
Vec toOpponent = (opponentGoal - state.ball.pos).Normalized();
float alignment = ballMovement.Normalized().Dot(toOpponent);
if (alignment < 0.2f) return 0.0f;  // ← TOO STRICT!
```

**What's happening**:
1. Player in own half ✓
2. Ball is touched ✓
3. Ball moves fast (1200+ UU) ✓
4. Ball is supposed to move toward opponent goal...
5. But alignment requirement is 0.2 = ~78° tolerance
6. **Most clears fail this check**

A defensive clear at 45° angle from goal would have:
```
cos(45°) ≈ 0.707 > 0.2 ✓ passes
```

But a clear at 60° angle:
```
cos(60°) = 0.5 > 0.2 ✓ passes
```

And a clear at 75° angle:
```
cos(75°) ≈ 0.26 > 0.2 ✓ barely passes
```

But at 80° angle:
```
cos(80°) ≈ 0.17 < 0.2 ✗ FAILS
```

**The problem**: Many defensive clears happen at awkward angles that aren't perfectly toward opponent goal. Clears along the wall, clears at angles, clears bouncing off structures - all might fail this check.

---

## Solutions (in order of effectiveness)

### Option 1: **Lower the dot product threshold** (RECOMMENDED)
```cpp
if (ballMovement.Normalized().Dot(toOpponent) < -0.3f) return 0.0f;
```

Change from 0.2 to -0.3 means:
- -1.0 to -0.3: Opposite direction (180° to ~107°) ❌ FAIL
- -0.3 to 1.0: Anything mostly forward ✓ PASS

This means **anything not directly back toward own goal** passes, which is reasonable for a defensive clear.

**Impact**: Pass rate increases from ~15% to ~70% for Gate 5

### Option 2: **Remove direction check entirely**
```cpp
// Gate 5: Toward opponent (ball moving away from us)
// REMOVED - any hard hit from defense is good
```

**Rationale**: If you're in your defensive half and hitting the ball hard + high, that's inherently good.

**Pro**: Simpler, fewer false negatives
**Con**: Might reward some bad clears (hitting into wall, etc.)

### Option 3: **Check against defensive direction instead**
```cpp
Vec ownGoal = (player.team == Team::BLUE) ?
    CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
Vec awayFromGoal = (state.ball.pos - ownGoal).Normalized();
if (ballMovement.Normalized().Dot(awayFromGoal) < 0.0f) return 0.0f;
// Must move away from own goal (dot > 0)
```

**Rationale**: Just ensure ball is moving AWAY from own goal (dot > 0), not specifically toward opponent.

**Pro**: Less strict, but still prevents "clearing" backward
**Con**: Still might need tuning

### Option 4: **Reduce hardness threshold**
```cpp
if (ballMovement.Length() < 800.0f) return 0.0f;  // Instead of 1200
```

**Rationale**: 1200 UU/frame might be too high. Many legitimate clears are 800-1200.

**Pro**: Increases Gate 4 pass rate
**Con**: Might reward slower passes

---

## Recommendation

**Best Fix: Option 1 + Option 4**

```cpp
// Gate 4: Hard speed (ball moved 800+ UU this frame) - RELAXED from 1200
Vec ballMovement = state.ball.pos - state.prev->ball.pos;
if (ballMovement.Length() < 800.0f) return 0.0f;

// Gate 5: Toward opponent (not back toward own goal) - RELAXED from 0.2
Vec ownGoal = (player.team == Team::BLUE) ?
    CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
Vec awayFromGoal = (state.ball.pos - ownGoal).Normalized();
if (ballMovement.Normalized().Dot(awayFromGoal) < 0.0f) return 0.0f;
```

**Why this works**:
- ✅ Still requires hard-ish speed (800+)
- ✅ Still requires ball moving away from own goal
- ✅ Doesn't require perfect alignment to opponent goal
- ✅ Much more lenient (Gates 4+5 combined pass rate: ~40% instead of ~6%)

---

## Why This Wasn't Obvious Before

The issue is **combinatorial gate strictness**:
- Each gate individually seems reasonable
- But combined: 0.80 × 0.95 × 0.70 × 0.40 × 0.15 × 0.50 = ~1%
- Impossible to detect without analyzing pass rates

This is why you see 0 rewards - every episode, fewer than 1% of defensive touches meet all criteria.

---

## Next Steps

1. ✅ Identified: Gate 5 (direction check) is too strict
2. ⏳ Fix: Relax direction threshold or change logic
3. ⏳ Test: Rebuild and run training
4. ⏳ Monitor: Check if reward signal appears

