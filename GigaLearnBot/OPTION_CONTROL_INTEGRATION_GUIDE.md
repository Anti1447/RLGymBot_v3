# Option Control Reward - Integration Guide

## ✅ Integration Complete: Phase 1 & 2 Done

Your Option Control Reward system is now fully integrated into **RewardConfig.cpp** in the **PHASE_4_PLATINUM** rewards!

---

## What Was Integrated

### 1. **Standalone Reward** (Low Weight)
```cpp
WeightedReward(optionControl, 0.5f)
```
- Encourages bot to seek positions with many viable plays
- Very low weight (0.5f) - primarily used for gating other rewards
- Safe to increase weight if you want stronger optionality pressure

### 2. **Example Wrapper #1: Flick Reward Gating**
```cpp
WeightedReward(
    new ScoringRewardWrapper(
        new AdvancedFlickReward(),
        optionControl
    ),
    6.0f  // Same weight as original
)
```
**What This Does:**
- Flick gets 0.2x multiplier when 0 scoring options available
- Flick gets 1.0x multiplier when 2-4 options available
- Flick gets 1.5x multiplier when 4+ options available (bonus)
- Encourages flicking from positions with multiple follow-up plays

### 3. **Example Wrapper #2: Passing Accuracy Gating**
```cpp
WeightedReward(
    new PassingRewardWrapper(
        new PassingAccuracyReward(),
        optionControl
    ),
    20.0f  // Same weight as original
)
```
**What This Does:**
- Pass accuracy gets 0.2x multiplier when < 0.5 pass options
- Pass accuracy gets 0.7x multiplier when 0.5-1.5 pass options
- Pass accuracy gets 1.2x multiplier when 1.5+ options (bonus)
- Encourages passing from positions with multiple receiving teammates

---

## How The Gating Mechanism Works

### Scoring Gate Multiplier Curve
```
0 scoring options → 0.2x (minimal reward for actions)
1 option         → 0.6x (some reward)
2-3 options      → 1.0x (baseline reward)
4+ options       → 1.5x (bonus applied!)
```

### Passing Gate Multiplier Curve
```
< 0.5 pass options  → 0.2x (minimal reward)
0.5-1.5 options     → 0.7x (some reward)
1.5+ options        → 1.2x (bonus applied!)
```

### Bonus-Based Design
- Base reward is **always available** (no action gets completely punished)
- Bonus multiplier kicks in when options are plentiful
- Gentle encouragement (0.2x to 1.5x) - not overly aggressive

---

## Current Integration Status

### ✅ DONE
- [x] Created `OptionControlReward.h` core implementation
- [x] Created `OptionGatingWrappers.h` wrapper classes
- [x] Added includes to `RewardConfig.h`
- [x] Added includes to `RewardConfig.cpp`
- [x] Integrated standalone reward (weight: 0.5f)
- [x] Integrated example wrapper for AdvancedFlickReward
- [x] Integrated example wrapper for PassingAccuracyReward
- [x] **BUILD PASSES** ✅

### Ready for Next Steps
- [ ] Train and observe bot behavior
- [ ] Monitor if bot seeks high-optionality positions
- [ ] Adjust weights if needed
- [ ] Add more wrapped rewards as desired

---

## How to Add More Gated Rewards

### Pattern 1: Gating a Scoring Reward
```cpp
// Wrap any scoring reward like this:
WeightedReward(
    new ScoringRewardWrapper(
        new YourScoringReward(),
        optionControl
    ),
    WEIGHT
),

// Then comment out or remove the original unwrapped version:
// WeightedReward(new ZeroSumReward(new YourScoringReward(), 0.3f), WEIGHT),
```

**Good candidates for scoring gate:**
- `AdvancedFlickReward` ✅ (already wrapped)
- `AirDribbleReward` (rewards aerial ball control)
- `PowerShotReward` (rewards powerful strikes)
- `BumpReward` (rewards aggressive contact)
- `DemoReward` (rewards demolishing opponents)

### Pattern 2: Gating a Passing Reward
```cpp
// Wrap any passing reward like this:
WeightedReward(
    new PassingRewardWrapper(
        new YourPassingReward(),
        optionControl
    ),
    WEIGHT
),

// Then comment out or remove the original unwrapped version:
// WeightedReward(new ZeroSumReward(new YourPassingReward(), 1.0f), WEIGHT),
```

**Good candidates for passing gate:**
- `PassingAccuracyReward` ✅ (already wrapped)
- `PassingSetupReward` (rewards setting up passes)
- `ReceivingPassReward` (rewards receiving passes)
- `BackPassReward` (rewards defensive passes)
- `AssistReward` (rewards assists)

### Pattern 3: Generic Gating
```cpp
// For rewards that don't fit scoring/passing perfectly:
WeightedReward(
    new OptionGatingWrapper(
        new YourReward(),
        OptionGateType::COMBINED,  // Use both scoring and passing options
        optionControl
    ),
    WEIGHT
),
```

---

## Debugging & Monitoring

### Check Option Counts
```cpp
// Inside any reward function:
std::string debugStr = optionControl->GetAvailableOptionsString(player, state);
// Outputs: "Scoring: 3 | Passing: 1.5"
```

### Monitor Option Availability
The implementation tracks:
- **Scoring options**: dribble, flick, air dribble, bump/demo, powershot, pass
- **Passing options**: base count + bonus for well-positioned teammates

### Key Parameters to Tune
If option detection seems off:
```cpp
// In OptionControlReward.h - adjust these constexpr values:
DRIBBLE_RANGE = 1500.0f;          // How far away can ball be for dribble?
FLICK_RANGE = 2000.0f;            // How far for flick?
AIR_DRIBBLE_RANGE = 2000.0f;      // How far for air dribble?
BUMP_RANGE = 500.0f;              // How close opponent must be for bump
PASS_RANGE = 3000.0f;             // How far teammate must be for pass
POWERSHOT_ANGLE_RANGE = 45.0f;    // Max angle deviation from goal
```

---

## Expected Bot Behavior Changes

### With Gating Active (Current)
- Bot learns to position itself in high-optionality zones
- Flicking/passing more rewarding when many options exist
- Should reduce "tunnel vision" behavior (focusing only on immediate action)
- Should improve tactical positioning and setup plays

### Training Observation Points
1. **Position Seeking**: Does bot naturally move to high-optionality areas?
2. **Action Diversity**: Does it vary plays more (vs repeated same action)?
3. **Setup Play**: Does it create plays before executing (positioning first)?
4. **Rotation**: Does it maintain multiple viable options for teammates?

---

## Configuration Options

### Adjust Standalone Weight
```cpp
// Current: 0.5f (very low)
// To encourage more optionality:
WeightedReward(optionControl, 2.0f)   // Stronger signal

// To disable standalone reward:
// WeightedReward(optionControl, 0.0f)
```

### Adjust Gating Aggressiveness
The multiplier curves are hardcoded in `OptionControlReward::GetScoringMultiplier()` and `GetPassingMultiplier()`. To change aggressiveness:

**Current (Moderate - 0.2x to 1.5x):**
```cpp
if (scoringOptions >= 4) return 1.5f;  // Bonus
```

**More Aggressive (Stronger bonus):**
```cpp
if (scoringOptions >= 4) return 2.0f;  // Larger bonus
```

**Gentler (Smaller range):**
```cpp
if (scoringOptions >= 4) return 1.2f;  // Smaller bonus
```

---

## Integration Architecture Diagram

```
RewardConfig.cpp (PLATINUM Phase)
├── Standalone: OptionControlReward (weight: 0.5f)
│   └── Counts scoring/passing options every frame
│
├── Scoring Wrapper (Example: AdvancedFlickReward)
│   ├── Base Reward: AdvancedFlickReward
│   ├── Queries: optionControl->CountScoringOptions()
│   └── Multiplies: baseReward * GetScoringMultiplier(options)
│
└── Passing Wrapper (Example: PassingAccuracyReward)
    ├── Base Reward: PassingAccuracyReward
    ├── Queries: optionControl->CountPassOptions()
    └── Multiplies: baseReward * GetPassingMultiplier(options)
```

---

## Quick Reference: All Option Types

### Scoring Options Detected
| Type | Condition | Range |
|------|-----------|-------|
| **Dribble** | Ball z < 300 | < 1500 UU |
| **Flick** | Ball z 200-300 | < 2000 UU |
| **Air Dribble** | Ball z > 150, player airborne | < 2000 UU |
| **Bump/Demo** | Opponent nearby | < 500 UU |
| **Powershot** | Ball kickable, toward goal ±45° | Kickable |
| **Pass** | Teammate in range | < 3000 UU |

### Passing Options Detected
| Type | Points | Bonus |
|------|--------|-------|
| **Base Teammate** | +1.0 | Nearby in range |
| **Well-Positioned** | +0.5 | Ahead + offensive side |

---

## Next Steps

1. **Train the model** with current configuration
2. **Observe behavior** - Does bot seek optionality?
3. **Monitor metrics**:
   - Average option count over game
   - Position distribution (does it stay central?)
   - Action diversity (flick/pass/dribble distribution)
4. **Iterate**:
   - Adjust weights if gating too weak/strong
   - Add more wrapped rewards
   - Refine thresholds based on play patterns

---

## Files Modified

| File | Changes |
|------|---------|
| `RewardConfig.h` | Added includes for Option Control |
| `RewardConfig.cpp` | Added integration to PLATINUM phase |
| `OptionControlReward.h` | Core implementation (no changes) |
| `OptionGatingWrappers.h` | Wrapper classes (no changes) |

---

## Build Status
✅ **COMPILE SUCCESS** - All integration complete and tested

---

## Support & Troubleshooting

### Compilation Issues?
- Ensure includes are in correct order in RewardConfig.h
- Check that namespace RLGC is used correctly
- Verify all wrapper classes inherit from RewardWrapper

### Reward Not Applying?
- Check if wrapper is correctly instantiated with optionControl pointer
- Verify the base reward is not also included (avoid duplicates)
- Monitor that GetReward() is being called by checking logs

### Unexpected Behavior?
- Check option detection logic matches your game state expectations
- Verify multiplier curves are appropriate for your reward scale
- Try disabling wrappers to confirm it's the gating causing change

