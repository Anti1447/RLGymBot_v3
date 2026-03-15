# TTI Integration — Session Findings
**Date:** 2026-03-03
**Status: REVERTED** — Integration caused 2v2/3v3 rating drop and entropy spike. See below.

---

## What Was Attempted

`TTIUtility.h` (from this folder) was integrated as a drop-in replacement for `TimeToBalPossessionReward` in Phases 5, 6, and 7. The goal was to upgrade the possession signal from a crude Euclidean time-to-reach estimate to a physics-accurate **Dubins path** solver that properly accounts for turn radius, vertical reach (jump/double-jump/fast-aerial), and thread-local caching across 310 parallel games.

**Files that would need to be created:**
- `GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/TTIUtility.h` — the main utility + reward class
- `GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/TTIUtility.cpp` — thread_local static definitions for the RLGymCPP.lib side
- `src/TTIUtility.cpp` — thread_local static definitions for the GigaLearnBot.exe side (required due to DLL/EXE split — see Issue 1 below)

**Files that would need to be modified:**
- `src/RewardConfig.h` — add `#include <RLGymCPP/Rewards/TTIUtility.h>`
- `src/RewardConfig.cpp` — replace 3 entries (Phases 5, 6, 7)

---

## Issues Encountered

### Issue 1 — Dual .cpp Required (Build Architecture)

The project splits into `GigaLearnCPP.dll` and `GigaLearnBot.exe`. `RewardConfig.cpp` compiles directly into the EXE, not the DLL. The `TTICache` class uses `static thread_local` members (not `static inline`) which require out-of-line definitions. These definitions must exist in the same binary that calls them.

**Fix:** Two separate `TTIUtility.cpp` files are needed:
- One in `GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/` (compiled into RLGymCPP.lib → GigaLearnCPP.dll)
- One in `src/` (compiled into GigaLearnBot.exe alongside RewardConfig.cpp)

Both can be identical. The `thread_local` semantics guarantee each binary has its own independent instance — no data sharing conflict.

**Note on CMake glob:** After adding new .cpp files, CMake must be re-run before building (`cmake -G "Visual Studio 17 2022" -A x64 -B build`), because `file(GLOB_RECURSE)` is evaluated at configure time, not build time.

---

### Issue 2 — Missing ZeroSumReward Wrapper (2v2/3v3 Rating Drop)

The original `TimeToBalPossessionReward` was always wrapped: `ZeroSumReward(new TimeToBalPossessionReward(), 1.0f)`.
The `TTIPossessionControlReward` was initially instantiated **without** this wrapper, which broke two critical properties in team modes:

**Property 1 — Zero-sum competitive balance.**
With `ZeroSumReward(teamSpirit=1.0)`, all players' rewards sum to exactly zero every step. Without it, the sum is non-zero and biased, corrupting the value function in 2v2/3v3.

Concrete example with BLU tti=[0.5, 1.2], ORA tti=[1.8, 2.5]:

| | BLU-0 | BLU-1 | ORA-2 | ORA-3 | Sum |
|---|---|---|---|---|---|
| With ZeroSum | +1.30 | +1.30 | −1.30 | −1.30 | **0.0 ✓** |
| Without ZeroSum | +1.17 | +0.82 | −1.17 | −1.52 | **−0.70 ✗** |

**Property 2 — Team-level credit assignment.**
`ZeroSumReward(teamSpirit=1.0)` forces both teammates to receive the exact same averaged reward, teaching coordinated play. Without it, the faster player gets individual `wPossess` credit, the slower player gets a confusing mixed signal, and rotation/support discipline breaks down.

**Fix:** Always wrap in `ZeroSumReward(new TTIPossessionControlReward(...), 1.0f)` — same as the old reward.

---

### Issue 3 — Entropy Spike (All Modes)

Even with `ZeroSumReward` added back, entropy climbed. Two compounding causes:

**Cause A — Reward ~48% too large per step.**
`Config::Balanced()` has 6 sub-terms that can all fire simultaneously (raw max 3.2/step). After ZeroSum amplification and weight 1.5f, max per-player ≈ 4.8. The old `TimeToBalPossessionReward` had a continuous raw max of ~1.3, giving a max of ~3.25 after ZeroSum and weight 2.5f.

Additionally, `wTeam=0.8` is effectively doubled by ZeroSum because both teammates share the same `teamPossession` value and both opponents share the mirror value. `wTeam=0.8` behaves like **1.6** in the final gradient.

**Cause B — Two sub-terms are frame-to-frame deltas.**
`wClosing` uses `dSelfTTI` and `wOppDiverge` uses `dBestOppTTI` — differences between consecutive `TTICache` snapshots. At 8-tick skip, the Dubins solver can switch aerial branches between steps (e.g. `SINGLE_JUMP → FAST_AERIAL`) causing discrete TTI jumps. These are uncorrelated with the bot's actions. The policy can't learn a consistent action → reward mapping and hedges toward uniform distributions → entropy rises.

**The `Stable()` preset** (added to TTIUtility.h before revert) was designed to address this:
```
wPossess = 1.0   — core Dubins TTI signal, kept
wTeam    = 0.0   — ZeroSum handles team credit
wDuration= 0.2   — smooth, predictable (old reward also had duration bonus)
wDubinsAccuracy = 0.0  — disabled
wClosing        = 0.0  — disabled: delta-based, noisy
wOppDiverge     = 0.0  — disabled: delta + history-gated, noisy
```
With weight 2.0f this produces a max per-player ≈ 4.8, matching the old reward's magnitude.

**This was NOT tested** — the integration was fully reverted before the entropy fix could be validated.

---

## Summary Table — What Works vs What Needs Work

| Aspect | Status | Notes |
|---|---|---|
| Dubins path TTI (physics quality) | ✅ Correct | Better than Euclidean estimate; handles turn radius and vertical reach |
| Thread-local caching (per-frame) | ✅ Correct | O(n) once per tick per thread, O(1) on cache hit |
| Dual .cpp for DLL/EXE split | ✅ Solved | Two identical .cpp files, one per binary |
| CMake glob re-run after new files | ✅ Known | Must run `cmake -G ...` not just `cmake --build` |
| ZeroSumReward wrapper | ✅ Required | Must wrap, same as old reward (teamSpirit=1.0) |
| Delta sub-terms (wClosing, wOppDiverge) | ❌ Too noisy | Discrete TTI jumps on branch transitions break PPO |
| wTeam in ZeroSum context | ❌ Double-counted | Effectively 2× after ZeroSum subtraction, inflates magnitude |
| Config::Balanced() magnitude | ❌ ~48% too large | All 6 terms firing simultaneously exceeds old reward magnitude |
| Config::Stable() (proposed fix) | ⚠️ Untested | Designed to fix entropy but not validated |

---

## Recommended Integration Path (Next Attempt)

1. Use `Config::Stable()` preset from day one
2. Wrap in `ZeroSumReward(..., 1.0f)` — no exceptions
3. Weight 2.0f for Phases 6/7, 1.8f for Phase 5
4. Run 100M steps and confirm entropy is stable and 2v2/3v3 ratings hold
5. Only then consider enabling `wDuration` (it's already in `Stable()`)
6. The delta terms (`wClosing`, `wOppDiverge`) should only be added once training is solid — and may need smoothing (e.g. exponential moving average of TTI rather than raw frame-to-frame diff) to be viable

### Phase-by-phase replacement guide:

| Phase | Old entry | Replacement |
|---|---|---|
| 5 Diamond (~line 518) | `ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 3.0f` | `ZeroSumReward(new TTIPossessionControlReward(Config::Stable()), 1.0f), 1.8f` |
| 6 Champion (~line 719) | `ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f` | `ZeroSumReward(new TTIPossessionControlReward(Config::Stable()), 1.0f), 2.0f` |
| 7 GC (~line 876) | `ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f` | `ZeroSumReward(new TTIPossessionControlReward(Config::Stable()), 1.0f), 2.0f` |

Phases 2, 3, 4 should keep `TimeToBalPossessionReward` — TTI is over-engineered for early phases.
