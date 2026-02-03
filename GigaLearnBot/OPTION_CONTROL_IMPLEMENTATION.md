# General Purpose Option Control Reward - Implementation Summary

## ✅ Phase 1 Complete: Core Implementation

### What Was Built

Two new reward files have been successfully created and integrated into your rewards system:

#### 1. **OptionControlReward.h** (Core Implementation)
**Location:** `C:\Projects\RLGymBot_v3\GigaLearnBot\GigaLearnCPP\RLGymCPP\src\RLGymCPP\Rewards\OptionControlReward.h`

**Core Methods:**

| Method | Purpose | Returns |
|--------|---------|---------|
| `CountScoringOptions(player, state)` | Enumerate all 6 scoring play types available | int (0-6) |
| `CountPassOptions(player, state)` | Count pass opportunities with tiering | float (0-3+) |
| `GetScoringMultiplier(options)` | Gating multiplier for scoring rewards | 0.2x to 1.5x |
| `GetPassingMultiplier(options)` | Gating multiplier for passing rewards | 0.2x to 1.2x |
| `GetReward(player, state, isFinal)` | Standalone reward for optionality | 0.0 to 1.0 |

**Scoring Options Detected:**
1. **Dribble** - Ball on ground (< 300 UU), player < 1500 UU away
2. **Flick** - Ball at 200-300 UU height, player < 2000 UU away
3. **Air Dribble** - Ball > 150 UU, player airborne, moving in similar direction
4. **Bump/Demo** - Any opponent < 500 UU away
5. **Powershot** - Ball kickable, moving toward goal (±45°), speed 800-2300 UU/s
6. **Pass** - At least one teammate within 3000 UU

**Passing Options Detected:**
- Base: Any teammate within 3000 UU = +1.0 option
- Bonus: Well-positioned teammate (ahead, offensive side) = +0.5 option

**Multiplier Curves:**

Scoring:
```
0 options → 0.2x
1 option  → 0.6x
2-3 opts  → 1.0x (base)
4+ opts   → 1.5x (bonus)
```

Passing:
```
< 0.5 options → 0.2x
0.5-1.5 opts  → 0.7x
1.5+ opts     → 1.2x (bonus)
```

---

#### 2. **OptionGatingWrappers.h** (Integration Helpers)
**Location:** `C:\Projects\RLGymBot_v3\GigaLearnBot\GigaLearnCPP\RLGymCPP\src\RLGymCPP\Rewards\OptionGatingWrappers.h`

Three wrapper classes provided for easy integration:

**A. `ScoringRewardWrapper`**
- Wraps any scoring reward
- Automatically gates with scoring option multiplier
- Usage: `new ScoringRewardWrapper(new AdvancedFlickReward(), optionControl)`

**B. `PassingRewardWrapper`**
- Wraps any passing reward
- Automatically gates with passing option multiplier
- Usage: `new PassingRewardWrapper(new PassSetupReward(), optionControl)`

**C. `OptionGatingWrapper`** (Generic)
- Flexible wrapper supporting three gating modes:
  - `SCORING` - Gate with scoring options
  - `PASSING` - Gate with passing options
  - `COMBINED` - Gate with average of both
- Usage: `new OptionGatingWrapper(reward, OptionGateType::SCORING, optionControl)`

---

### How It Works

#### Standalone Usage
```cpp
// In RewardConfig.cpp
WeightedReward(new OptionControlReward(), 0.5f)  // Low weight to start
```

The bot gets small rewards for being in positions with many options.

#### Gating Usage (Recommended)
```cpp
// In RewardConfig.cpp
OptionControlReward* optCtrl = new OptionControlReward();

WeightedReward(
    new ScoringRewardWrapper(
        new AdvancedFlickReward(),
        optCtrl
    ),
    35.0f
)

WeightedReward(
    new PassingRewardWrapper(
        new PassSetupReward(),
        optCtrl
    ),
    20.0f
)
```

This automatically modulates rewards:
- Flick in low-option position → gets reduced reward (0.2-0.6x)
- Flick in high-option position → gets bonus reward (1.0-1.5x)
- Encourages seeking positions with multiple viable plays

---

### Configuration Parameters (All Tunable)

All thresholds are `static constexpr` for easy adjustment:

```cpp
// Distances (in UU - Unreal Units)
DRIBBLE_RANGE = 1500.0f
FLICK_RANGE = 2000.0f
AIR_DRIBBLE_RANGE = 2000.0f
BUMP_RANGE = 500.0f
PASS_RANGE = 3000.0f

// Heights
FLICK_HEIGHT_MIN = 200.0f
FLICK_HEIGHT_MAX = 300.0f
AIR_DRIBBLE_HEIGHT = 150.0f

// Angle constraints
POWERSHOT_ANGLE_RANGE = 45.0f (degrees)
POWERSHOT_MIN_SPEED = 800.0f
POWERSHOT_MAX_SPEED = 2300.0f

// Multiplier ranges
MULTIPLIER_MIN = 0.2f
MULTIPLIER_MAX_SCORING = 1.5f
MULTIPLIER_MAX_PASSING = 1.2f
```

---

## Next Steps: Phase 2 & 3 Integration

### Recommended Integration Path

**Option A: Minimal (Standalone - Test first)**
1. Add `new OptionControlReward()` with weight 0.5f to RewardConfig
2. Train and observe bot behavior
3. Adjust weight based on results

**Option B: Moderate (Gating on Key Rewards)**
1. Create `OptionControlReward*` instance in RewardConfig
2. Wrap 3-5 key scoring rewards (flick, air dribble, powershot)
3. Wrap 2-3 key passing rewards (pass setup, receiving)
4. Train and observe

**Option C: Comprehensive (Full Integration)**
1. Implement wrappers for all offensive rewards
2. Use `COMBINED` gate type for hybrid action rewards
3. Potentially add option control to defensive rewards
4. Progressive rollout during training phases

### Integration Instructions

To integrate into RewardConfig.cpp:

```cpp
#include "OptionControlReward.h"
#include "OptionGatingWrappers.h"

// In GetPhasedRewards() for your phase:

// Create shared instance
OptionControlReward* optionControl = new OptionControlReward();

// Add as standalone reward (very low weight)
WeightedReward(optionControl, 0.5f),

// Gate scoring rewards
WeightedReward(
    new ScoringRewardWrapper(new AdvancedFlickReward(), optionControl),
    35.0f
),

// Gate passing rewards
WeightedReward(
    new PassingRewardWrapper(new PassSetupReward(), optionControl),
    20.0f
),

// ... rest of rewards
```

---

## Build Status

✅ **Phase 1 Complete** - Core implementation compiles cleanly
✅ **Phase 2 Complete** - Gating wrappers compile cleanly
✅ **No Errors** - All syntax validation passed
✅ **Ready for Integration** - Can be added to RewardConfig.cpp

---

## Testing Checklist

Before full training integration, verify:

- [ ] Option counting works in various game states
  - Ground level with ball nearby → dribble + flick detected
  - Airborne far from ball → pass option detected
  - Near opponent → bump option detected
  - etc.

- [ ] Multiplier curves are reasonable
  - 0 scoring options → 0.2x multiplier
  - 4+ scoring options → 1.5x multiplier
  - Smooth scaling in between

- [ ] Wrapper integration works
  - Wrapped rewards still produce sensible values
  - Multipliers apply correctly
  - No reward collapse

- [ ] Integration with existing rewards
  - Compile cleanly with RewardConfig.cpp
  - No memory leaks (all pointers properly owned)
  - Lifecycle (Reset/PreStep) called correctly

---

## Debugging Tools

The implementation includes debugging helpers:

```cpp
// Get string representation of available options
std::string debugStr = optionControl->GetAvailableOptionsString(player, state);
// Output: "Scoring: 3 | Passing: 1.5"

// Query last frame's option counts
int lastScoring = optionControl->GetLastScoringOptions(player.carId);
float lastPassing = optionControl->GetLastPassingOptions(player.carId);
```

---

## Key Design Decisions

1. **Simplified Heuristics** - Used distance/height checks, not physics sims (fast, good signal)
2. **Tiered Pass Options** - Base + bonus for positioning (natural difficulty scaling)
3. **Bonus-based Gating** - Base reward always available, bonus when options high (gentle encouragement)
4. **Separate Scoring/Passing Gates** - Different multiplier curves for different action types
5. **Modular Architecture** - Can use standalone, per-reward, or globally

---

## Future Refinements (Phase 4)

Based on training observation, consider:

1. **Temporal Persistence** - Only count options that exist for N frames
2. **Option Quality** - Weight options by feasibility/probability
3. **Lookahead** - Predict if options will exist in next 0.5-2 seconds
4. **Adaptive Thresholds** - Learn distance/height thresholds from play data
5. **Strategic Weighting** - Some options more valuable than others (scoring > passing)
6. **Contextual Gates** - Different multipliers based on game situation (down/up in score)

---

## Questions & Refinement

After training with this system, you can:

- Adjust range thresholds if options are over/under-detected
- Modify multiplier curves if gating is too aggressive/lenient
- Add new option types if major play styles are missed
- Refine teammate positioning criteria for passes
- Add scoring type discrimination (air dribble vs ground dribble)

Every parameter is tunable - this is designed for iterative improvement.

