# Reward Implementation Summary

## Overview
Two new rewards have been implemented to improve bot rotation behavior and numerical advantage exploitation:

1. **BoostAwareRotationReward** - Already implemented in previous session
2. **NumericalAdvantageAttackingReward** - Newly implemented

---

## 1. BoostAwareRotationReward (Previously Completed)

### What It Does
Incentivizes players to adopt proper rotation roles based on their boost levels:
- **Low-boost players** (< 75 boost) should stay in **FURTHEST** role (last man, defensive)
- **High-boost players** (> 100 boost) should be in **CLOSEST** role (first man, offensive)
- Creates a natural "rotation circle" where players dynamically adjust positions based on available resources

### Key Features
- **Goal-line distance detection:** Determines roles by how close each player is to their own goal
- **Works in 2v2, 3v3+:**
  - 2v2: FURTHEST (last man) / CLOSEST (first man)
  - 3v3+: FURTHEST / MIDDLE / CLOSEST
- **Boost disparity amplification:** Rewards are amplified 1.5x when boost difference > 30
- **Stability window:** Tracks roles for 120 steps (~1 second) to prevent thrashing

### Reward Structure
- **Event reward (+0.3f):** When transitioning to correct role
- **Shaping reward (0.2f base):** For maintaining correct roles
- **Boost amplifier (1.5x):** When disparity > 30 boost
- **Penalty (-0.15f):** When low-boost player in CLOSEST role

### Integration
- **Location:** `RotationRewards.h` (lines 786-987)
- **Config Integration:** Added to Platinum (3.5x) and Diamond (4.0x) phases
- **Activation:** Only in Platinum+ when `enableRoleCycling` is true

---

## 2. NumericalAdvantageAttackingReward (NEW)

### What It Does
Makes bots much more aggressive and coordinated in numerical advantage situations (2v1, 3v2, etc.):
- **Demo/bump tactics:** Primary attacker demos defender while support shoots
- **Passing plays:** High-velocity passes between teammates for clean goals
- **Aggressive positioning:** Rewards staying in attacking zone during advantages

### Key Features

#### Role Detection
- **Primary attacker:** Closest teammate to ball (goes for demo/shot)
- **Support attacker:** Second-closest (positions for pass/followup)
- Only activates when: attacking_players >= defending_players + 1

#### Two Main Attack Paths

**Path 1: Demo/Bump Tactic**
1. Primary attacker approaches defender (< 1500 UU)
2. Primary executes demo/bump (+0.4f event reward)
3. Support attacker shoots within 2 seconds (+0.5f event reward)
4. Both get credit for successful 2v1 sequence

**Path 2: Passing Tactic**
1. Primary attacker in passing position (1500-3500 UU from goal)
2. Makes high-velocity pass (> 500 UU/s) to teammate (+0.4f)
3. Support positioned for reception (1000-2500 UU from goal)
4. Support shoots within 2 seconds (+0.5f)
5. Both players get equal reward for successful pass sequences

#### General Aggression
- **Per-frame bonus (+0.1f):** For each attacker in attacking zone
- **Scales with advantage:** Bonus = 0.1f × (attacking_count - defending_count) / 2
- **Retreat penalty (-0.05f):** For not pushing advantage when clearly outnumbering opponents

### Integration
- **Location:** `ShootingRewards.h` (end of file, lines 291+)
- **Config Integration:**
  - Gold phase: 4.0x weight
  - Platinum phase: 5.0x weight
  - Diamond phase: 6.0x weight (highest emphasis)
- **Activation:** From Gold+ (basic aggression phase onward)

### Constants
```cpp
DEMO_RANGE = 1500.0f                  // Range for demo attempt
PASS_QUALITY_THRESHOLD = 500.0f       // Min velocity for quality pass
ATTACKING_ZONE_DIST = 5000.0f        // Distance threshold from goal
POST_ACTION_WINDOW = 120              // Steps to reward followup actions (~1 sec)
PASS_POSITION_MIN/MAX = 1500/3500     // Valid passing position range
RECEPTION_POSITION_MIN/MAX = 1000/2500 // Valid reception position range
```

---

## Implementation Details

### BoostAwareRotationReward Memory Structure
```cpp
struct RoleMemory {
    BoostRole lastFrameRole;          // Current role (FURTHEST/MIDDLE/CLOSEST)
    int roleStepCount;                // Frames in current role
    bool hasGivenEventReward;         // Track if event reward given
    float lastBoostDisparity;         // Boost difference from closest teammate
};
```

### NumericalAdvantageAttackingReward Memory Structure
```cpp
struct AdvantageMemory {
    AttackerRole lastRole;            // Role in current advantage
    int framesSinceDemoBump;          // Frames since demo occurred
    int framesSincePassing;           // Frames since pass occurred
    bool lastFrameHadPossession;      // Ball possession tracking
    uint32_t lastDemoTarget;          // ID of demo target
};
```

---

## How They Work Together

### Phase Progression
1. **Gold Phase:** Both rewards activate
   - `BoostAwareRotationReward`: Not active (needs Platinum+)
   - `NumericalAdvantageAttackingReward`: Active (4.0x weight)

2. **Platinum Phase:** Both activate with moderate weights
   - `BoostAwareRotationReward`: 3.5x weight
   - `NumericalAdvantageAttackingReward`: 5.0x weight

3. **Diamond Phase:** Both activate with higher emphasis
   - `BoostAwareRotationReward`: 4.0x weight
   - `NumericalAdvantageAttackingReward`: 6.0x weight (highest focus on numerical advantage play)

### Interaction
- **Rotation reward** handles teammate positioning and boost-aware role assignment
- **Advantage reward** handles aggressive exploitation of those advantages
- Together: Creates bots that intelligently rotate roles *and* aggressively exploit when they have superior numbers

---

## Testing & Verification

### For BoostAwareRotationReward
- [ ] Test 2v2 match: Low-boost bot retreats when teammate has high boost
- [ ] Test 3v3 match: FURTHEST/MIDDLE/CLOSEST roles adapt to boost levels
- [ ] Monitor role stability: Verify no excessive switching (120-step window working)
- [ ] Check reward signal: Ensure correlation with boost disparity in training logs

### For NumericalAdvantageAttackingReward
- [x] Code compiles successfully
- [ ] Test 2v1 demo scenario: Primary demos, support shoots (reward activates)
- [ ] Test 2v1 passing: Primary passes to support positioned for shot
- [ ] Test 3v2 scenario: Verify aggression scales with advantage magnitude
- [ ] Test retreat penalty: Verify bots punished for backing off during 2v1
- [ ] Verify role assignment: Only primary goes for demos, support positions properly
- [ ] Check post-action window: Followup shots/actions rewarded within ~1 second

---

## File Changes

### Modified Files
1. **RotationRewards.h**
   - Added `BoostAwareRotationReward` class (lines 786-987)
   - Includes role detection by goal-line distance
   - Boost disparity tracking and amplification

2. **ShootingRewards.h**
   - Added `NumericalAdvantageAttackingReward` class (end of file)
   - Includes demo/bump and passing path detection
   - Aggressive positioning shaping rewards

3. **RewardConfig.cpp**
   - Added integration for `BoostAwareRotationReward` in Platinum (3.5x) and Diamond (4.0x)
   - Added integration for `NumericalAdvantageAttackingReward` in Gold (4.0x), Platinum (5.0x), and Diamond (6.0x)

---

## Expected Bot Behavior Changes

### With BoostAwareRotationReward
- Bots will naturally rotate back when low on boost
- High-boost teammates will advance to press advantage
- Smoother, less chaotic team positioning
- Fewer ball-chasing collisions

### With NumericalAdvantageAttackingReward
- Bots will seek and exploit 2v1 opportunities
- Primary attacker will position for demos when opponent isolated
- Support will follow up with shots after demos
- Passing plays will increase in numerical advantage situations
- Fewer solo-dribble attempts when outnumbering opponent
- More coordinated, aggressive attacking

### Combined Effect
- Better team coordination based on boost availability
- More intelligent exploitation of numerical advantages
- Smoother transitions between defensive and offensive phases
- Higher-quality scoring opportunities through coordinated plays

---

## Configuration Tuning

Both rewards can be tuned via their constants:

### BoostAwareRotationReward Tuning
- `LOW_BOOST_THRESHOLD`: Lower value = more aggressive, higher value = more defensive
- `HIGH_BOOST_THRESHOLD`: How much boost needed to be "high-boost" player
- `BOOST_DISPARITY_THRESHOLD`: Minimum difference to trigger amplification
- `ROLE_STABILITY_STEPS`: Increase to prevent more role switching

### NumericalAdvantageAttackingReward Tuning
- `DEMO_RANGE`: Increase to reward demo attempts from further away
- `PASS_QUALITY_THRESHOLD`: Increase to only reward harder passes
- `ATTACKING_ZONE_DIST`: Increase to expand "attacking zone"
- `POST_ACTION_WINDOW`: Increase to give more time for followup actions
- Weights in RewardConfig: Increase to prioritize 2v1 exploitation

---

## Notes for Training

1. Both rewards are designed to work with PPO's short feedback loops
2. Event rewards provide immediate milestone feedback
3. Shaping rewards provide continuous gradient for learning
4. Memory tracking prevents reward gaming and oscillation
5. Role stability windows prevent thrashing between roles
6. Phase-based integration ensures rewards activate at appropriate skill levels

All changes are backward compatible and don't affect existing reward infrastructure.
