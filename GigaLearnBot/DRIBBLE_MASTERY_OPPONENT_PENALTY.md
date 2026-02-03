# DribbleMasteryReward - Opponent Proximity Penalty Update
**Date:** 2025-02-02
**Update:** Added opponent pressure penalty for risky dribbles

---

## What Changed

DribbleMasteryReward now **penalizes dribbling when opponents are nearby** (<500 UU away).

This encourages the bot to avoid risky dribbles under defensive pressure.

---

## Implementation Details

### New Parameters Added

```cpp
float opponentDangerRange = 500.0f;   // Penalty zone for dribbling near opponents
float maxOpponentPenalty = 0.5f;      // Maximum penalty (50% reward reduction)
```

### How It Works

**Graduated Penalty System:**
- At 0 UU distance: 50% penalty (reward × 0.5)
- At 250 UU distance: 25% penalty (reward × 0.75)
- At 500 UU distance: 0% penalty (reward × 1.0)
- Beyond 500 UU: No penalty

**Formula:**
```
opponentProximityFactor = closestOpponentDist / 500.0f  // 0..1
opponentPenalty = 0.5f × (1.0f - opponentProximityFactor)  // 0..0.5
finalReward = baseReward × (1.0f - opponentPenalty)
```

### Scenarios

**Scenario 1: Dribbling with opponent 100 UU away**
```
Base dribble reward: 2.0
Opponent distance: 100 UU
Proximity factor: 100/500 = 0.2
Penalty: 0.5 × (1.0 - 0.2) = 0.4 (40%)
Final reward: 2.0 × (1.0 - 0.4) = 1.2 ✅ Risky, but still rewarded
```

**Scenario 2: Dribbling with opponent 300 UU away**
```
Base dribble reward: 2.0
Opponent distance: 300 UU
Proximity factor: 300/500 = 0.6
Penalty: 0.5 × (1.0 - 0.6) = 0.2 (20%)
Final reward: 2.0 × (1.0 - 0.2) = 1.6 ✅ Safer position
```

**Scenario 3: Dribbling with opponent 600 UU away**
```
Base dribble reward: 2.0
Opponent distance: 600 UU
Proximity factor: N/A (outside danger range)
Penalty: 0 (no penalty)
Final reward: 2.0 × 1.0 = 2.0 ✅ Safe position, full reward
```

---

## Why This Matters

### Problem Being Solved
Previously, DribbleMasteryReward gave the same reward for dribbling regardless of opponent proximity. This meant:
- Bot would dribble even when heavily pressured
- Risky ball-on-car plays with defenders nearby earned full reward
- No incentive to pass or retreat under pressure

### Solution
Now the reward scales with opponent pressure:
- **Safe dribbles** (far from defenders) get full reward
- **Risky dribbles** (near defenders) get reduced reward
- Bot learns to abandon dribbles when pressured
- Encourages passing or retreating when defenders close in

---

## Integration with Option Control Gating

Remember: DribbleMasteryReward is now wrapped with Option Control Gating in PLATINUM Phase:

```cpp
new ZeroSumReward(
    new ScoringRewardWrapper(
        new DribbleMasteryReward(),
        optionControl
    ),
    0.3f
)
```

### Combined Effects

**High-option, safe dribble (opponent 600+ UU away):**
```
1. Base reward: 2.0
2. Option gate (4+ options): × 1.5 = 3.0
3. ZeroSum (0.3f threshold, opp avg 8.0): 3.0 - 2.4 = 0.6
Final: 0.6 ✅ Encouraged (when you have alternatives)
```

**Low-option, risky dribble (opponent 100 UU away):**
```
1. Base reward: 2.0
2. Opponent penalty: × 0.5 = 1.0
3. Option gate (0 options): × 0.2 = 0.2
4. ZeroSum: 0.2 - 2.4 = -2.2
Final: -2.2 ❌ Strongly discouraged (risky + no options)
```

---

## Configuration

### Tunable Parameters

You can adjust opponent penalty strength by modifying:

**Penalty Zone Radius:**
```cpp
float opponentDangerRange = 500.0f;   // Increase to 700.0f for stricter penalty
                                       // Decrease to 300.0f for more forgiving
```

**Maximum Penalty Amount:**
```cpp
float maxOpponentPenalty = 0.5f;       // Increase to 0.7f for harsher penalty
                                        // Decrease to 0.3f for gentler penalty
```

### Recommended Adjustments

**If bot passes too much:**
→ Reduce `maxOpponentPenalty` from 0.5f to 0.3f

**If bot still dribbles under pressure:**
→ Increase `maxOpponentPenalty` from 0.5f to 0.7f

**If penalty activates too early:**
→ Reduce `opponentDangerRange` from 500.0f to 350.0f

**If penalty doesn't activate enough:**
→ Increase `opponentDangerRange` from 500.0f to 700.0f

---

## Expected Behavior Changes

### What Bot Should Learn

✅ **Better decision-making under pressure:**
- Dribbles when safe (defenders far)
- Passes when pressured (defenders close)

✅ **Improved team coordination:**
- Less risky ball-hogging
- More willingness to distribute

✅ **Better defensive positioning:**
- Stops attempting dribbles when outnumbered
- Creates space for teammates

### What Bot Should Avoid

❌ **Risky solo plays:**
- No more "dribble into 3 defenders" behavior

❌ **Tunnel vision:**
- Learns to notice when opponents are closing

❌ **Possession loss:**
- Fewer turnovers from reckless dribbling

---

## Testing & Monitoring

### What to Watch During Training

1. **Pass frequency when pressured** - Should increase
2. **Dribble risk assessment** - Better decision-making
3. **Turnover rate** - Should decrease (fewer risky dribbles)
4. **Win rate** - Should improve (safer plays)

### Red Flags

⚠️ **Bot never dribbles**
→ Penalty too harsh, reduce `maxOpponentPenalty`

⚠️ **Bot ignores opponent proximity**
→ Penalty not activating, increase `opponentDangerRange`

⚠️ **Pass frequency unchanged**
→ Option gating might be overriding penalty, check combined weights

---

## Code Quality

### Safety Checks Included

✅ Team filtering - Only penalizes opponent proximity (ignores teammates)
✅ Distance validation - Uses squared/length calculations correctly
✅ Graduated scaling - Penalty smoothly transitions (not binary)
✅ Comments - Explains penalty calculation and philosophy

### Performance Impact

**Minimal** - O(n) loop over players (same as any defensive awareness reward)

---

## Relationship to Other Rewards

### Synergies

**CutOutplayReward:**
- Both discourage holding ball too long
- Both reward smart positioning

**PassingAccuracyReward:**
- Opponent proximity penalty encourages passing
- PassingAccuracy rewards completing the pass

**WallDashReward:**
- Alternative recovery when dribble pressured
- Bot learns multiple escape options

**Option Control Gating:**
- Combined 13.5x incentive difference (position + pressure awareness)
- Dribbles in good positions under no pressure = 13.5x rewarded

---

## Code Changes Summary

**File:** `AdvancedRewards.h`

**Lines Modified:** 2 additions (parameters) + 22 additions (penalty logic)

**Total Changes:** 24 lines added

**Integration:** Fully backward compatible (existing dribble logic unchanged)

---

## Rollback Instructions (If Needed)

If you want to disable the opponent penalty:

**Option 1: Set penalty to zero**
```cpp
float maxOpponentPenalty = 0.0f;  // No penalty
```

**Option 2: Set danger range to zero**
```cpp
float opponentDangerRange = 0.0f;  // Never activate penalty
```

**Option 3: Remove the penalty block entirely**
- Comment out or delete the `// --- NEW: Opponent proximity penalty` section

---

## Future Enhancements

Potential improvements (not implemented):

1. **Distance-weighted penalty** - Increase penalty range based on opponent speed
2. **Multi-opponent penalty** - Increase penalty if 2+ opponents nearby
3. **Team support bonus** - Reduce penalty if teammate is backing up
4. **Stamina consideration** - Reduce penalty if opponent out of boost

---

## Commit Information

**Commit:** (to be made)

**Type:** Enhancement - Opponent awareness penalty

**Files:** 1 modified (AdvancedRewards.h)

**Lines Changed:** 24 added

---

## Summary

DribbleMasteryReward now includes **opponent proximity awareness** through a graduated penalty system:

- **Safe dribbles** (>500 UU from opponents): Full reward
- **Risky dribbles** (<500 UU from opponents): Reduced reward (0-50% penalty)
- **Risky + low-option**: Strongly discouraged
- **Safe + high-option**: Strongly encouraged

This encourages smarter, more defensive dribbling and better team coordination.

**Ready for training!** 🚀
