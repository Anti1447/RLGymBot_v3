# New Bot Specifications
**GigaLearnBot v2 — Architecture & Configuration**

---

## Core Specs vs Old Bot

| Parameter | Old Bot (v1) | New Bot (v2) | Impact on this session |
|---|---|---|---|
| Obs baseline size | ~250 features | **261 features** | Session adds 16 → 277 total |
| Shared head | (512, 512) | **(1024, 1024)** | More capacity to use memory features |
| Policy head | (512, 512) | **(1024, 1024)** | Same |
| Critic head | (512, 512) | **(512, 512)** (TBC with Nathan) | Verify |
| Action bins — steer | 3 | **5** | No impact on obs |
| Action bins — air roll | 3 | **5** | No impact on obs |
| tickSkip | 8 | **4** | EMA alpha values change — see below |
| Decisions/sec | ~15 | **~30** | All time-window features must account for this |
| Training phase (current) | Phase 5 Diamond | **Phase 1 Bronze (fresh start)** | Memory blocks will be zero until Phase 5 |
| Training machine | Machine 1 | **Machine 2** | Isolation environment |

---

## tickSkip=4 — Why It Matters for Memory Features

Every time-window and EMA feature in the tendency blocks is spec'd in **seconds**, not steps, precisely because of this difference.

At tickSkip=4:
- 1 tick = 1/120 sec
- 4 ticks = 1 step = 1/30 sec ≈ **33ms per decision**
- 30 decisions per second

Alpha values for EMAs are derived from target time windows:
```
Effective window ≈ 3 / α steps

10s window → 300 steps → α = 0.010
15s window → 450 steps → α = 0.007
30s window → 900 steps → α = 0.003
3s window  → 90  steps → α = 0.030
```

If you ever see a time window specified in steps from old-bot code, **do not port it directly** to the new bot. Recalculate from seconds.

The `confidence` ramp in Block A uses `sampleCount / 300` to represent ~10 seconds of samples (300 samples × 1/30 sec = 10 seconds). This is correct for tickSkip=4.

---

## Obs Builder for New Bot

The new bot uses a **new, larger obs builder** that produces 261 features at baseline. The obs builder file for the new bot is likely a new file or a renamed/modified version of `AdvancedObsPadded`. The exact filename must be confirmed with Nathan (Priority 1 question in `05_QUESTIONS_FOR_NATHAN.md`).

The session's goal is to add 16 features to this obs builder, bringing the new bot's baseline from 261 to 277. With a soft budget of <500 total features, this leaves ~223 slots for future additions.

### Obs Block Structure (new bot, post-session)

| Block | Features | Range | Status |
|---|---|---|---|
| Existing obs builder content | 261 | Various | Unchanged — do not modify |
| Block A: Opponent team tendency | 8 | [0,1] or [-1,1] | **This session** |
| Block B: Self/team summary | 8 | [0,1] or [-1,1] | **This session** |
| **Total** | **277** | — | Post-session target |

---

## Network Configuration (new bot)

From `ModelConfig.cpp` pattern — new bot values:

```cpp
cfg.ppo.sharedHead.layerSizes = { 1024, 1024 };
cfg.ppo.policy.layerSizes     = { 1024, 1024 };
cfg.ppo.critic.layerSizes     = { 512, 512 };   // TBC — verify with Nathan
cfg.ppo.networkArch = NetworkArchitecture::MLP;  // Same as old bot
```

Activation: GELU (same as old bot)  
LayerNorm: enabled (same as old bot)  
Optimizer: AdamW (same as old bot)

---

## Action Space (new bot)

5-bin actions for steering directions and air roll. This changes the action space distribution but has no impact on the obs builder code. Note for completeness only.

---

## Phase Structure (new bot)

Same phase enum as old bot (defined in `RewardConfig.h`). New bot starts at `PHASE_1_BRONZE`.

The memory blocks must be present but zero-filled for Phases 1–4. Activation at `PHASE_5_DIAMOND` via a boolean flag on the obs builder. This means the new bot won't use active tendency features for approximately 40 billion training steps — by design.

**Do not change the phase gating threshold** without Nathan's explicit instruction. Phase 5 activation is the correct decision; earlier activation would expose the policy to noisy tendency features from an immature self-play distribution.

---

## Reward Structure (new bot)

The new bot has a "similar reward structure" to the old bot. Exact reward config for the new bot has not been fully spec'd yet in this conversation — that is separate work. Do not modify rewards in this session.

Key principle carried over: **gradient dilution from 40+ simultaneous reward signals is the central bottleneck**. Any reward structure for the new bot must be designed with this in mind. Out of scope for this session.

---

## What Is NOT Carried Over From Old Bot

The following old-bot features are NOT confirmed for the new bot and should not be assumed:
- Specialist heads routing (old bot has Phase 5+ specialist heads for kickoff, aerial, mechanical, general sub-policies) — new bot status unknown, ask Nathan
- Outcome-based value head — new bot status unknown
- `AirDribbleBumpConversionReward` Shadow Mode — old bot only, new bot reward spec TBD
- Specific reward weights — new bot reward config is separate work

---

## Files That Exist for Old Bot But May Need New Versions for New Bot

These files exist in the project for the old bot. Whether the new bot has identical, modified, or entirely new versions should be confirmed:

| Old bot file | New bot status |
|---|---|
| `AdvancedObsPadded.h/.cpp` | Likely modified or new — **confirm filename** |
| `RewardConfig.h/.cpp` | Likely new reward config — **do not modify old bot's** |
| `ModelConfig.h/.cpp` | New bot needs its own config — **do not modify old bot's** |
| `ExampleMain.cpp` | New bot entrypoint — **confirm whether separate file** |
| `GameModeConfig.h/.cpp` | Likely similar — confirm |
