# Codebase Navigation
**What lives where, what to read, what to touch**

---

## Project Root

```
C:\Projects\RLGymBot_v3\GigaLearnBot\GigaLearnCPP\
│
├── src/
│   ├── ExampleMain.cpp          ← Bot entrypoint; env creation, learner config, phase gating
│   └── RLBotClient.cpp/.h       ← Live deployment client
│
├── RLGymCPP/src/RLGymCPP/
│   ├── CommonValues.h           ← All game physics constants — READ BEFORE ANYTHING ELSE
│   ├── Framework.h              ← RocketSim namespace bridge, basic types
│   ├── Math.h/.cpp              ← Vec, math helpers
│   ├── ObsBuilders/             ← WHERE THIS SESSION'S CHANGES GO
│   ├── Rewards/                 ← Reward classes — DO NOT TOUCH in this session
│   └── Gamestates/              ← GameState, Player, PhysState definitions
│
├── RewardConfig.h/.cpp          ← CURRENT_PHASE, reward weights, phase-gated config
├── ModelConfig.h/.cpp           ← LearnerConfig, network sizes, PPO params
├── GameModeConfig.h/.cpp        ← 1v1/2v2/3v3 mode config
│
└── docs/claude_code/            ← This documentation package
```

---

## Files to Read Before Writing Any Code

Read these in order, noting the specific things listed:

### 1. `CommonValues.h`
Look for and note:
- `BACK_WALL_Y = 5120.0f` ✓ confirmed
- `CEILING_Z = 2044.0f` ✓ confirmed
- `SUPERSONIC_THRESHOLD = 2200.0f` ✓ confirmed
- `BALL_MAX_SPEED = 6000.0f` ✓ confirmed (use for ball speed normalization in B7)
- Coordinate convention comment: **positive Y = toward Orange goal, negative Y = toward Blue goal**

Note the coordinate convention carefully. "Pressure in my half" means opponents have Y on the opposite side from their own goal. Blue player's half = negative Y. Orange player's half = positive Y.

### 2. `Framework.h`
Look for:
- `using namespace RocketSim` — confirms all RocketSim types are in scope
- `RS_CLAMP` macro definition location
- How `Vec` is used (it's the RocketSim Vec type)

### 3. `AdvancedObsPadded_cpp.txt` (the .txt is the actual source)
Read the entire file. Note:
- How `inv` is used for position inversion throughout
- How `InvertPhys(state.ball, inv)` works for the ball state
- The `FList obs = {}` pattern and `obs +=` idiom
- Where `BuildObs` ends — new memory block code goes AFTER the final existing `obs +=`
- The `demoTimers` member variable pattern — the new tracker members follow the same design

### 4. `RewardConfig.h` (header only)
Look for:
- `TrainingPhase` enum definition and exact phase names
- `CURRENT_PHASE` declaration
- `PHASE_5_DIAMOND` exact enum value name

### 5. `ExampleMain.cpp`
Look for:
- Where `ObsBuilder` (or the obs builder class) is instantiated
- How the obs builder is passed to `EnvCreateFunc` or equivalent
- Whether there's a single obs builder or one per env
- The `cfg.ppo.networkArch = NetworkArchitecture::MLP` line — confirms MLP arch
- `ConfigureLearnerConfig(cfg, phaseCfg)` call structure

### 6. `AdvancedRewards.h`, lines 1418–1515 (specific section)
This is the **reference implementation for ball touch tracking** in this codebase. The pattern at line 1433–1435:
```cpp
if (p.ballTouchedStep) {
    lastBallTouchedBy[0] = p.carId;
}
```
**This is how touch detection works.** `player.ballTouchedStep` is a per-player boolean that is true on the exact frame a player contacts the ball. There is NO `state.ball.lastTouch.carId` field. The spec's `<-- VERIFY API` note is now answered: use `player.ballTouchedStep` by iterating `state.players`.

---

## Touch Detection API — Confirmed Pattern

This was the critical unknown in the Mechanic Spec. Research into the codebase confirms:

**`player.ballTouchedStep`** — boolean, true on the frame a player's car contacts the ball.

To detect any touch this step and determine who touched:
```cpp
for (const auto& p : state.players) {
    if (p.ballTouchedStep) {
        bool isOppTouch  = (p.team == oppTeam);
        bool isMyTouch   = (p.carId == perspective.carId);
        bool isTeamTouch = (p.team == myTeam);
        // update EMAs accordingly
    }
}
```

This replaces the `state.ball.lastTouch.carId` approach in the Mechanic Spec Section 13. The `oppTendency.lastTouchCarId` member variable in the spec is no longer needed and should be removed from the struct. The `carId` identity tracking for touch detection was only needed for the old "detect when carId changes" approach.

**Update the `TeamTendency` struct** to remove `lastTouchCarId`.  
**Update `UpdateMemoryTrackers`** to use the iteration pattern above.

---

## Files to Modify (This Session)

| File | What Changes |
|---|---|
| `ObsBuilders/AdvancedObsPadded.h` | Add structs, members, method declarations. Confirm exact filename for new bot with Nathan. |
| `ObsBuilders/AdvancedObsPadded.cpp` | Add method bodies, modify Reset() and BuildObs() |
| `ExampleMain.cpp` | Add SetMemoryBlocksEnabled() call at obs builder construction |

**All other files: read-only this session.**

---

## Files to NOT Touch

| File | Reason |
|---|---|
| Any `Rewards/` file | Explicitly out of scope; reward changes require separate spec |
| `ModelConfig.cpp` | Network architecture is fixed; no changes needed for obs addition |
| `RewardConfig.cpp` | Phase is already defined; the obs builder reads it, not the other way around |
| Any Machine 1 file | Machine 1 is running the old bot in Phase 5; do not disrupt |
| `CMakeLists.txt` | No new source files are being added; methods go into existing .cpp |

---

## Key Type Reference

From `RocketSim` (via `Framework.h`):

```cpp
// GameState fields relevant to this session:
state.players          // std::vector<Player>
state.ball             // PhysState
state.ball.pos         // Vec (x, y, z)
state.ball.vel         // Vec
state.elapsedTime      // float — game clock in seconds
state.deltaTime        // float — seconds since last step

// Player fields relevant to this session:
player.carId           // uint32_t — unique per player per session
player.team            // Team enum (Team::BLUE, Team::ORANGE)
player.pos             // Vec
player.vel             // Vec
player.boost           // float — 0 to 100
player.ballTouchedStep // bool — TRUE on the frame this player touches the ball
player.index           // int — index in state.players array

// Vec operations:
Vec a, b;
a + b; a - b; a * scalar; a / scalar;  // all supported
a.Length()          // magnitude
a.Normalized()      // unit vector
a.Dot(b)            // dot product
```

---

## Obs Builder Class Structure (AdvancedObsPadded)

The obs builder inherits from a base class (`ObsBuilder` or similar from RLGymCPP). The key methods:

```cpp
// Called once at episode start
virtual void Reset(const GameState& initialState) override;

// Called every step, per player
virtual FList BuildObs(const Player& player, const GameState& state) override;
```

`FList` is a list of floats (likely `std::vector<float>` or a thin wrapper). The `obs +=` idiom appends a float or Vec components to the list.

---

## Obs Inversion Convention (Critical)

The game coordinate system:
- Blue team plays toward +Y (attacks orange goal)
- Orange team plays toward -Y (attacks blue goal)

The obs system normalizes this by inverting for orange players: every positional feature becomes "toward my goal = negative, toward opponent goal = positive" regardless of team. This is why `inv = player.team == Team::ORANGE`.

For the new `depthMeanEMA` feature (A3): we want "opponents pushed up toward my end = positive pressure." For a Blue observer, opponent depth Y > 0 (they're in blue's half). For an Orange observer, opponent depth Y < 0, so we negate to make it positive. The `inv` flag handles this.

---

## Established Reward Patterns to Reference (Not to Copy)

| File | Useful for |
|---|---|
| `AdvancedRewards.h:1418–1515` | `lastBallTouchedBy` pattern — reference for multi-player touch tracking |
| `AdvancedOvercommitmentPenalty.h` | `SamplingMemory` struct pattern — reference for per-player EMA state |
| `KickoffRandomizationReward.h` | `PlayerKickoffData` rolling history — reference for history queue pattern |
| `AdvancedObsPadded_cpp.txt` | Entire file — primary reference for how obs builder works |

---

## W&B Metric Naming Convention

Existing W&B metrics in this project follow `category/metric_name` format (e.g., `rewards/goal_reward`, `training/policy_kl`). The new memory feature metrics should follow:
```
memory/blockA_commitment
memory/blockA_aerial
memory/blockA_depth
memory/blockA_boost
memory/blockA_speed
memory/blockA_pressure
memory/blockA_challengeRate
memory/blockA_confidence
memory/blockB_secsSinceMyTouch
memory/blockB_secsSinceTeamTouch
memory/blockB_possession
memory/blockB_boostSlope
memory/blockB_posVariance
memory/blockB_ballMaxZ
memory/blockB_ballSpeedMean
memory/blockB_teamDist
```
W&B hook implementation is out of scope for this session. Nathan will add these post-integration. Note the naming convention so they can be added consistently.
