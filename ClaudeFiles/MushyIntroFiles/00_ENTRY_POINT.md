# Claude Code Session — Entry Point
**Project:** GigaLearnBot v2 (new bot, larger architecture)  
**Session Author:** NJH  
**Session Date:** 2026-05-18  
**Task:** Implement Tendency Memory Blocks A+B into the new bot's obs builder

---

## Read These Files First, In Order

| # | File | Purpose | Est. read time |
|---|---|---|---|
| 1 | `00_ENTRY_POINT.md` ← you are here | Session overview, rules, questions | 5 min |
| 2 | `01_PROJECT_CONTEXT.md` | Training philosophy, architecture, what not to break | 8 min |
| 3 | `02_NEW_BOT_SPECIFICATIONS.md` | New bot specs vs. old bot | 5 min |
| 4 | `04_CODEBASE_NAVIGATION.md` | Which files to read and touch | 5 min |
| 5 | `05_QUESTIONS_FOR_NATHAN.md` | **Ask all Priority 1 questions before writing any code** | — |
| 6 | `03_MEMORY_BLOCKS_TASK.md` | Step-by-step implementation guide | 10 min |
| 7 | `MechanicSpec_TendencyMemoryBlocks_AB.md` | Full mechanic spec with code templates | 15 min |

Do not write any code until:
- All five context files have been read
- All Priority 1 questions in `05_QUESTIONS_FOR_NATHAN.md` have been answered by Nathan
- You have opened and skimmed every file listed in the "Files to touch" section of `04_CODEBASE_NAVIGATION.md`

---

## What This Session Is Building

A **16-feature hand-crafted memory block** embedded directly into the obs builder. It consists of:

- **Block A (8 features):** Rolling opponent-team behavioral tendencies — commitment level, aerial frequency, field depth, boost economy, speed, pressure, challenge rate, confidence ramp.
- **Block B (8 features):** Self and own-team summary state — time since last touch, possession fraction, boost slope, position variance, ball Z history, ball speed, team spread.

These features give the policy direct access to temporal context that the single-frame MDP observation cannot express. The network no longer has to re-derive "what just happened" from raw position/velocity alone.

The implementation is a **self-contained addition** to the obs builder. No rewards are changed. No network architecture changes. No reward weights change. Obs vector shape grows by exactly 16 floats.

---

## Critical Constraints — Never Violate These

These are non-negotiable properties of the codebase:

1. **Obs vector shape is invariant across ALL phases (1–9).** The 16 new slots are always present. They are zero-filled in Phases 1–4, populated in Phase 5+. This is enforced by the `memoryBlocksEnabled` flag pattern. Do NOT conditionally omit slots.

2. **Thread isolation via `thread_local`, never `static inline` maps.** The training run uses 310 parallel environments. Any per-environment state — including the tendency trackers — must use the `thread_local` storage pattern already established in the codebase. See `AdvancedOvercommitmentPenalty.h` for the reference pattern if needed.

3. **Inversion (`inv`) must be applied to all positional features.** The `depthMeanEMA` in Block A is a signed field-depth value. When building obs for an orange-team player, positions must be inverted to maintain the standard "positive Y = toward opponent goal" convention. See `AdvancedObsPadded.cpp` for how this is done throughout.

4. **Trackers must reset on episode boundary.** The `Reset(initialState)` method must clear all tracker state. Detection of rollback (elapsedTime going backwards) should also trigger a reset.

5. **Obs builders are per-player instances.** Each player gets their own obs builder call. The opponent team is always "whoever the perspective player is NOT on." The tracker is keyed per-perspective, not per-env.

6. **No reward may reference tendency tracker state.** These features are passive observations only. If a future reward proposal wants to consume tendency data, that requires a new design review — do not wire it in as part of this task.

7. **Touch detection must use the correct API.** The exact RLGymCPP field for last ball toucher is one of the Priority 1 questions. Do not guess. See `05_QUESTIONS_FOR_NATHAN.md`.

---

## Definition of Done for This Session

- [ ] `AdvancedObsPadded.h` updated with new structs, members, method declarations
- [ ] `AdvancedObsPadded.cpp` updated with all method implementations
- [ ] `Reset()` updated to clear tracker state
- [ ] `BuildObs()` updated with `UpdateMemoryTrackers()` call and memory block emission at end
- [ ] Phase gating flag wired into obs builder construction in `ExampleMain.cpp` (or wherever Nathan specifies)
- [ ] Zero-fill confirmed — obs size increments by exactly 16, no more, no less
- [ ] Local smoke test: instantiate one env, call BuildObs() 100 times, print all 16 feature values, verify no NaN/Inf, verify features change sensibly
- [ ] All Priority 1 and Priority 2 questions from `05_QUESTIONS_FOR_NATHAN.md` answered and reflected in implementation
- [ ] Code compiles clean — zero warnings on the new additions
- [ ] Nathan has signed off on the smoke test output

---

## Things Explicitly Out of Scope For This Session

- Block C (game flow features) — deferred
- Library architecture (archetype matching, clustering, merging) — deferred post-competition
- Per-opponent tendency tracking (team-level only in this session)
- Teammate tendency tracking (team-level self only)
- W&B metric hooks (separate session or Nathan adds post-integration)
- Reward changes of any kind
- Network architecture changes

---

## Key Person

**Nathan (NJH)** — project owner. Questions go to Nathan. When in doubt about intent, ask rather than assume. Nathan's training run is in a delicate phase; do not suggest or make changes outside the explicit task scope without surfacing it first.
