# TTI Utility — Claude Code Integration Brief
**Project:** GigaLearnBot v3 / RLGymCPP  
**Generated:** 2026-03-03  
**Scope:** Wire `TTIUtility.h` + `TTIUtility.cpp` into the existing reward pipeline and replace/augment the current possession rewards.

---

## 1. Context & Goal

This document captures a full design session. The goal was to replace the fragmented per-reward `CalculateTimeToReach()` implementations with a single shared **Time-To-Intercept (TTI) utility** that:

- Uses a **Dubins path approximation** (arc + straight) for horizontal TTI instead of a Euclidean straight-line estimate with a crude turn-penalty scalar.
- Adds a **vertical height component** from jump and boost physics to determine which aerial branch (ground / single-jump / double-jump / fast-aerial) the car needs to reach a given ball height.
- Exposes per-frame **TTI snapshots** — self, best ally, best opponent — with frame-to-frame **deltas** that surface fake/bait outcomes without a separate detector reward.
- Is **thread-safe** for 310 concurrent environment threads via `thread_local` storage.

---

## 2. Files to Create

### 2a. `TTIUtility.h`
Already generated and provided as a separate output file.

**Destination:**
```
GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/TTIUtility.h
```

Contains (in order):
| Section | Class / Struct | Purpose |
|---|---|---|
| 1 | `TTIPhysics` namespace | Physics constants pulled from `CommonValues.h` |
| 2 | `AerialBranch` enum | GROUND / SINGLE_JUMP / DOUBLE_JUMP / FAST_AERIAL |
| 3 | `PredictVerticalProfile()` | Max Z reachable at time t per branch |
| 4 | `EuclideanTTI()` | Baseline (old approach) — kept for W&B delta logging |
| 5 | `DubinsTTI()` | Horizontal arc+straight + vertical reach check |
| 6 | `TTISnapshot` struct | Per-player per-frame TTI data |
| 7 | `TTICache` class | Thread-local cache; O(n) first call, O(1) subsequent |
| 8 | `TTIPossessionControlReward` | Drop-in reward using the cache |

### 2b. `TTIUtility.cpp`
Must be created alongside the header. Required because `thread_local` class statics cannot be defined in a header.

**Destination:**
```
GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/TTIUtility.cpp
```

**Full content:**
```cpp
// TTIUtility.cpp
// Thread-local storage definitions for TTICache.
// 2026-03-03 | Rev 1.0 | NJH
#include "TTIUtility.h"

namespace RLGC {
    thread_local uint64_t                          TTICache::s_lastTick     = 0;
    thread_local std::map<uint32_t, TTISnapshot>   TTICache::s_snapshots;
    thread_local std::map<uint32_t, TTISnapshot>   TTICache::s_prevSnapshots;
}
```

---

## 3. Files to Modify

### 3a. `CMakeLists.txt`
Add `TTIUtility.cpp` to the source list. Find the block that lists reward `.cpp` files and append:

```cmake
# In the rewards source list, add:
${REWARDS_DIR}/TTIUtility.cpp
```

If rewards are header-only and there is no existing `.cpp` pattern, add a new explicit target source:
```cmake
target_sources(RLGymCPP PRIVATE
    src/RLGymCPP/Rewards/TTIUtility.cpp
)
```

### 3b. `RewardConfig.cpp` — `GetPhasedRewards()`
`TTIPossessionControlReward` is a **drop-in companion** to the existing possession rewards, not a hard replacement. Integrate as follows:

**Step 1 — Add include at top of `RewardConfig.cpp`:**
```cpp
#include <RLGymCPP/Rewards/TTIUtility.h>
```

**Step 2 — In `GetPhasedRewards()`, replace or augment possession entries:**

The existing rewards to consider replacing/removing:
- `TimeToBalPossessionReward` — fully superseded; remove.
- `MultiAgentPossessionReward` — superseded for TTI logic; remove.
- `OffensivePossessionPositionReward` — keep; it adds goal-side positional context that TTI doesn't cover.

Add the new reward with a weight appropriate to the current phase. Start conservative:

```cpp
// Phase 5 Diamond example — conservative weight, tune upward over 2-4B steps
rewards.push_back({ new TTIPossessionControlReward(
    TTIPossessionControlReward::Config::Conservative()), 1.2f });
```

For Phase 6+ Champion, use Balanced:
```cpp
rewards.push_back({ new TTIPossessionControlReward(
    TTIPossessionControlReward::Config::Balanced()), 1.5f });
```

**Step 3 — Add `TTICache::Reset()` call in any reward that calls `Reset()`:**

The cache auto-invalidates on tick change, so this is only needed as a safety measure for thread reuse. The `TTIPossessionControlReward::Reset()` already calls it internally — no extra work needed unless you build custom rewards that call `TTICache::Get()` directly.

---

## 4. Thread Safety — Critical Detail

### The Problem
`TTICache` originally used `static inline` maps — global singletons shared across all 310 environment threads. That is a data race.

### The Fix
Use `thread_local` instead. Each OS thread (one per environment in RLGymCPP) gets its own private cache. No mutexes needed. Zero synchronization overhead.

The `TTIUtility.cpp` file above implements this correctly. As long as that `.cpp` is compiled and linked, thread safety is handled.

### Thread Reuse Guard
If GigaLearnCPP reuses OS threads across environment resets (thread pool pattern), stale cache data could persist. The fix is already implemented: `TTIPossessionControlReward::Reset()` calls `TTICache::Reset()`, which zeroes `s_lastTick` and clears both maps. This fires on every episode reset, guaranteeing a clean slate.

---

## 5. Physics Design Decisions

### Dubins TTI vs. Euclidean TTI
| | Euclidean (old) | Dubins (new) |
|---|---|---|
| Turn cost | Scalar multiplier on alignment | Arc length = `r_min * θ` |
| Turn radius | Ignored | `speed / CAR_MAX_ANG_VEL` |
| At supersonic (2300 UU/s) | ~Same as straight-line | r_min ≈ 418 UU; 120° turn adds ~875 UU arc |
| Vertical component | None | Branch selection + binary search for extra time |
| W&B logging | N/A | `euclideanTTI` stored in snapshot for delta |

### Vertical Branch Selection Priority
Branches are tried cheapest-first:
1. `SINGLE_JUMP` — if `hasDoubleJumped == false`
2. `DOUBLE_JUMP` — if `hasDoubleJumped == false`
3. `FAST_AERIAL` — if `boost >= 15`

If none reach ball Z within horizontal TTI, a binary search over [0, 3s] finds the minimum extra time needed. Ball Z tolerance is ±50 UU.

### Jump Physics
- Immediate impulse: 292 UU/s (`JUMP_IMMEDIATE_FORCE`)
- Held force integrated over min(t, 0.2s): 1458 UU/s² × holdTime
- Double-jump: second 292 UU/s impulse at ~midpoint timing
- Fast aerial vertical boost efficiency: 80% of `BOOST_ACCEL_AIR` (car pitching, not perfectly vertical)

---

## 6. Reward Structure — `TTIPossessionControlReward`

Six additive terms, all independently weighted:

| Term | What it measures | Default weight |
|---|---|---|
| `wPossess` | `(bestOppTTI - selfTTI)` normalised to [-1,1] | 1.0 |
| `wTeam` | `(bestOppTTI - min(selfTTI, bestAllyTTI))` | 0.8 |
| `wDuration` | Seconds in possession, capped at 5s | 0.4 |
| `wDubinsAccuracy` | Dubins TTI improvement over Euclidean | 0.2 |
| `wClosing` | `dSelfTTI < 0` (closing on ball faster) | 0.3 |
| `wOppDiverge` | `dBestOppTTI > 0` (opponent falling behind) | 0.5 |

The last two terms (`wClosing`, `wOppDiverge`) together act as an implicit fake/bait signal: when a fake works, the agent's TTI drops while the opponent's jumps. This removes the need for a separate `DetectFake()` heuristic reward.

### Presets
```cpp
TTIPossessionControlReward::Config::Conservative()  // Start here, Phase 5
TTIPossessionControlReward::Config::Balanced()       // Phase 6+
TTIPossessionControlReward::Config::Aggressive()     // Overfitting test / diagnostic
```

---

## 7. Rewards to Remove / Retire

| Reward | Action | Reason |
|---|---|---|
| `TimeToBalPossessionReward` | **Remove** | Fully superseded by TTICache |
| `MultiAgentPossessionReward` | **Remove** | TTIPossessionControlReward covers team TTI |
| `OffensivePossessionPositionReward` | **Keep** | Adds goal-side context; no overlap |
| `CutOutplayReward` (fake detection) | **Keep** | Ball-touch-based; complements TTI deltas |

---

## 8. W&B Metrics to Add

Log these from a `StepCallback` or custom metric collector to track Dubins vs Euclidean comparison over training:

```cpp
// Suggested W&B metric names:
"tti/self_dubins"         // snap.selfTTI
"tti/self_euclidean"      // snap.selfTTI_euclid
"tti/dubins_gain"         // snap.selfTTI_euclid - snap.selfTTI
"tti/best_opp"            // snap.bestOppTTI
"tti/possession"          // snap.possession
"tti/team_possession"     // snap.teamPossession
"tti/d_self"              // snap.dSelfTTI
"tti/d_opp"               // snap.dBestOppTTI
```

A positive `tti/dubins_gain` means the bot is taking arc paths that the Euclidean estimate undervalued — confirms the Dubins solver is contributing.

---

## 9. Integration Checklist for Claude Code

- [ ] Copy `TTIUtility.h` to `GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/`
- [ ] Create `TTIUtility.cpp` with thread-local definitions (content in Section 2b above)
- [ ] Add `TTIUtility.cpp` to `CMakeLists.txt` source list
- [ ] Add `#include <RLGymCPP/Rewards/TTIUtility.h>` to `RewardConfig.cpp`
- [ ] Remove `TimeToBalPossessionReward` entry from `GetPhasedRewards()`
- [ ] Remove `MultiAgentPossessionReward` entry from `GetPhasedRewards()`
- [ ] Add `TTIPossessionControlReward` with `Config::Conservative()` at weight 1.2f
- [ ] Build and verify: zero `static inline` map accesses in `TTICache` (should all be `thread_local`)
- [ ] Confirm `TTICache::Reset()` fires on episode reset (via `TTIPossessionControlReward::Reset()`)
- [ ] Add W&B metric logging for `tti/*` keys (Section 8)

---

## 10. File Tree After Integration

```
GigaLearnCPP/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/
├── TTIUtility.h           ← NEW: Full TTI utility + reward
├── TTIUtility.cpp         ← NEW: thread_local definitions
├── PossessionRewards.h    ← MODIFIED: remove TimeToBalPossession, MultiAgent
├── RewardConfig.cpp       ← MODIFIED: add TTIPossessionControlReward
└── CMakeLists.txt         ← MODIFIED: add TTIUtility.cpp to sources
```
