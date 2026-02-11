# BigClearReward vs CornerClearReward Comparison
**Date**: 2026-02-09
**Purpose**: Understand design patterns and validate BigClearReward simplification

---

## Side-by-Side Comparison

### CornerClearReward (Reference Implementation)
```cpp
class CornerClearReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Gate 1: Touch ball
        if (!player.ballTouchedStep) return 0.0f;

        // Gate 2: In defensive zone (3000 UU from own goal)
        Vec myGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        float ballDistToGoal = (state.ball.pos - myGoal).Length();
        if (ballDistToGoal > 3000.0f) return 0.0f;

        // Gate 3: Ball moving toward corners
        Vec ballVelNorm = state.ball.vel.Normalized();
        Vec corners[] = {
            Vec(CommonValues::SIDE_WALL_X, myGoal.y + 2000.0f, 0),
            Vec(-CommonValues::SIDE_WALL_X, myGoal.y + 2000.0f, 0)
        };

        for (auto& corner : corners) {
            Vec toCorner = (corner - state.ball.pos).Normalized();
            float alignment = ballVelNorm.Dot(toCorner);
            if (alignment > 0.7f) {
                return 5.0f;
            }
        }
        return 0.0f;
    }
};
```

**Lines**: ~30
**Gates**: 3
**Constants**: Hardcoded (3000.0, 2000.0, 0.7f)
**Complexity**: Medium

---

### BigClearReward (Current Simplified Version)
```cpp
class BigClearReward : public Reward {
private:
    static constexpr float BASE_REWARD = 5.0f;

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Gate 1: Touch ball
        if (!player.ballTouchedStep) return 0.0f;

        // Gate 2: Previous frame exists
        if (!state.prev) return 0.0f;

        // Gate 3: In defensive half (Y < 0 for BLUE, Y > 0 for ORANGE)
        bool inDefensiveHalf = (player.team == Team::BLUE) ? (player.pos.y < 0) : (player.pos.y > 0);
        if (!inDefensiveHalf) return 0.0f;

        // Gate 4: Hard speed (ball moved 1200+ UU this frame)
        Vec ballMovement = state.ball.pos - state.prev->ball.pos;
        if (ballMovement.Length() < 1200.0f) return 0.0f;

        // Gate 5: Toward opponent
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        Vec toOpponent = (opponentGoal - state.ball.pos).Normalized();
        if (ballMovement.Normalized().Dot(toOpponent) < 0.2f) return 0.0f;

        // Gate 6: Ball elevated
        if (state.ball.pos.z <= 200.0f && state.ball.vel.z <= 200.0f) return 0.0f;

        return BASE_REWARD;
    }

    void PreStep(const GameState& state) override {}
};
```

**Lines**: ~50
**Gates**: 6
**Constants**: 1 (BASE_REWARD = 5.0f)
**Complexity**: Low (simple sequential gates)

---

## Key Differences

| Aspect | CornerClearReward | BigClearReward | Winner |
|--------|-------------------|----------------|--------|
| **Gates** | 3 | 6 | Corner (simpler) |
| **Position Check** | Distance to goal (3000 UU) | Field side (Y coordinate) | Big (faster) |
| **Speed Check** | Uses current ball velocity | Uses frame-delta velocity | Big (more precise) |
| **Direction Target** | Specific corners | Opponent goal direction | Big (more flexible) |
| **Elevation Check** | None | Z > 200 OR Z_vel > 200 | Big (ensures "clear") |
| **Requires prev state** | No | Yes | Corner (simpler) |
| **Reward Value** | 5.0 | 5.0 | Same |

---

## What BigClearReward Does Better

### 1. **Speed Check is More Precise**
**CornerClearReward**:
```cpp
Vec ballVelNorm = state.ball.vel.Normalized();  // Current velocity only
```

**BigClearReward**:
```cpp
Vec ballMovement = state.ball.pos - state.prev->ball.pos;  // Frame-based movement
if (ballMovement.Length() < 1200.0f) return 0.0f;
```

**Why**: Frame-delta velocity is more reliable than instantaneous velocity. If ball stops for one frame, `ballVelNorm` might be misleading. Frame-delta captures actual movement distance.

### 2. **Elevation is Required**
**CornerClearReward**: No check if ball is actually cleared (elevated)

**BigClearReward**:
```cpp
if (state.ball.pos.z <= 200.0f && state.ball.vel.z <= 200.0f) return 0.0f;
```

**Why**: A "clear" should be a hard hit UP, not just to the corner. This prevents rewarding ground-level passes.

### 3. **Position Check is Faster**
**CornerClearReward**:
```cpp
float ballDistToGoal = (state.ball.pos - myGoal).Length();  // sqrt operation
if (ballDistToGoal > 3000.0f) return 0.0f;
```

**BigClearReward**:
```cpp
bool inDefensiveHalf = (player.team == Team::BLUE) ? (player.pos.y < 0) : (player.pos.y > 0);
```

**Why**: Simple comparison is O(1), distance calculation with sqrt is O(n). For defensive checks, "which side of field" is more intuitive than "how far from goal."

---

## What CornerClearReward Does Better

### 1. **Simpler Logic Flow**
CornerClearReward has only 3 gates vs BigClearReward's 6. It's more concise.

### 2. **No State Dependency**
CornerClearReward doesn't require `state.prev`, making it more robust to edge cases.

### 3. **Specific Strategy Targeting**
CornerClearReward specifically rewards corner clears (which are strategically sound in Rocket League). BigClearReward just rewards any hard, elevated, forward hit.

---

## Insights & Recommendations

### Why BigClearReward is Correctly Simplified

The simplification is actually a **design improvement**, not just a code reduction:

1. **More General**: BigClearReward rewards ANY hard, elevated, forward clear. CornerClearReward only rewards corner-specific clears. The generalized version should learn more diverse clearing strategies.

2. **Speed/Elevation Requirements**: These are good gates that prevent low-speed passes from getting rewarded as "clears."

3. **Frame-Delta Check**: More reliable than instantaneous velocity.

### Potential Further Simplification

Looking at CornerClearReward's simplicity, we could potentially reduce BigClearReward's complexity by:

#### Option A: Remove elevation check
```cpp
// Gate 6 removed - accept any hard, forward hit
return BASE_REWARD;
```
**Rationale**: If you're hitting hard + forward during defense, that's good regardless of height
**Risk**: Might reward ground-level passes

#### Option B: Combine gates 4-5 (speed + direction)
```cpp
// Gate 4-5 combined: Must be hard AND forward
float rawSpeed = ballMovement.Length();
float forwardComponent = ballMovement.Normalized().Dot(toOpponent);
if (rawSpeed < 1200.0f || forwardComponent < 0.2f) return 0.0f;
```
**Benefit**: Slightly cleaner
**Risk**: No readability benefit

#### Option C: Remove prev state requirement
```cpp
// Gate 2 removed - use current velocity instead
Vec ballVelNorm = state.ball.vel.Normalized();  // Current velocity
Vec opponentGoal = ...
if (ballVelNorm.Dot(toOpponent) < 0.2f) return 0.0f;
```
**Benefit**: Matches CornerClearReward simplicity, no state dependency
**Risk**: Less precise (depends on instantaneous velocity)

---

## Recommendation

**Current BigClearReward is GOOD as-is.** Here's why:

✅ **Maintains simplicity** (6 clear gates, no complexity)
✅ **More robust** (frame-delta velocity + elevation check)
✅ **More general** (not corner-specific)
✅ **Well-balanced** (harder to game, easier to understand)

The extra gates in BigClearReward vs CornerClearReward are **justified**:
- **Gate 2** (prev state): Required for accurate velocity calculation
- **Gate 6** (elevation): Ensures it's actually a "clear" not a pass

---

## Design Pattern Analysis

Both rewards follow a good pattern:

```
1. Ball touch gate
2. Position gate (defensive)
3. Movement quality gate (speed/direction)
[Optional gates for specificity]
4. Return reward
```

This is a solid reward design pattern:
- ✅ Early gates are quick (bail out if not touching ball, not in right position)
- ✅ Later gates are expensive (calculations only if early gates pass)
- ✅ Reward value determined by strategy-level requirements

---

## Conclusion

**BigClearReward is correctly simplified.** It's actually more sophisticated than CornerClearReward in meaningful ways (frame-delta velocity, elevation requirement). The 6 gates aren't bloat—they're strategic requirements.

If anything, CornerClearReward is *under-specified* (no elevation check), while BigClearReward is *well-specified* (6 strategic gates with good rationale for each).

