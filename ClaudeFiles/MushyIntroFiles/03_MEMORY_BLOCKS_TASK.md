# Memory Blocks A+B — Implementation Task
**Step-by-step guide for Claude Code**

---

## Before You Start

1. Read `MechanicSpec_TendencyMemoryBlocks_AB.md` fully — it contains the complete feature semantics, all code templates, and the integration checklist. This task file gives you the *order of operations*; the spec gives you the code.

2. Answer all Priority 1 questions in `05_QUESTIONS_FOR_NATHAN.md` with Nathan first.

3. Open and read every file in the "Files to Read" section of `04_CODEBASE_NAVIGATION.md` before writing anything.

---

## Overview of Changes

Exactly 4 files change. Nothing else.

| File | Type of change |
|---|---|
| `AdvancedObsPadded.h` (or new bot equivalent) | Add structs, member variables, method declarations |
| `AdvancedObsPadded.cpp` (or new bot equivalent) | Add method implementations, modify `Reset()` and `BuildObs()` |
| `ExampleMain.cpp` (or new bot entrypoint) | Add `SetMemoryBlocksEnabled(...)` call on obs builder construction |
| `RewardConfig.h` or `ModelConfig.h` | No change needed — phase check happens at obs builder construction |

No reward files change. No network config changes. No CMakeLists changes (no new source files — methods are added to existing files).

---

## Step 1 — Add Includes to Header

In `AdvancedObsPadded.h`, add `#include <deque>` if not already present. Verify it compiles with just this change before proceeding.

---

## Step 2 — Add Structs to Header (Private Section)

Add the `TeamTendency` and `SelfSummary` structs to the private section of the `AdvancedObsPadded` class. See `MechanicSpec_TendencyMemoryBlocks_AB.md` Section 12 for the exact struct definitions.

Key details:
- `TeamTendency.lastTouchCarId` is initialized to -1 (no touch yet seen)
- `SelfSummary.myLastTouchTime` and `.teamLastTouchTime` are initialized to -1000.0f (far in the past, so the normalized "time since touch" feature starts at 1.0 = "long time ago")
- All EMA fields have sensible priors: commitment/boost/possession at 0.5 (uncertain), aerial/pressure at 0.0 (assume grounded, no pressure until observed)

---

## Step 3 — Add Member Variables to Header (Private Section)

```cpp
TeamTendency oppTendency;
SelfSummary  selfSummary;
float lastUpdateTime  = -1.0f;
bool  memoryBlocksEnabled = false;
```

And all constants (ALPHA_FAST, ALPHA_MED, etc.). See Spec Section 12.

---

## Step 4 — Add Public Method Declaration

```cpp
public:
    void SetMemoryBlocksEnabled(bool enabled) { memoryBlocksEnabled = enabled; }
```

---

## Step 5 — Add Private Method Declarations

```cpp
private:
    void UpdateMemoryTrackers(const Player& perspective, const GameState& state);
    void TrimHistoryScalar(std::deque<std::pair<float, float>>& q, float now, float window);
    void TrimHistoryVec(std::deque<std::pair<float, Vec>>& q, float now, float window);
    void AddMemoryBlocksAToObs(FList& obs, const GameState& state) const;
    void AddMemoryBlocksBToObs(FList& obs, const GameState& state) const;
```

**Compile check:** The header should compile cleanly at this point before proceeding to the .cpp additions.

---

## Step 6 — Modify `Reset()` in .cpp

Add to the END of the existing `Reset()` method body:

```cpp
// Memory blocks reset
oppTendency    = TeamTendency();
selfSummary    = SelfSummary();
lastUpdateTime = -1.0f;
```

This ensures all tracker state is cleared on episode boundary.

---

## Step 7 — Implement the Four New Methods in .cpp

Implement the following methods in this order (each one is independently testable):

1. `TrimHistoryScalar` — trivial deque trim
2. `TrimHistoryVec` — same for Vec deque
3. `AddMemoryBlocksAToObs` — reads oppTendency, emits 8 floats
4. `AddMemoryBlocksBToObs` — reads selfSummary, computes derived values, emits 8 floats
5. `UpdateMemoryTrackers` — the main update logic, most complex

Implement in the above order so you can test emitters (3, 4) before the update logic (5) is wired in.

Full implementations are in `MechanicSpec_TendencyMemoryBlocks_AB.md` Section 13.

### Critical implementation note for `UpdateMemoryTrackers` — touch detection

The A7 challenge rate and B1/B2/B3 features all depend on detecting when a ball touch occurred. The implementation in the spec uses:

```cpp
int lastToucherCarId = state.ball.lastTouch.carId;  // <-- VERIFY API
```

**This field name must be confirmed with Nathan (Priority 1 question #1).** If the RLGymCPP API is different, adjust accordingly. The logic is: if the last toucher carId changed from the previous frame, a touch occurred this step.

### Critical implementation note — the `inv` flag for Block A

`depthMeanEMA` (A3) is a signed Y-position feature. When the perspective player is on the orange team, Y must be flipped:

```cpp
bool inv = (perspective.team == Team::ORANGE);
// ...
float yPersp = inv ? -nearest->pos.y : nearest->pos.y;
float depthObs = RS_CLAMP(yPersp / CommonValues::BACK_WALL_Y, -1.0f, 1.0f);
```

The `inv` variable is already computed at the start of `BuildObs()`. Either pass it into `UpdateMemoryTrackers` as a parameter, or recompute it from `perspective.team` inside the method. Do not use a stale `inv` from a different scope.

---

## Step 8 — Modify `BuildObs()` in .cpp

Two additions, in order:

### Addition 1 — Near the TOP of `BuildObs()`, after `inv` is determined:

```cpp
// Update memory trackers BEFORE building obs so reads are current-frame.
// Runs regardless of memoryBlocksEnabled so trackers stay warm across
// the phase boundary. The enabled flag only controls obs emission.
UpdateMemoryTrackers(player, state);
```

### Addition 2 — At the VERY END of `BuildObs()`, after all existing features:

```cpp
// ============================================================
// MEMORY BLOCKS A+B (16 features)
// Phase-gated: zero-filled in Phases 1–4, active Phase 5+
// DO NOT change the slot count (must be 16 total, always)
// ============================================================
if (memoryBlocksEnabled) {
    AddMemoryBlocksAToObs(obs, state);  // 8 features
    AddMemoryBlocksBToObs(obs, state);  // 8 features
} else {
    for (int i = 0; i < 16; i++) obs += 0.0f;
}
```

**Verify that this code adds exactly 16 floats.** Count manually in `AddMemoryBlocksAToObs` (should be exactly 8 `obs +=` statements) and `AddMemoryBlocksBToObs` (exactly 8). Any discrepancy will silently corrupt all downstream obs indexing.

---

## Step 9 — Wire Activation in ExampleMain.cpp

Find where the obs builder is constructed (ask Nathan for exact location if unclear). Add:

```cpp
bool memoryActive = (CURRENT_PHASE >= TrainingPhase::PHASE_5_DIAMOND);
obsBuilder->SetMemoryBlocksEnabled(memoryActive);
```

Or inline equivalent. `CURRENT_PHASE` is defined in `RewardConfig.h`.

---

## Step 10 — Obs Size Verification

If the codebase has any hardcoded obs size constant, update it to reflect the +16. Search for:
- Any integer constant that equals the old obs size (~261)
- Any assert or validation on obs vector size
- Any consumer of the obs builder that allocates buffers based on obs size

If Nathan confirms no hardcoded size, skip. Ask if unsure (Priority 2 question in `05_QUESTIONS_FOR_NATHAN.md`).

---

## Step 11 — Smoke Test Build

Build and run a minimal test:
1. Construct one obs builder instance
2. Construct a mock GameState (or use an existing test fixture)
3. Call `BuildObs()` 300 times in a loop (simulating ~10 seconds at 30 steps/sec)
4. On each call, print all 16 memory feature values to console
5. After 300 calls, verify:
   - No NaN or Inf in any feature
   - `oppTendency.sampleCount` = 300 and `confidence` feature = 1.0
   - `commitmentEMA` has moved from its prior (not still exactly 0.0 or exactly 0.5)
   - `boostHistory` deque has ~30 entries (1 second's worth at 30 steps/sec)
   - Block A features sum is nonzero
   - Block B features sum is nonzero
6. Call `Reset()` and verify all features return to their initial values

Show Nathan this output before marking the session complete.

---

## Step 12 — Review Integration Checklist

Walk through the checklist in `MechanicSpec_TendencyMemoryBlocks_AB.md` Section 16 with Nathan. Each item should be verifiable from the code or the smoke test output.

---

## Common Mistakes to Avoid

**Wrong obs size:** Adding or removing even one `obs +=` statement in the new methods will silently corrupt all downstream obs indexing. Count every `obs +=` twice.

**Forgetting `inv`:** The `depthMeanEMA` feature will be inverted for orange players in obs. Everything else in Block A and B is naturally symmetric (fractions, magnitudes) and doesn't need inversion. Double-check if any other feature you add is asymmetric.

**Stale history deques on reset:** The `SelfSummary.boostHistory`, `positionHistory`, `ballZHistory`, `ballSpeedHistory` deques must all be cleared in `Reset()`. Since `selfSummary = SelfSummary()` does a default-constructor reset, this is handled automatically — but verify the default constructor initializes empty deques (it does, by default).

**Double-update if multiple players share obs builder:** If the obs builder is shared across players (i.e., `BuildObs` is called for multiple players on the same obs builder instance), `UpdateMemoryTrackers` will be called multiple times per step with different perspective players. This should not happen — obs builders are per-player. Confirm with Nathan.

**EMA alpha not adjusted for tickSkip:** The alpha values in the spec are already calculated for tickSkip=4 (30 steps/sec). Do not recalculate them unless Nathan changes tickSkip.

**Tracker updating when not needed:** `UpdateMemoryTrackers` runs regardless of `memoryBlocksEnabled` by design (keeps trackers warm across phase boundary). This is correct — do not move it inside the `if (memoryBlocksEnabled)` block.
