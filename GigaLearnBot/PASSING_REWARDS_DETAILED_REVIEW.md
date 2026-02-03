# PassingRewards.h - Detailed Review & Analysis
**Date:** 2025-02-02
**Status:** Experimental - Backpasses OK, Forward Passes Struggling

---

## Executive Summary

PassingRewards.h contains **4 reward classes** designed to incentivize different passing types:
1. **BackPassReward** - ✅ Working well (safe backward passes)
2. **ForwardPassReward** - ❌ Not triggering (missing signal)
3. **HighPassReward** - ❌ Not triggering (likely too strict)
4. **OffensivePlayReward** - ❌ Not triggering (positioning logic questionable)

**Root Causes Identified:**
- Missing `ballTouchedStep` filtering for receivers
- Overly strict teammate positioning requirements
- Ball movement detection logic doesn't account for player-initiated vs. passive motion
- Pass recognition window (0.1-2.0s) is arbitrary and misses many genuine passes
- No passive pass detection (when ball is touched near teammate)

---

## Detailed Analysis Per Reward

### 1. BackPassReward ✅ WORKING

**Status:** OK (not perfect, but functional)

**How It Works:**
```cpp
// Triggers when:
1. Current player touches ball (ballTouchedStep == true)
2. Ball moves toward own goal (backPassAlignment > 0.5f)
3. Teammate positioned behind player in defensive direction
4. Teammate within 2000 UU (reasonable pass distance)
```

**Why It Works:**
- Simple, deterministic criteria
- Back passes are **geometrically obvious** (just check dot product to own goal)
- Teammate positioning is **forgiving** (only needs alignment > 0.3f)
- 0-5 reward scales reasonably with distance

**Why It's "Not Super Clean Yet":**
- May not distinguish between **intentional passes** and just moving ball backward
- No check for ball velocity (could reward slow rolls)
- Teammate detection only requires `< 2000 UU` and basic positioning
- Could trigger on defensive clears that happen to move toward a teammate

**Suggested Improvement:**
```cpp
// Add velocity check
float ballSpeed = (state.ball.pos - state.prev->ball.pos).Length();
if (ballSpeed < 300.0f) return 0.0f;  // Too slow to count as real pass

// Add safety check - ball shouldn't be saved goal-bound
if (state.ball.pos.y < GOAL_BOUNDARY) return 0.0f;  // Disallow near-goal passes
```

---

### 2. ForwardPassReward ❌ NOT TRIGGERING

**Status:** Silent (not rewarding at all)

**How It Works:**
```cpp
// Should trigger when:
1. Current player touches ball (ballTouchedStep == true)
2. Ball moves forward toward opponent goal (forwardAlignment > 0.3f)
3. Ball travels > 500 UU (MIN_PASS_DISTANCE)
4. Teammate positioned ahead in forward direction (aheadAlignment > 0.4f)
5. Teammate within 2500 UU (TEAMMATE_POSITIONING_RANGE)
```

**The Problem:**
The critical issue is in the **receiver detection logic**:

```cpp
// WRONG: This logic never registers for "forward pass"
// It's checking if teammate is ahead of PASSER and in forward direction
Vec toTeammate = (other.pos - player.pos);
float aheadAlignment = toTeammate.Normalized().Dot(toGoal);

if (aheadAlignment > 0.4f && distanceToTeammate < TEAMMATE_POSITIONING_RANGE) {
    // This triggers...
}

// But when does this actually occur in real game?
// - Passer at X position touching ball
// - Teammate needs to be ahead of passer (aheadAlignment > 0.4f means 66°+ ahead)
// - Typical formation: Passer hits ball, teammate is BEHIND or SIDE (not 66° ahead)
```

**Why It's Failing:**
1. **Overly Strict Positioning** - Requires teammate to be 66°+ ahead. In real gameplay:
   - Player 1 touches ball at mid-field
   - Player 2 is recovering or supporting behind → No reward
   - Player 2 is at angle/side → No reward (needs > 0.4f dot product)

2. **Ball Movement Check is Weak** - `forwardAlignment > 0.3f` means ball only needs 73°+ toward goal
   - But in 3v3, center passes often go at oblique angles
   - Sideways passes don't count (0° alignment)

3. **Receiver Never Gets Reward** - The **receiver** isn't tracked as having received
   - Player 1 touches ball and launches it forward ✓
   - Player 2 receives it ✗ (function returns 0 for receiver, no award)

4. **Distance Factor Calculation is Weird:**
   ```cpp
   float distance_factor = 1.0f - RS_CLAMP((ballDistance - MIN_PASS_DISTANCE) /
                                           (MAX_PASS_DISTANCE - MIN_PASS_DISTANCE), 0.0f, 1.0f);
   // ballDistance = 600 UU
   // distance_factor = 1.0 - (600-500)/(3000-500) = 1.0 - 0.045 = 0.955 (95.5% reward)
   // ballDistance = 3000 UU
   // distance_factor = 1.0 - (3000-500)/(3000-500) = 0.0 (0% reward!)
   // BACKWARDS: Penalizes long passes!
   ```

---

### 3. HighPassReward ❌ NOT TRIGGERING

**Status:** Silent (never triggers)

**How It Works:**
```cpp
// Should trigger when:
1. Current player touches ball
2. Ball GAINS height (heightGain >= 50 UU after touch)
3. Ball is elevated (ball.pos.z >= 200 UU)
4. Ball moves toward goal (goalAlignment > 0.5f)
5. Teammate positioned to receive aerial (distance 200-1500 UU, below ball)
```

**The Problems:**

1. **Height Gain Check is Impossible to Satisfy:**
   ```cpp
   float heightGain = state.ball.pos.z - state.prev->ball.pos.z;
   if (heightGain < 50.0f) return 0.0f;
   ```
   - One frame (0.016s), ball physics moves slowly
   - Even powerful flicks gain only 20-50 UU height per frame
   - If ball is already at z=150, next frame might be z=170 (only 20 UU gain)
   - **50 UU gain in one frame is nearly impossible** on ground

2. **Requires 200 UU Min Height**
   - Ball must ALREADY be at 200+ UU AND gain 50 UU more
   - After gaining height, goes to 250+ UU
   - But lobs/passes often stay in 100-200 UU range

3. **Teammate Positioning Logic is Broken:**
   ```cpp
   Vec toTeammate = (other.pos - state.ball.pos);  // Vector FROM ball TO teammate
   float distanceToAerialBall = toTeammate.Length();

   // heightAlignment = RS_MAX(0.0f, other.pos.z - state.ball.pos.z)
   // If teammate is below ball, heightAlignment = negative (clamped to 0)
   // If teammate is above ball, heightAlignment = positive

   float heightAlignment = RS_MAX(0.0f, other.pos.z - state.ball.pos.z);
   ```
   - **This is inverted!** Teammate should be BELOW the ball to hit an aerial
   - Current code: `RS_MAX(0, teammate.z - ball.z)` = gives 0 if teammate < ball (wrong!)
   - Should be: `RS_MAX(0.0f, state.ball.pos.z - other.pos.z)` (ball height above teammate)

---

### 4. OffensivePlayReward ❌ NOT TRIGGERING

**Status:** Silent (never rewards)

**How It Works:**
```cpp
// Should trigger when:
1. Current player touches ball
2. Ball speed >= 500 UU/s
3. A teammate is positioned for striking (< 2000 UU from opponent goal)
4. Teammate has <= 1 opponents nearby (undefended)
```

**The Problems:**

1. **Striker Position Detection Counts Only Defenders in 500 UU Range:**
   ```cpp
   for (const auto& opp : state.players) {
       if (opp.team == player.team) continue;
       Vec toTeammate = (other.pos - opp.pos);
       if (toTeammate.Length() < 500.0f) blockingOpponents++;
   }
   ```
   - 500 UU is very close (tight marking)
   - In real 3v3, opponents often hover 800-1200 UU away
   - Most passes fail this check (thinks striker is defended when they aren't)

2. **Strike Position Threshold (2000 UU to goal) is Vague:**
   - 2000 UU ≈ 2.5 seconds of moderate speed
   - Could be behind the goal, at the goal, or in midfield depending on arena position
   - No check for whether teammate is actually FACING the goal

3. **Score Calculation is Inverted:**
   ```cpp
   float positionScore = 1.0f - RS_CLAMP(distToGoal / GOAL_PROXIMITY, 0.0f, 1.0f);
   // If distToGoal = 0 (teammate at goal): positionScore = 1.0 ✓
   // If distToGoal = 2000 (teammate far): positionScore = 0.0 ✓
   // This is actually correct!
   ```

4. **Never Rewards the Receiver:**
   - Passer gets reward for setting up striker
   - Striker who receives the pass gets **nothing**
   - Creates asymmetry where only the passer is incentivized

5. **No Pass Reception Logic:**
   - Function only executes in frame where **current player** touches ball
   - Doesn't track if this player is the **receiver** of a previous pass
   - A teammate receiving this "offensive pass" gets 0 reward

---

## PassingAccuracyReward (From MultiModalRewards.h)

**Status:** Complex & Questionable

**How It Works:**
```cpp
// Tracks:
mutable std::unordered_map<uint32_t, float> lastPassTime;
mutable std::unordered_map<uint32_t, uint32_t> passedFromPlayer;  // Who initiated

// When current player touches ball:
if (player.ballTouchedStep) {
    lastPassTime[player.carId] = elapsedTime[id];
    passedFromPlayer[player.carId] = player.carId;  // I just touched it
}

// If ANOTHER player (receiver) touches ball:
if (player.ballTouchedStep && passedFromPlayer[0] != player.carId) {
    // We're receiving! Check if previous passer exists
    uint32_t passerCarId = passedFromPlayer[0];
    float timeSincePass = elapsedTime[id] - lastPassTime[passerCarId];

    if (timeSincePass < 2.0f && timeSincePass > 0.1f) {
        reward += 3.0f;  // Pass received!
    }
}
```

**Issues:**
1. **`passedFromPlayer[0]` is Always the Passer - Not Tracked Per Car**
   - Uses hardcoded index `[0]` instead of `passedFromPlayer[receiver.carId]`
   - If players touch in wrong order, breaks tracking

2. **Time Window is Arbitrary (0.1-2.0s)**
   - 0.1s = Too tight (ball bounces between players in 0.2-0.5s)
   - 2.0s = Too loose (accounts for unrelated touches)

3. **Doesn't Check Ball Continuity**
   - Ball could bounce off walls between passer/receiver
   - No check that ball stayed in play/with team

4. **Receiver Bonus is Confusing:**
   ```cpp
   if (goalwardSpeed > 1500.0f) {
       reward += 1.0f;  // Pass led to immediate offensive play
   }
   ```
   - Rewards if ball is moving toward goal at 1500 UU/s
   - But doesn't check if RECEIVER is responsible for this speed
   - Could trigger from random physics

---

## Key Issues Summary

| Reward | Issue | Impact | Fix Difficulty |
|--------|-------|--------|-----------------|
| BackPassReward | Too permissive on pass intent | May reward accidental clears | 🟡 Medium |
| ForwardPassReward | Overly strict teammate positioning (>0.4f) | Never triggers in realistic gameplay | 🔴 Hard |
| ForwardPassReward | Distance factor inverted | Long passes penalized | 🟢 Easy |
| HighPassReward | Height gain check impossible (50 UU/frame) | Never triggers | 🔴 Hard |
| HighPassReward | Height alignment calculation inverted | Wrong teammate detection | 🟢 Easy |
| OffensivePlayReward | Defender range too strict (500 UU) | Thinks striker always defended | 🟡 Medium |
| OffensivePlayReward | No receiver reward tracking | Asymmetric incentive | 🔴 Hard |
| PassingAccuracyReward | Car ID tracking uses hardcoded [0] | Broken multi-player tracking | 🔴 Hard |
| PassingAccuracyReward | Time window arbitrary | Misses/counts false passes | 🟡 Medium |

---

## Recommended Fixes (Priority Order)

### 1. **Rewrite ForwardPassReward** 🔴 CRITICAL
**Problem:** Core passing reward never triggers

**Solution:** Simplify positioning requirements
```cpp
class ForwardPassReward : public Reward {
private:
    static constexpr float MIN_PASS_DISTANCE = 300.0f;    // Reduced
    static constexpr float MAX_PASS_DISTANCE = 2500.0f;
    static constexpr float FORWARD_THRESHOLD = 0.1f;      // Reduced (allow more angles)
    static constexpr float TEAMMATE_RANGE = 3500.0f;      // Increased (more forgiving)

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || !state.prev) return 0.0f;

        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
        Vec toGoal = (opponentGoal - player.pos).Normalized();

        Vec ballMovement = (state.ball.pos - state.prev->ball.pos);
        float ballDistance = ballMovement.Length();

        // Require SOME forward movement, not strict
        Vec ballDir = (ballDistance > 0.1f) ? ballMovement.Normalized() : toGoal;
        float forwardAlignment = ballDir.Dot(toGoal);

        // Much more lenient: -0.1f allows even backward-to-sides
        if (forwardAlignment < -0.1f) return 0.0f;
        if (ballDistance < MIN_PASS_DISTANCE) return 0.0f;

        // Check for ANY teammate in good position (not strict alignment)
        int goodTeammates = 0;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;

            float distToTeammate = (other.pos - player.pos).Length();
            if (distToTeammate < TEAMMATE_RANGE && distToTeammate > 200.0f) {
                goodTeammates++;
            }
        }

        if (goodTeammates == 0) return 0.0f;

        // Reward scales with distance (long passes worth more)
        float distance_factor = RS_CLAMP((ballDistance - MIN_PASS_DISTANCE) /
                                        (MAX_PASS_DISTANCE - MIN_PASS_DISTANCE), 0.0f, 1.0f);
        float baseReward = 3.0f + (distance_factor * 5.0f);  // 3-8 depending on distance

        // Bonus if multiple receivers available
        return baseReward * (1.0f + goodTeammates * 0.2f);
    }
};
```

### 2. **Fix HighPassReward Height Logic** 🟢 EASY
**Problem:** Height gain check impossible, alignment inverted

**Solution:**
```cpp
// OLD (BROKEN):
float heightGain = state.ball.pos.z - state.prev->ball.pos.z;
if (heightGain < 50.0f) return 0.0f;

// NEW (FIXED):
// Check if ball is being elevated (continuous height), not 1-frame gain
float currentHeight = state.ball.pos.z;
if (currentHeight < HIGH_BALL_THRESHOLD) return 0.0f;  // Just check final height

// OLD (BROKEN):
float heightAlignment = RS_MAX(0.0f, other.pos.z - state.ball.pos.z);

// NEW (FIXED):
// Teammate should be BELOW ball to hit aerial
float heightAlignment = RS_MAX(0.0f, state.ball.pos.z - other.pos.z);
```

### 3. **Add Pass Reception Tracking** 🔴 HARD
**Problem:** Receivers don't get rewarded for receiving

**Solution:** Create a separate receiver reward
```cpp
class PassReceptionReward : public Reward {
private:
    mutable std::unordered_map<uint32_t, uint64_t> lastTeammatePassTick;

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        // Did a teammate touch ball recently (0.5-2s ago)?
        bool recentTeammateTouch = false;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team) continue;
            if (other.ballTouchedStep) {
                uint64_t timeSincePrevTouch = state.lastTickCount - state.lastTickCount;  // Would need tracking
                if (timeSincePrevTouch < 120 && timeSincePrevTouch > 6) {  // ~0.5-2s at 60 ticks/sec
                    recentTeammateTouch = true;
                    break;
                }
            }
        }

        if (recentTeammateTouch) {
            return 2.0f;  // Receiving a pass
        }
        return 0.0f;
    }
};
```

### 4. **Simplify BackPassReward** 🟡 MEDIUM
**Problem:** May reward accidental backward clears

**Solution:** Add velocity gate
```cpp
// Add after backPassAlignment check:
float ballSpeed = (state.ball.pos - state.prev->ball.pos).Length();
if (ballSpeed < 200.0f) return 0.0f;  // Must be an actual pass, not a roll
if (ballSpeed > 3000.0f) return 0.0f;  // Not a chaotic scramble
```

### 5. **Fix OffensivePlayReward Defender Detection** 🟡 MEDIUM
**Problem:** 500 UU defender range too strict

**Solution:**
```cpp
// OLD:
if (toTeammate.Length() < 500.0f) blockingOpponents++;

// NEW (more generous):
if (toTeammate.Length() < 1000.0f) blockingOpponents++;  // Increased to 1000 UU
```

---

## Integration Recommendations

### Current Config (PLATINUM Phase):
```cpp
WeightedReward(new ZeroSumReward(new BackPassReward(), 0.5f), 5.0f),
WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 10.0f),
WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 10.0f),
WeightedReward(
    new ZeroSumReward(
        new PassingRewardWrapper(
            new PassingAccuracyReward(),
            optionControl
        ),
        1.0f
    ),
    20.0f
),
```

### Suggested Replacement (After Fixes):
```cpp
// Backpasses (safe, working)
WeightedReward(new ZeroSumReward(new BackPassReward(), 0.5f), 4.0f),  // Slight nerf

// Forward passes (REWRITTEN)
WeightedReward(new ZeroSumReward(new ForwardPassReward(), 0.3f), 15.0f),  // New simplified version

// High passes (lobs, aerials) - ENABLE after fix
// WeightedReward(new ZeroSumReward(new HighPassReward(), 0.3f), 8.0f),  // Currently disabled

// Reception tracking (NEW)
// WeightedReward(new ZeroSumReward(new PassReceptionReward(), 1.0f), 5.0f),  // When ready

// Keep existing passing infrastructure
WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 10.0f),
WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 10.0f),
```

---

## Conclusion

**BackPassReward** is working because it has simple, clear criteria that match real game situations.

**All forward passes are silent** because they have logical errors:
- Unrealistic positioning requirements
- Impossible physics thresholds
- Missing receiver tracking
- Inverted distance scaling

The system needs a **fundamental rethink** around:
1. Simplifying position requirements (most passes won't have perfect receiver positioning)
2. Tracking passes longitudinally (who passes, who receives, within realistic time)
3. Giving rewards to BOTH passer and receiver
4. Using position optionality as a **gate** rather than strict requirement

The new **OptionControlReward gating** actually addresses this problem better than the current PassingRewards - rewards high-option positions where passing is viable, then let the other rewards do the work!
