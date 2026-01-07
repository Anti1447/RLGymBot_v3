# Compilation Error Fix Guide

## Quick Fix Checklist

### 1. Remove Duplicate `advanced_mechanic_rewards.h`

**Problem:** Classes defined in both files without namespace:
- `advanced_mechanic_rewards.h` (OLD, no namespace)
- `CompleteMechanicsRewards.h` (NEW, with RLGC namespace)

**Fix:**
```bash
# Option A: Delete the old file
rm GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/advanced_mechanic_rewards.h

# Option B: Rename it as backup
mv GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/advanced_mechanic_rewards.h \
   GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/advanced_mechanic_rewards.h.bak
```

### 2. Fix HalfFlipReward Duplicate

**Problem:** Defined in both:
- `CompleteMechanicsRewards.h:117`
- `AdvancedRewards.h:309`

**Fix:** Remove from `AdvancedRewards.h` or rename one variant.

**In AdvancedRewards.h:**
```cpp
// BEFORE (line 309):
class HalfFlipReward : public Reward {

// AFTER - either delete or rename:
class AdvancedHalfFlipReward : public Reward {  // Renamed
// OR just delete the duplicate
```

### 3. Fix Enum Scoping in Enhanced Files

**Problem:** `Side` enum declared AFTER struct that uses it.

**Fix:** Replace `GodTierMovementRewards_Enhanced.h` with the fixed version:

```cpp
// WRONG ORDER:
class EnhancedMenaFlipReward : public Reward {
private:
    struct MenaData {
        Side wallSide;  // ERROR: Side not yet declared
    };
    
    enum class Side { NONE, LEFT, RIGHT };  // Declared too late!

// CORRECT ORDER:
class EnhancedMenaFlipReward : public Reward {
private:
    enum class Side { NONE, LEFT, RIGHT };  // Declared FIRST
    
    struct MenaData {
        Side wallSide;  // OK: Side is already declared
    };
```

**Action:** Use the provided `GodTierMovementRewards_Enhanced_Fixed.h` file.

### 4. Fix GameState Forward Declaration

**Problem:** `ExampleMain.cpp` uses `GameState` without full definition.

**Fix:** Include proper header at top of ExampleMain.cpp:

```cpp
// Add this include:
#include "RLGymCPP/GameState.h"  // Or wherever GameState is fully defined

// Then use fully qualified names:
void StepCallback(GGL::Learner* learner, 
                  const std::vector<RLGC::GameState>& states,  // RLGC:: prefix
                  GGL::Report& report) {
    // ...
}
```

### 5. Fix Report::AddAvg Calls

**Problem:** `Report::AddAvg()` requires 2 arguments but only 1 provided.

**Fix in ExampleMain.cpp:**

```cpp
// WRONG:
report.AddAvg("Player Boost Avg");

// CORRECT:
float avgBoost = 0.0f;
for (const auto& player : state.players) {
    avgBoost += player.boost;
}
avgBoost /= state.players.size();
report.AddAvg("Player Boost Avg", avgBoost);
```

### 6. Fix RandomState Constructor

**Problem:** Line 684 tries to default-construct `RandomState`.

**Fix:**

```cpp
// WRONG:
RLGC::RandomState randomState;

// CORRECT - provide required seed:
RLGC::RandomState randomState(12345);  // Or whatever seed you need
```

### 7. Fix WeightedReward Initializer Lists

**Problem:** Nested initializer lists improperly constructed.

**Example error location:** Lines 213, 247, 401, 466, 528, 589

**Fix:**

```cpp
// WRONG:
return {
    // ... other rewards ...
    { new RLGC::DoubleTapReward(), 1.0f },  // Missing RLGC:: in many places
};

// CORRECT - make sure all rewards are properly namespaced:
return {
    { new RLGC::EventReward(CommonValues::BALL_SCORED_EVENT), 10.0f },
    { new RLGC::VelocityPlayerToBallReward(), 1.0f },
    { new RLGC::VelocityBallToGoalReward(), 1.0f },
    { new RLGC::TouchBallReward(), 1.0f },
    { new RLGC::BoostPickupReward(), 0.25f },
    { new RLGC::GoalSpeedReward(), 5.0f },
    { new RLGC::HalfFlipReward(), 2.0f },  // Make sure this is unique
    // Add with RLGC:: prefix:
    { new RLGC::EnhancedDoubleTapReward(), 2.0f },
    { new RLGC::BoostStealReward(), 1.0f },
    { new RLGC::ReverseAerialReward(), 1.5f },
    { new RLGC::FieldSpacingReward(), 0.8f }
};
```

## Complete Fix Script

Save this as `fix_compilation_errors.sh`:

```bash
#!/bin/bash

echo "Fixing Rocket League Bot Compilation Errors..."

# 1. Backup old files
echo "Creating backups..."
cp -r GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards.backup

# 2. Remove duplicate file
echo "Removing duplicate advanced_mechanic_rewards.h..."
rm -f GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/advanced_mechanic_rewards.h

# 3. Replace with fixed enhanced file
echo "Copying fixed GodTierMovementRewards_Enhanced.h..."
cp GodTierMovementRewards_Enhanced_Fixed.h \
   GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/GodTierMovementRewards_Enhanced.h

# 4. Check for duplicate HalfFlipReward
echo "Checking for duplicate HalfFlipReward..."
grep -n "class HalfFlipReward" GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/*.h

echo "Done! Now:"
echo "1. Manually fix HalfFlipReward duplicate in AdvancedRewards.h"
echo "2. Fix ExampleMain.cpp (see guide above)"
echo "3. Run cmake and make again"
```

## Verification Steps

After applying fixes, verify:

```bash
# 1. Check for duplicate class definitions
echo "Checking for duplicates..."
grep -r "class HalfFlipReward" GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/
grep -r "class DoubleTapReward" GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/

# 2. Run linter
python reward_linter.py GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/*.h

# 3. Compile unit tests first
g++ -std=c++17 -I./GigaLearnCPP/RLGymCPP/src RewardSystemUnitTests.cpp -o reward_tests
./reward_tests

# 4. Build main project
cd GigaLearnCPP/build
cmake ..
make
```

## Expected Output After Fixes

```
[ 99%] Building CXX object CMakeFiles/GigaLearnBot.dir/src/ExampleMain.cpp.obj
[100%] Linking CXX executable GigaLearnBot.exe
[100%] Built target GigaLearnBot
```

## If Still Getting Errors

### Error: "DoubleTapReward ambiguous"
**Cause:** Both files still present
**Fix:** Completely remove `advanced_mechanic_rewards.h`

### Error: "wallSide unknown override specifier"
**Cause:** Using old Enhanced file
**Fix:** Use `GodTierMovementRewards_Enhanced_Fixed.h`

### Error: "GameState undefined"
**Cause:** Missing include
**Fix:** `#include "RLGymCPP/GameState.h"` in ExampleMain.cpp

### Error: "AddAvg does not take 1 arguments"
**Cause:** Missing value parameter
**Fix:** Pass both metric name AND value

## Testing After Fixes

1. **Run linter:**
```bash
python reward_linter.py GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/CompleteMechanicsRewards.h
```

2. **Run unit tests:**
```bash
./reward_tests
```

3. **Compile main project:**
```bash
cd GigaLearnCPP/build && make
```

4. **Check warnings:**
Look for any remaining C4251 warnings (these are OK, just DLL interface warnings).

## Summary of File Changes

| File | Action | Reason |
|------|--------|--------|
| `advanced_mechanic_rewards.h` | DELETE | Duplicate classes, no namespace |
| `GodTierMovementRewards_Enhanced.h` | REPLACE | Enum scoping issues |
| `AdvancedRewards.h` | EDIT | Remove duplicate HalfFlipReward |
| `ExampleMain.cpp` | EDIT | Fix GameState, Report calls |
| `CompleteMechanicsRewards.h` | KEEP | This is the main file |

All fixed files provided in this delivery!
