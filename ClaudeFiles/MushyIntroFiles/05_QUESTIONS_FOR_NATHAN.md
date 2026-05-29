# Questions for Nathan
**Ask before writing code — prioritized**

---

## Priority 1 — Must Answer Before Writing Any Code

These block the implementation. Do not proceed past Step 5 in `03_MEMORY_BLOCKS_TASK.md` until all of these are answered.

---

**Q1. What is the exact filename/class name for the new bot's obs builder?**

The old bot uses `AdvancedObsPadded.h / AdvancedObsPadded.cpp`. The new bot has a 261-feature obs builder that is different. Is this:

a) A new file with a new name (e.g., `AdvancedObsV2.h`)?  
b) The same `AdvancedObsPadded.h` file but with additions already made?  
c) A completely separate project directory for the new bot?

*Why it matters:* I need to modify the correct file. Modifying the old bot's obs builder would corrupt Machine 1's training run.

---

**Q2. Where exactly is the obs builder instantiated, and is it shared across players?**

In `ExampleMain.cpp`, I need to find the line where the obs builder is created and call `SetMemoryBlocksEnabled()` on it. Is it:

a) Created once and shared across all envs (one global instance)?  
b) Created per-env inside `EnvCreateFunc`?  
c) Created somewhere in `ConfigureLearnerConfig`?

Also: is `BuildObs()` called once per env step (for all players at once), or once per player per step? This affects whether `UpdateMemoryTrackers` risks double-updating.

*Why it matters:* Phase gating (`SetMemoryBlocksEnabled`) must be called on the right instance. If shared, one call sets all envs. If per-env, each instance must be configured.

---

**Q3. Does the new bot's project live in the same directory structure as the old bot, or a separate folder?**

Specifically: is the new bot in:
- The same `C:\Projects\RLGymBot_v3\GigaLearnBot\GigaLearnCPP\` folder as the old bot?
- A new folder alongside it (e.g., `GigaLearnBot_v2\`)?
- A separate branch on the same project?

*Why it matters:* Determines which files I should open and modify. Also matters for confirming that Machine 1's files are not at risk.

---

**Q4. Is there any hardcoded obs size constant anywhere in the codebase that needs updating?**

For example:
- A `constexpr int OBS_SIZE = 261;` declaration
- A `RG_ASSERT(obs.size() == EXPECTED_SIZE)` check
- A network input layer size that must match the obs builder output

Or is the obs size fully dynamic (derived from whatever `BuildObs()` produces)?

*Why it matters:* If there's a hardcoded size and we add 16 floats without updating it, the build may succeed but training will fail at runtime.

---

**Q5. Confirm: is `player.ballTouchedStep` accessible inside the obs builder `BuildObs()` method?**

From the rewards code, `player.ballTouchedStep` is confirmed as the correct per-player ball touch boolean. However, `BuildObs()` receives `const Player& player` and `const GameState& state`. Is `ballTouchedStep` populated on the `player` object at the time `BuildObs()` is called, or is it only valid during reward computation?

*Why it matters:* If `ballTouchedStep` is cleared before `BuildObs()` runs, touch detection in the obs builder will always see false. In that case, we'd need a different approach (e.g., cache touch events in a separate observer that runs before obs building).

---

## Priority 2 — Important but Can Proceed with Reasonable Defaults

These affect code quality but won't block the implementation. Answer these before completing Step 11 (smoke test).

---

**Q6. What is the new bot's exact baseline obs size — confirmed as 261, or is that approximate?**

After this session adds 16 features, the total will be 261 + 16 = 277. If the baseline is actually 258 or 263, the total is different. This should be verified by counting existing `obs +=` statements in the new obs builder, or Nathan confirming the number.

*Why it matters:* Documentation accuracy and any hardcoded size checks (see Q4).

---

**Q7. Does `state.elapsedTime` start at 0.0 at episode start and increase monotonically?**

The tendency tracker uses `state.elapsedTime` to detect episode resets (if `now < lastUpdateTime`, we reset). It also uses it as the game clock for time-window history queues.

Alternatively: is `state.deltaTime` the per-step elapsed time, and if so, should I accumulate my own clock variable?

*Why it matters:* If `elapsedTime` doesn't behave as assumed, the episode-reset detection and history trimming will be wrong.

---

**Q8. In the new bot's self-play setup, does `carId` remain stable throughout an episode when `trainAgainstOldVersions` is active?**

When an old-version checkpoint is slotted in as an opponent, do the players in those positions keep their existing `carId` values, or are they reassigned?

*Why it matters:* The `selfSummary.myLastTouchTime` tracking uses `player.carId` to identify "is this my touch." If carId changes mid-episode for any player, the tracker could misidentify touches.

---

**Q9. Does the new bot have any specialist head routing (e.g., kickoff head, aerial head)?**

The old bot has specialist heads activated at Phase 5+. The new bot's architecture is described as (1024,1024) MLP only. Confirm: is there any multi-head or routing logic in the new bot's policy network, or is it a pure trunk→policy single-path MLP?

*Why it matters:* If there are routing heads, there may be additional obs sections (routing obs) that affect the obs size count and the slot allocation for the memory blocks.

---

**Q10. Is there a `state.prev` available in `BuildObs()` for the new bot, and if so, is it used?**

The existing `AdvancedObsPadded` doesn't appear to use `state.prev` in the obs builder (it's more common in rewards). Confirming this means we don't need to worry about `state.prev` being null on the first frame.

*Why it matters:* Some of the Block B calculations (boost slope, position variance) use deque-based history as a substitute for `state.prev`. If `state.prev` is available and stable, there may be simpler implementations.

---

## Priority 3 — Design Decisions Nathan Should Weigh In On

These won't block coding but affect the design. Nathan should answer these before the session is marked "done."

---

**Q11. Should `UpdateMemoryTrackers()` run when `memoryBlocksEnabled = false` (Phases 1–4)?**

Current design: yes, trackers update every step regardless of phase. This keeps them warm across the phase boundary at Phase 5 entry, so the first active values are immediately meaningful rather than starting from cold priors.

Alternative: no, trackers stay fully dormant until Phase 5. Simpler code path, fractionally less compute in early phases.

Recommendation: yes (run always). But Nathan's call.

---

**Q12. Should the `depthMeanEMA` feature (A3) track the NEAREST opponent to the ball, or the AVERAGE of all opponents?**

Current design: nearest opponent's Y position — captures the most threatening depth signal.  
Alternative: average of all opponents' Y — smoother, less reactive to single fast movers.

In 1v1 these are identical. In 3v3 they can differ significantly.

Recommendation: nearest (current design). But worth confirming.

---

**Q13. Should Block B include ANY teammate features, or strictly self-only?**

Current design: Block B is self-only with one team-aggregate feature (`teamAvgDistToBallEMA`). No individual teammate tracking.

Alternative: add "time since nearest teammate's last touch" as a second touch-time feature. Adds 1 feature, gives the policy a sense of whether a teammate is actively engaged.

Recommendation: keep as-is for now, can be added in a future session. But flag for Nathan.

---

**Q14. Preferred smoke test method: printf-debug in a unit test, or log to a file, or RocketSimVis?**

The implementation guide (Step 11) calls for printing 16 feature values to console over 300 steps. Nathan may prefer:
a) Printf to console in a minimal test env
b) W&B logging during a short 1-env training run
c) RocketSimVis with feature overlay

Whichever Nathan prefers, the smoke test must confirm no NaN/Inf and sensible feature trajectories.

---

## Questions Answered During Session Preparation

These were originally unknown but were resolved by reading the codebase. Claude Code does not need to ask these.

| Question | Answer | Source |
|---|---|---|
| What is the ball touch detection API? | `player.ballTouchedStep` (bool, per-player) — NOT `state.ball.lastTouch.carId` (this field does not exist) | `AdvancedRewards.h:1433-1435`, `CommonRewards.h:161` |
| Exact names of CommonValues constants? | `BACK_WALL_Y`, `CEILING_Z`, `SUPERSONIC_THRESHOLD`, `BALL_MAX_SPEED` — all confirmed | `CommonValues.h` |
| Does Vec support `/` scalar? | Yes — RocketSim Vec supports standard arithmetic operators | `Framework.h` (RocketSim namespace) |
| Coordinate convention? | Positive Y = toward Orange goal. Negative Y = toward Blue goal. | `CommonValues.h` comments |
| Correct normalization for ball speed? | `BALL_MAX_SPEED = 6000.0f` | `CommonValues.h` |
| `RS_CLAMP` available? | Yes — defined in RocketSim includes | Throughout codebase |

---

## How to Handle Unexpected Findings

If you discover something during coding that contradicts this documentation or the Mechanic Spec:

1. **Stop and flag it to Nathan immediately** — don't make design decisions unilaterally
2. Write a comment in the code where the issue is, e.g., `// TODO NJH: confirm this approach — [description of issue]`
3. Document the finding at the bottom of this file under "Session Discoveries"

Do not silently work around unexpected behavior. Surface it.

---

## Session Discoveries
*(Claude Code appends findings here during the session)*

| # | Finding | File:Line | Impact | Nathan's decision |
|---|---|---|---|---|
| — | — | — | — | — |
