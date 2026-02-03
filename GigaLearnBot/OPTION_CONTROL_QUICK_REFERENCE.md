# Option Control Reward - Quick Reference Card

## 🚀 What's Live Now

**Location:** PHASE_4_PLATINUM rewards in RewardConfig.cpp

### Three Active Components:

1. **Standalone Reward** `optionControl` (Weight: 0.5f)
   - Rewards positions with many viable plays
   - Very low weight (primarily for gating)

2. **Flick Gating** - AdvancedFlickReward wrapped
   - Multiplier: 0.2x (0 opts) → 1.5x (4+ opts)
   - Encourages flicking from advantageous positions

3. **Pass Accuracy Gating** - PassingAccuracyReward wrapped
   - Multiplier: 0.2x (0 opts) → 1.2x (1.5+ opts)
   - Encourages passing when multiple teammates available

---

## 📊 Option Detection

### What Gets Counted as "Options"

**Scoring (6 types):**
- ✅ Dribble (ball low + nearby)
- ✅ Flick (ball 200-300 UU + nearby)
- ✅ Air Dribble (ball + player airborne + aligned)
- ✅ Bump/Demo (opponent < 500 UU)
- ✅ Powershot (kickable + toward goal ±45°)
- ✅ Pass to Teammate (teammate in range)

**Passing (tiered):**
- ✅ Base: Teammate within 3000 UU = +1.0
- ✅ Bonus: Well-positioned = +0.5

---

## 🎯 Multiplier Curves

### Scoring Gate (For Flick, etc.)
```
0 options → 0.2x
1 option  → 0.6x
2-3       → 1.0x (baseline)
4+        → 1.5x (BONUS!)
```

### Passing Gate (For passes)
```
< 0.5 options → 0.2x
0.5-1.5       → 0.7x
1.5+          → 1.2x (BONUS!)
```

---

## ⚙️ How to Use

### Add Another Gated Reward

**Example: Gate PowerShot**
```cpp
WeightedReward(
    new ScoringRewardWrapper(
        new PowerShotReward(),
        optionControl
    ),
    WEIGHT
),
```

**Example: Gate Assist**
```cpp
WeightedReward(
    new PassingRewardWrapper(
        new AssistReward(),
        optionControl
    ),
    WEIGHT
),
```

**Example: Generic Gate (Both options)**
```cpp
WeightedReward(
    new OptionGatingWrapper(
        new SomeReward(),
        OptionGateType::COMBINED,
        optionControl
    ),
    WEIGHT
),
```

### Adjust Aggressiveness

**Increase gating pressure:**
```cpp
// In OptionControlReward.h, change multiplier max:
static constexpr float MULTIPLIER_MAX_SCORING = 2.0f;  // was 1.5f
```

**Decrease gating pressure:**
```cpp
// In OptionControlReward.h, change multiplier min:
static constexpr float MULTIPLIER_MIN = 0.5f;  // was 0.2f
```

### Tune Detection Ranges

**Make options easier to detect:**
```cpp
// In OptionControlReward.h:
static constexpr float PASS_RANGE = 4000.0f;  // was 3000.0f
static constexpr float DRIBBLE_RANGE = 2000.0f;  // was 1500.0f
```

---

## 📈 Training Tips

### Monitor These Behaviors
1. **Position Seeking**: Bot moves to central areas (more options)
2. **Setup Play**: More time spent positioning before shooting
3. **Action Diversity**: Variety of plays (flick/pass/air dribble mix)
4. **Rotation**: Maintains positions supporting teammates

### If Gating Too Aggressive
- Reduce standalone weight: `0.5f → 0.1f`
- Reduce multiplier max: `1.5f → 1.2f`
- Add more well-positioned teammates to increase pass options

### If Gating Too Weak
- Increase standalone weight: `0.5f → 2.0f`
- Increase multiplier max: `1.5f → 2.0f`
- Enable gating on more rewards

---

## 🔧 Implementation Details

### Core Files
- **OptionControlReward.h** - Counts options, provides multipliers
- **OptionGatingWrappers.h** - Three wrapper classes
- **RewardConfig.cpp** - Integration point (PLATINUM)

### Key Methods
```cpp
// Count options
int scoringOpts = optionControl->CountScoringOptions(player, state);
float passingOpts = optionControl->CountPassOptions(player, state);

// Get multipliers
float sMult = optionControl->GetScoringMultiplier(scoringOpts);
float pMult = optionControl->GetPassingMultiplier(passingOpts);

// Debug
std::string info = optionControl->GetAvailableOptionsString(player, state);
// Output: "Scoring: 3 | Passing: 1.5"
```

---

## 📋 Wrapper Classes

### ScoringRewardWrapper
- **Use for**: Flick, PowerShot, AirDribble, Bump, Demo, etc.
- **Multiplier**: Based on scoring options only
- **Best for**: Skills that have multiple execution opportunities

### PassingRewardWrapper
- **Use for**: Pass, Assist, BackPass, PassAccuracy, etc.
- **Multiplier**: Based on passing options only
- **Best for**: Team-oriented actions

### OptionGatingWrapper
- **Use for**: Hybrid rewards that involve both scoring AND passing
- **Multiplier**: Average of both (configurable)
- **Best for**: Complex plays like setup/shot combinations

---

## ✅ Build Status

```
✅ RewardConfig.h compiles
✅ RewardConfig.cpp compiles
✅ OptionControlReward.h included
✅ OptionGatingWrappers.h included
✅ All wrappers instantiate correctly
✅ No duplicate rewards
✅ Scoping correct
✅ PLATINUM phase functional
✅ Ready to train
```

---

## 🎓 Examples in Platinum

### Currently Wrapped
1. **AdvancedFlickReward** (6.0f weight)
   - Gated by scoring options

2. **PassingAccuracyReward** (20.0f weight)
   - Gated by passing options

### Easy to Add Next
- BumpReward (scoring gate)
- DemoReward (scoring gate)
- AssistReward (passing gate)
- BackPassReward (passing gate)

---

## 🚨 Common Issues

**Q: Bot not seeking optionality**
→ Increase standalone weight: `0.5f → 2.0f`

**Q: Flicks/passes getting too high/low rewards**
→ Adjust multiplier curve in OptionControlReward methods

**Q: Option counting seems off**
→ Check detection ranges in OptionControlReward.h constexpr values

**Q: Build fails with includes**
→ Ensure paths match exactly in RewardConfig.cpp (relative paths)

---

## 📚 Documentation Files

Created for you:
- `OPTION_CONTROL_IMPLEMENTATION.md` - Full technical details
- `OPTION_CONTROL_INTEGRATION_GUIDE.md` - How to integrate more rewards
- `INTEGRATION_CODE_SUMMARY.md` - Exact code changes made
- `OPTION_CONTROL_QUICK_REFERENCE.md` - This file!

---

## 🎯 Next Steps

1. **Run training** with current config (low-weight, 2 wrapped rewards)
2. **Observe behavior** - Does bot seek center positions with options?
3. **Check metrics** - Option count trends during episodes
4. **Iterate** - Adjust weights/thresholds based on observations
5. **Expand** - Add more wrapped rewards as desired

---

## 📞 Quick Command Reference

### Enable/Disable Option Control
```cpp
// Disable (keep wrappers but no standalone):
// WeightedReward(optionControl, 0.0f),

// Increase pressure:
WeightedReward(optionControl, 2.0f),

// Gentle approach:
WeightedReward(optionControl, 0.1f),
```

### Test Without Wrappers
```cpp
// Temporarily disable gating:
WeightedReward(
    new AdvancedFlickReward(),  // Direct, no wrapper
    6.0f
),
```

### Debug Option Counts
Add to any reward's GetReward():
```cpp
if (player.carId == 0) {  // Log only first player
    auto optStr = optionControl->GetAvailableOptionsString(player, state);
    std::cout << "Player options: " << optStr << std::endl;
}
```

---

**Status: ✅ READY TO TRAIN**

Your bot now has the Option Control Reward system live and ready!

