# PLATINUM Phase Configuration - Complete Summary
**Date:** 2025-02-02
**Status:** ✅ Ready for Training

---

## Overview

PLATINUM Phase (Phase 4) has been upgraded with **dual-layer reward gating**:
1. **Option Control Gating** - Position-based multipliers (0.2x-1.5x)
2. **ZeroSum Competition** - Relative competitive balance

All scoring and passing rewards now use this system to encourage **strategic, team-oriented play**.

---

## Key Rewards Configuration

### Scoring Rewards (Option Control + ZeroSum)

| Reward | Weight | ZeroSum | Gate Range | Purpose |
|--------|--------|---------|-----------|---------|
| AdvancedFlickReward | 6.0f | 0.3f | 0.2x-1.5x | High-skill offense (flicks, spins) |
| DribbleMasteryReward | 1.5f | 0.3f | 0.2x-1.5x | Ball control on ground |
| CutOutplayReward | 30.0f | 0.3f | 0.2x-1.5x | Defensive repositioning |
| FakeChallengeOffenseReward | 1.0f | 0.3f | 0.2x-1.5x | Unpredictable movement |

**Scoring Threshold (0.3f):**
- High competition element
- Opponent performance significantly reduces relative score
- Models direct competitive pressure (you vs them)

---

### Passing Rewards (Option Control + ZeroSum)

| Reward | Weight | ZeroSum | Gate Range | Purpose |
|--------|--------|---------|-----------|---------|
| PassingAccuracyReward | 20.0f | 1.0f | 0.2x-1.2x | Accurate pass execution |
| BackPassReward | 5.0f | 0.5f | — | Safe backward passes |
| PassingSetupReward | 10.0f | 1.0f | — | Pass setup positioning |
| ReceivingPassReward | 10.0f | 1.0f | — | Receiving and controlling |

**Passing Threshold (1.0f):**
- Lower competition element than scoring
- Opponent performance has moderate effect
- Models team-based play (coordination over direct competition)
- BackPassReward: 0.5f (mid-ground, defensive)

---

## How the Dual-Layer System Works

### Layer 1: Option Control Gating
**Question:** "How many viable plays are available right now?"

**Scoring Options (0-6):**
1. Dribble - ball on ground, player nearby
2. Flick - ball at flick height (200-300 UU)
3. Air Dribble - ball airborne, player airborne
4. Bump/Demo - opponent within 500 UU
5. Powershot - toward goal within ±45°
6. Pass - teammate within 3000 UU

**Gating Multiplier:**
- 0 options → 0.2x (desperate, risky)
- 1-2 options → 0.6x (limited)
- 2-4 options → 1.0x (balanced)
- 4+ options → 1.5x (bonus for multiple plays)

**Example:** Flick in high-option position = 1.5x multiplier
```
25.0 (base flick) × 1.5 (option bonus) = 37.5 reward
```

### Layer 2: ZeroSum Competition
**Question:** "How am I doing relative to opponents?"

**Formula:** `playerScore - (threshold × opponentAverage)`

**Example with 0.3f threshold:**
```
My flick reward: 37.5
Opponent average: 8.0
ZeroSum adjustment: 37.5 - (0.3f × 8.0) = 37.5 - 2.4 = 35.1

Higher threshold (worse for me):
ZeroSum with 0.5f: 37.5 - (0.5f × 8.0) = 33.5
```

### Combined Effect

**Scenario: Flick in high-option vs low-option position**

**High-Option (4+ scoring options):**
1. Base flick reward: 25.0
2. Option gate: 1.5x (4+ options)
3. After gate: 37.5
4. ZeroSum (0.3f, opp avg 8.0): 35.1
5. **Final incentive: 35.1** ✅ Strong

**Low-Option (0 scoring options):**
1. Base flick reward: 25.0
2. Option gate: 0.2x (0 options)
3. After gate: 5.0
4. ZeroSum (0.3f, opp avg 8.0): 2.6
5. **Final incentive: 2.6** ⚠️ Weak

**Ratio:** 35.1 / 2.6 = **13.5x difference**
- High-option flick is worth 13.5x more than low-option flick
- Strongly encourages: "Flick when you have play options, not desperately"

---

## Option Control Instance

**Configuration:**
```cpp
OptionControlReward* optionControl = new OptionControlReward();
```

**Shared Across:**
- All ScoringRewardWrapper instances
- All PassingRewardWrapper instances
- Standalone reward (0.5f weight, primarily for gating signal)

**Benefit:** Single consistent option counting across all rewards in phase

---

## Team-Based vs Competitive Balance

### Scoring Rewards (More Competitive)
- ZeroSum 0.3f = Opponent performance matters heavily
- Best for: Individual skill development (flicks, control)
- Philosophy: "Prove you're better than opponent"

### Passing Rewards (More Team-Oriented)
- ZeroSum 1.0f = Moderate opponent effect
- Best for: Coordination and teamplay
- Philosophy: "Help teammates succeed"

### Backup Passing Rewards (More Defensive)
- No option gating, lower ZeroSum
- Best for: Safe, tactical play
- Philosophy: "Maintain possession, support team"

---

## Expected Training Behavior

### Early Phase (<100M steps)
- Bot learns to recognize high-option positions
- Becomes less likely to randomly attempt risky plays
- Passes increase as high-option positions encourage teamplay
- Flicks cluster around high-option moments

### Mid Phase (100-500M steps)
- Bot develops habit of seeking good positions first
- Passing chains become more frequent
- Defensive repositioning (CutOutplay) increases
- Better overall decision-making

### Late Phase (500M+ steps)
- Strategic positioning becomes automatic
- Passes chain into scoring opportunities
- Competitive ZeroSum drives skill ceiling
- More sophisticated team rotations

---

## Monitoring Metrics

### Key Stats to Watch:
1. **Pass Frequency** - Should increase with option control
2. **Flick Success Rate** - Should correlate with option count
3. **Average Option Count** - Should stabilize around 2-3
4. **Win Rate** - Should maintain or improve
5. **Reward Distribution** - Gating should smooth spikes

### Red Flags:
- ⚠️ **Bot stuck in safe positions** → Option gating too strict
- ⚠️ **No passes ever** → PassingAccuracyReward not triggering
- ⚠️ **Reward spikes** → ZeroSum threshold too low
- ⚠️ **Random behavior** → Option control weight too low

---

## Configuration Parameters (Tunable)

### OptionControlReward Parameters
**File:** `OptionControlReward.h`

```cpp
// Scoring ranges
DRIBBLE_RANGE = 1500.0f         // How close to ball for dribble option
FLICK_RANGE = 2000.0f           // Range for flick option
AIR_DRIBBLE_RANGE = 2000.0f     // Range for air dribble
BUMP_RANGE = 500.0f             // Opponent proximity for bump
PASS_RANGE = 3000.0f            // Teammate distance for pass option

// Multiplier curves
MULTIPLIER_MIN = 0.2f           // Minimum gate (no options)
MULTIPLIER_MAX_SCORING = 1.5f   // Maximum gate (4+ scoring options)
MULTIPLIER_MAX_PASSING = 1.2f   // Maximum gate (good pass options)
```

### ZeroSum Thresholds (Tunable)
**File:** `RewardConfig.cpp` lines 196-228

```cpp
// Scoring (high competition)
new ZeroSumReward(..., 0.3f)

// Passing (moderate competition)
new ZeroSumReward(..., 1.0f)
```

**Adjusting Strategy:**
- **Lower threshold** (0.1f-0.2f) = Stronger relative competition
- **Higher threshold** (0.5f+) = Weaker relative competition
- **Test:** Run training 50M steps at each threshold level

---

## PassingRewards.h Status

**Current Issues (See PASSING_REWARDS_DETAILED_REVIEW.md):**
- ❌ ForwardPassReward - Overly strict positioning (0.4f alignment)
- ❌ HighPassReward - Impossible height gain check (50 UU/frame)
- ❌ OffensivePlayReward - Defender range too strict (500 UU)
- ❌ PassingAccuracyReward - Hardcoded car ID tracking issues
- ✅ BackPassReward - Working well

**Workaround:**
- PassingAccuracyReward now wrapped with option gating + ZeroSum
- Acts as backup passing reward while PassingRewards.h is refined
- Not perfect, but provides some passing incentive

**Future Work:**
- Rewrite ForwardPassReward with simplified positioning
- Fix HighPassReward height logic
- Improve PassingAccuracyReward tracking

---

## Weights Breakdown (PLATINUM Phase)

### High-Weight Rewards (Foundations)
```cpp
Touch Control:  TouchAccelReward (35.0f)
Recovery:       WavedashBasicReward (24.0f)
Boost:          PickupBoostReward (28.0f)
Defense:        OpponentPassInterceptionReward (3.0f) (with ZeroSum)
Wall Dash:      WallDashReward (25.0f)
Passing:        PassingAccuracyReward (20.0f) (with ZeroSum + option gating)
Outplay:        CutOutplayReward (30.0f) (with ZeroSum + option gating)
Shots:          Composite Shot Reward (35.0f)
Assist:         AssistReward (35.0f)
```

### Medium-Weight Rewards (Specialization)
```cpp
Recovery:       EnhancedSpeedFlipReward (8.0f)
Passing Setup:  PassingSetupReward (10.0f)
Reception:      ReceivingPassReward (10.0f)
Backpass:       BackPassReward (5.0f)
Flick:          AdvancedFlickReward (6.0f) (with ZeroSum + option gating)
Defensive Pos:  DefensivePositionReward (2.0f)
Defense Adv:    EarlyAerialReward (5.0f)
Rotation:       ProgressiveRotationReward (15.0f)
```

### Low-Weight Rewards (Polish)
```cpp
Dribble:        DribbleMasteryReward (1.5f) (with ZeroSum + option gating)
Fake Challenge: FakeChallengeOffenseReward (1.0f) (with ZeroSum + option gating)
Ball Catch:     CatchBallOnCarReward (0.5f)
Option Control: OptionControlReward (0.5f) (standalone)
```

---

## Compilation & Deployment

### Status: ✅ Ready

**Modified Files:**
- `src/RewardConfig.cpp` - Added ZeroSum wrappers, option gating

**No Changes Needed:**
- `Rewards/OptionControlReward.h` - Already integrated
- `Rewards/OptionGatingWrappers.h` - Already integrated
- `Rewards/CompleteMechanicsRewards.h` - All scoring rewards present
- `Rewards/PassingRewards.h` - Backup passing (BackPass, accuracy)

**Build Command:**
```bash
cd C:\Projects\RLGymBot_v3\GigaLearnBot
cmake -B build -S . -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release -j 8
```

---

## Next Optimization Targets

1. **PassingRewards.h Fixes** (Priority 1)
   - Rewrite ForwardPassReward positioning logic
   - Fix HighPassReward height checks
   - Improve accuracy tracking

2. **Option Control Tuning** (Priority 2)
   - Observe if positions have 2-3 average options
   - Adjust range constants if needed
   - Consider temporal persistence (options need N frames)

3. **ZeroSum Threshold Optimization** (Priority 3)
   - Test 0.2f, 0.3f, 0.5f for scoring
   - Test 0.7f, 1.0f, 1.3f for passing
   - Monitor for reward divergence/collapse

4. **Advanced Passing** (Priority 4)
   - Reception reward tracking
   - Chain passing sequences
   - Offensive setup sequences

---

## Key Insight: Why This Design Works

**Problem:** Rewards are usually isolated (flick doesn't know about context)

**Solution:** Layer context onto rewards:
1. **Option Control** → Asks "do I have other choices?"
2. **ZeroSum** → Asks "how do opponents compare?"
3. **Combined** → "How good am I, relative to alternatives and opposition?"

**Result:** Better strategic decision-making through informed incentives

---

## References

- **OptionControlReward.h** - Strategic optionality reward system
- **OptionGatingWrappers.h** - Wrapper classes for gating
- **PASSING_REWARDS_DETAILED_REVIEW.md** - PassingRewards.h analysis
- **ZEROSUM_GATING_INTEGRATION.md** - Technical gating documentation
- **RewardConfig.cpp** - Phase 4 configuration (lines 179-340)

---

**Commit:** `6ea9f79` - PLATINUM Phase - Option Control Gating + ZeroSum Wrapping
**Ready For:** Training in PLATINUM phase
**Estimated Progress:** 3-5B steps to tune and stabilize
