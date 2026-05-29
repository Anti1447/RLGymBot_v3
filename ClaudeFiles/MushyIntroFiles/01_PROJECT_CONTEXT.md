# Project Context
**GigaLearnBot v2 — Training Architecture, Philosophy & Constraints**

---

## Project Summary

Nathan is building a competitive Rocket League bot trained via PPO reinforcement learning, targeting Grand Champion+ play. This document covers everything about the training architecture, philosophy, and established patterns that Claude Code needs to understand before touching any files.

There are **two bots in active development:**

| | Old Bot (GigaLearnBot v1) | New Bot (GigaLearnBot v2) |
|---|---|---|
| Status | Phase 5 Diamond training, in-flight, do not disrupt | Fresh start — this session's target |
| Obs size | ~250 features (AdvancedObsPadded) | 261 baseline (new larger obs builder) |
| Network | Shared (512,512), Policy (512,512) | Shared (1024,1024), Policy (1024,1024) |
| Action bins | 3 bins (steering, air roll) | 5 bins (steering, air roll each direction) |
| tickSkip | 8 | 4 |
| Decisions/sec | ~15 | ~30 |
| Phase | 5 Diamond | Starting Phase 1 |

The old bot's training run on Machine 1 **must not be disrupted**. This session targets the new bot. The new bot is on Machine 2 (isolation/experimentation machine) initially.

---

## Two-Machine Workflow

**Machine 1:** Primary training of old bot. Do not touch unless Nathan explicitly says so.  
**Machine 2:** Isolation tests, new features, reward experimentation, new bot development. This session's work targets Machine 2.

---

## Core Training Architecture

**Algorithm:** Proximal Policy Optimization (PPO) with LibTorch (C++ PyTorch). No Python.

**Stack:**
- `RLGymCPP` — environment (game state, physics, obs, rewards)
- `GigaLearnCPP` — PPO training engine
- `RocketSim` — fast Rocket League physics simulation
- `RLBotCPP` — live deployment
- `Weights & Biases (W&B)` — primary observability layer
- `RocketSimVis` — behavioral validation (visual confirmation)

**Training volume:** ~310 parallel environments, ~150k steps/second throughput.

**Project path:** `C:\Projects\RLGymBot_v3\GigaLearnBot\GigaLearnCPP\`

---

## Phase System

Training is phase-gated by `CURRENT_PHASE` in `RewardConfig.cpp`. Phases correspond to skill tiers:

```
PHASE_1_BRONZE    — 0 to 200M steps
PHASE_2_SILVER    — 200M to 300M steps
PHASE_3_GOLD      — 300M to 5B steps
PHASE_4_PLATINUM  — 5B to 40B steps
PHASE_5_DIAMOND   — 40B to 80B steps  ← Memory blocks ACTIVATE here
PHASE_6_CHAMPION  — 80B to 120B steps
PHASE_7_GC        — 120B to 160B steps
PHASE_8_SSL       — 160B to 200B steps
PHASE_9_PRO       — 200B+ steps
```

Phase gating is the standard mechanism for introducing new features at the right skill tier. The memory blocks for this session use this same pattern: slots present but zero-filled in Phases 1–4, activated at Phase 5.

---

## The Central Bottleneck: Gradient Dilution

**The primary identified bottleneck is gradient dilution from 40+ simultaneous reward signals.**

Every design decision should be evaluated against this: does it add more gradient signals that will conflict, or does it compress multiple concerns into fewer, cleaner signals? The tendency memory features are designed to be a *compressed summary* that reduces the policy's burden — they should not require many new reward signals to be useful.

---

## Established Codebase Patterns

These patterns are already in use throughout the codebase. New code must follow them.

### 1. Per-environment isolation — `thread_local`

With 310 concurrent environments, any per-environment state must use `thread_local` storage.

```cpp
// CORRECT — per-thread, per-env isolation
thread_local std::unordered_map<uint32_t, SomeThing> myMap;

// WRONG — shared across all environments
static inline std::unordered_map<uint32_t, SomeThing> myMap;
```

The tendency tracker state (`oppTendency`, `selfSummary`) lives as member variables on the obs builder instance, which is itself created per-env. This is equally correct. The `thread_local` pattern matters most when state is in reward classes that may be referenced globally.

### 2. Inversion (`inv`) for orange-team perspective

The orange team's perspective requires all positional and directional features to be inverted so the network always sees the world as "I'm playing toward positive Y." Every existing feature in `AdvancedObsPadded.cpp` follows this. The new `depthMeanEMA` feature in Block A is a signed position feature and **must be inverted** for orange observers.

```cpp
bool inv = player.team == Team::ORANGE;
// ...
float yPersp = inv ? -pos.y : pos.y;
```

### 3. Feature normalization

All obs features should be in approximately [-1, 1] or [0, 1]. This ensures consistent gradient scaling into the network. Every Block A and B feature has a specified output range in the mechanic spec; ensure normalizations match.

### 4. State mutation is forbidden in obs builders

`BuildObs()` and `UpdateMemoryTrackers()` must read `state` and `player` but **never modify them**. All tracker state lives in the obs builder instance's member variables.

### 5. `prev` state pattern

Several existing rewards and obs use `state.prev` for frame-delta calculations. The tendency tracker uses `state.elapsedTime` for windowing instead. If you need frame-delta for any reason, `state.deltaTime` is the elapsed time since the previous step.

### 6. Obs size must be deterministic

The obs vector must be exactly the same length every single call, regardless of game state, team size, or phase. Conditional `+=` is forbidden. Size verification should be added if Nathan doesn't already have it.

---

## Key Design Philosophy Summary

Taken from Nathan's established design principles — relevant to this session:

**Anti-farming by design.** Reward and obs features should not be gameable by proxy behavior. The tendency features are passive observations; this property holds as long as no reward depends on them.

**Goodhart vigilance.** Features should measure what they mean. If a feature starts showing degenerate distributions (collapses to zero or one), it's being farmed or it's a bug. The W&B alert thresholds in the spec exist for this reason.

**Shadow mode before optimizer exposure.** New features that drive policy decisions should be observed (via W&B) before being promoted to active use. For obs features, "shadow mode" means: slots are zero in early phases, we watch distributions when they go live, and we confirm sensible behavior before training deepens.

**Drastic changes cause skill abandonment.** Adding 16 obs features at Phase 5 is designed to be a gentle introduction — the network has already learned to ignore those slots (zero-filled). The KL spike at activation is expected and bounded. Do not propose any changes that would reshape the network or resize the obs before Phase 5 with data.

**One variable per iteration.** This task is one atomic change: the tendency memory blocks. Do not bundle other fixes, improvements, or refactors unless Nathan explicitly approves them. If you notice something else that looks broken, flag it but don't fix it.

---

## W&B and RocketSimVis as Ground Truth

Nathan's bot has surpassed his own play level. He cannot personally evaluate improvements by watching the game and relying on his own judgment. The two authoritative feedback channels are:

1. **W&B metrics** — primary. Reward health, policy KL divergence, value loss, feature distributions.
2. **RocketSimVis behavioral confirmation** — secondary. Visual confirmation that the bot is doing the intended behavior.

When you complete the implementation, Nathan will run a smoke test with these tools. Your code should make instrumentation easy: print-logging feature values during the smoke test is explicitly expected (not just compile-and-hope).

---

## Competition Timeline

Nathan wants this bot ready for **fall competition** (~3–5 months from now, May 2026). This constrains the scope of this session and the broader project. Features are sequenced to deliver the highest-value items first. The library architecture (archetype clustering, merging) is explicitly deferred past the competition. Keep timelines in mind if Nathan asks for scope estimates.
