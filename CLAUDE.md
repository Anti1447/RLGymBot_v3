# CLAUDE.md — RLGymBot v3

## 1. What is this?

A Rocket League AI bot trained via PPO reinforcement learning. The bot plays 1v1, 2v2, and 3v3.

**Stack:** RLGymCPP (env) → GigaLearnCPP (PPO/LibTorch C++) → RocketSim (physics) → RLBotCPP (deployment)

**Key paths:**
- `GigaLearnBot/src/` — Training entrypoint and config files
- `GigaLearnBot/GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/` — All reward classes (~50 files)
- `GigaLearnBot/GigaLearnCPP/src/` — PPO learner, model wrappers, inference
- `GigaLearnBot/RLBotCPP/` — Live game deployment client
- `GigaLearnBot/rlbot/` — RLBot v5 plugin config
- `GigaLearnBot/python_scripts/` — Reward analysis, LLM reward generator, metrics
- `ClaudeFiles/` — Reference docs, code reviews, archived reward files

## 2. How do I run things?

**Build (CMake + MSVC 2022):**
```bash
cd GigaLearnBot
cmake -G "Visual Studio 17 2022" -A x64 -B build
cmake --build build --config Release --parallel 4
```
Or use the root `BUILD.bat`. The output is `GigaLearnBot.exe` + `GigaLearnCPP.dll`.

**Run training:**
```bash
cd GigaLearnBot/build
set PYTHONHOME=C:\Users\nhafe\AppData\Local\Programs\Python\Python311
bin\GigaLearnBot.exe
```
Training resumes from the latest checkpoint in `build/checkpoints/`. Render mode uses 3 games (one per mode); training mode uses 310 parallel games.

**Deploy to live Rocket League:** Build the `rlbot` target, then use RLBot v5 + BakkesMod. Config is in `GigaLearnBot/rlbot/`.

## 3. What patterns do I follow?

**Phase-based curriculum.** Everything is gated by `CURRENT_PHASE` in `RewardConfig.h`. Phases go Bronze → SSL (enum `TrainingPhase`). To change phase: edit the constexpr, rebuild.

**Three config files control training:**
- `RewardConfig.h/.cpp` — Reward selection and weights per phase (the big one)
- `ModelConfig.h/.cpp` — LR, entropy, network arch, PPO hyperparams per phase
- `GameModeConfig.h/.cpp` — 1v1/2v2/3v3 sampling distribution

**Reward authoring pattern.** Every reward is a header-only class in `Rewards/` inheriting `Reward`. Override `GetReward(player, state, isFinal)` → return float. Wrap competitive rewards in `ZeroSumReward`. Add to `GetPhasedRewards()` as `WeightedReward(new MyReward(), weight)`.

**Before adding a new reward — fill out the Mechanic Spec first:**
Answer these before writing any code. Misalignment caught here costs 5 minutes;
misalignment caught in training costs days.
1. Trigger condition: exact observable state that fires the reward
2. Anti-farming defense: what prevents cheaply looping the trigger
3. Expected activation rate: % of steps this should fire (ballpark)
4. W&B metric name: what you'll watch to confirm it's working
5. "Working" signal at 500M steps: what does success look like in W&B?
6. Overlapping rewards: check RewardCatalog.md before writing anything

**Direct implementation by default:**
New rewards go live immediately. Never use `ShadowReward` or shadow mode.

**Log every change:**
After any reward add/edit/weight change, add a one-line entry to
`Rewards/Reward_Change_Log.md`. Format: date | what changed | hypothesis | result.

**Observation:** `AdvancedObsPadded` with max 3 players per team. Padded so the network handles all game modes with one model.

**Action delay:** Training uses 8-tick skip. Live deployment uses 1-tick. This mismatch matters — see `ClaudeFiles/docs/action-delay.md` if it exists, otherwise ask Nathan.

## 4. What's weird here?

- **~50 reward files, 40+ active simultaneously.** Gradient dilution is the #1 problem. Don't add rewards casually — every new reward dilutes the signal from existing ones. Surgical adjustments > dramatic weight changes.
- **Root directory is full of orphaned .md files.** These are session summaries from prior Claude conversations. Ignore them. Real docs live in `ClaudeFiles/`.
- **Three copies of python scripts** exist (`python_scripts/`, `GigaLearnCPP/python_scripts/`, `build/python_scripts/`). The canonical copy is `GigaLearnBot/python_scripts/`.
- **`#pragma once` in .cpp files** — `RewardConfig.cpp` has `#pragma once` at the top. It's nonstandard but works with MSVC. Don't "fix" it.
- **Reward weights span 0.1 to 250.** `GoalReward` is 250, movement rewards are < 1. This is intentional — the scale encodes priority.
- **No unit tests.** Validation is done by compiling, running training, and watching W&B metrics. The `reward_llm_generator.py` has a `--validate-code` mode that does cl.exe syntax checks.
- **`.vs/` and `build/` are gitignored but present.** Don't touch them.
- **RewardCatalog.md is the source of truth for what exists.** Before writing a new
  reward class, read it. It lists every reward, its trigger, current weight, and status
  (active / shadow / retired). Update it whenever a reward ships or is retired.
  C:\Projects\RLGymBot_v3\GigaLearnBot\GigaLearnCPP\RLGymCPP\src\RLGymCPP\Rewards\RewardCatalog.md
- **One variable per iteration.** When something isn't working, change either the reward
  design OR the weight OR the state setter — not all three at once. Before each
  adjustment, write one sentence: "I think the problem is X and I expect changing Y
  to produce Z in W&B." This is how you build intuition instead of noise.

## 5. How do we work?

**Before changing rewards:** Read the relevant reward file first. Understand what phase it's active in. Check `RewardConfig.cpp` for its current weight. Small changes (±20% weight) are safe; large changes risk policy collapse.

**Before adding a new reward:** Ask Nathan which phase it targets. Every reward needs: a header file in `Rewards/`, an include in `RewardConfig.h`, and a `WeightedReward` entry in the correct phase in `RewardConfig.cpp`.

**When modifying training config:** Changes to `ModelConfig.cpp` affect all future training from checkpoint. LR changes, entropy changes, and gamma changes should be gradual (scale over 2-4B steps, not instant).

**Build after every change.** This is C++ — if it doesn't compile, it doesn't exist. Use the CMake build above.

**Don't generate mountains of markdown.** Keep docs in `ClaudeFiles/docs/` if needed. Don't create summary files in the project root.

**Key references:**
- Reward class API: see `Rewards/Reward.h` and `Rewards/CommonRewards.h` for patterns
- Game state/player types: see `RLGymCPP/Gamestates/` and `CommonValues.h`
- Math utilities: see `RLGymCPP/Math.h` and `Rewards/RewardMath.h`
- Phase config details: see `RewardConfig.cpp` (the source of truth for what's active)

## 6. ⚠️ CRITICAL: File Deletion Safety Protocol

**IMPORTANT: Any attempt to delete, remove, or clean up directories/files REQUIRES explicit human approval.**

### Rules:
1. **BEFORE any deletion attempt**, Claude must:
   - Clearly state which files/directories will be DELETED
   - List the exact paths
   - Explain why deletion is needed
   - **ASK FOR EXPLICIT USER APPROVAL** in plain English
   - Wait for user to confirm before proceeding

2. **If deletion is attempted without approval:**
   - STOP immediately
   - Report what was attempted
   - Ask for confirmation

3. **Critical directories MUST NEVER be deleted without explicit approval:**
   - `build/` — Contains compiled binaries and checkpoints
   - `build/checkpoints/` — Training checkpoints (IRREPLACEABLE)
   - Any directory with checkpoint data
   - `GigaLearnCPP/` — Source code
   - `src/` — Configuration and entrypoint

4. **Safe cleanup operations (with user consent):**
   - Build cache (`CMakeCache.txt`)
   - Build artifacts in a clean build directory
   - Temporary files that are explicitly identified

### Example of CORRECT behavior:
```
Claude: "I need to clean the build directory. This will DELETE:
  - C:\Projects\RLGymBot_v3\GigaLearnBot\build/CMakeCache.txt
  - C:\Projects\RLGymBot_v3\GigaLearnBot\build/CMakeFiles/

Should I proceed? Please confirm explicitly."

User: "Yes, delete the cache files"

Claude: "Confirmed. Deleting specified files now..."
```

### Example of WRONG behavior:
```
Claude: "Cleaning build directory..." (proceeds without asking)
Claude: "rm -rf build" (deletes entire directory without approval)
```

**This protocol exists because:**
- Training checkpoints are irreplaceable
- Deleting `build/` removes training progress
- Accidental deletions can waste weeks of training
- Human oversight prevents catastrophic losses

## 7. Learning Loop Protocol

The full loop is:
Spec → Read RewardCatalog → Code → Deploy live →
Train → W&B alerts → Behavior check → Log result

**One variable per iteration.** Before each adjustment write one sentence:
"I think the problem is X and I expect changing Y to produce Z in W&B."
Change only that one thing. This is how you build intuition instead of noise.

**W&B — stop training and investigate if:**
- `kl_divergence` > 0.05
- `entropy` < 0.02
- `mean_reward` drops > 20% over 5 consecutive iterations
- Any single reward dominates > 40% of total reward signal

**Weight estimation heuristic:**
Activation rate × desired contribution to total = starting weight guess.
Never jump more than ±50% of current weight in one change.

**Log every change:**
After any reward add/edit/weight change, add one line to Reward_Change_Log.md.
Format: date | what changed | hypothesis | result (filled after observation).

### Reward Health vs. Behavior Confirmation

These are two different questions. Never conflate them.

**Reward Health (W&B) — "Is the reward functioning as designed?"**
Answers: Is it firing? Is it being farmed? Is the activation rate in the expected range?
This tells you the reward is working mechanically. It does NOT tell you the bot
is learning the intended behavior.

Signs of a healthy reward signal:
- Activation rate is in the expected ballpark (not zero, not suspiciously high)
- Value distribution looks reasonable (no extreme outliers suggesting farming)
- Co-occurring rewards that should correlate are actually correlating
  (e.g. underside_contact_reward and flip_reset_trigger_reward rising together)

**Behavior Confirmation (RocketSimVis / Live RL) — "Is the bot actually doing the thing?"**
This is the real answer. W&B cannot confirm this. Only visual observation can.
A reward can be firing at a healthy rate while the bot has found a degenerate
strategy to trigger it that looks nothing like the intended mechanic.

Schedule a RocketSimVis review after every 500M–1B steps when a new reward
is active. This is not optional — it's the only way to confirm behavior.

**In the Mechanic Spec, define both separately:**
- Reward health check: what metric, what range, what would look suspicious
- Behavior confirmation: what you'll look for in RocketSimVis specifically
  (e.g. "bot approaches ball from below at ~45°" not "flip_reset_rate > 0.1%")

---

## 7A. Work Type Reference

Different categories of bot improvement have different reward patterns,
different signals, and different failure modes. Match your approach to the
work type before writing any code.

---

### MECHANICS
*Flip resets, aerials, dribbles, flicks, wavedashes, wall plays*

**Reward pattern:**
- Event-based trigger (fires once on success, not continuously)
- All conditions must be met simultaneously — no partial credit
- Anti-farming: require meaningful context (height, speed, ball proximity)
- Follow with productive use validation window (≤2s for PPO credit assignment)

**State setter:** Almost always required. Mechanics need curated initial states
to explore — they will never appear organically at natural frequency.

**"Working" signal:** Activation rate climbs from near-zero to a stable plateau.
Behavior visible in RocketSimVis within 500M–1B steps of activation.

**Common failure modes:**
- Reward never fires → bad state setter geometry or trigger condition too strict
- Reward farms → condition not requiring game-useful context
- Mechanic learned but wrong approach → check state setter spawn geometry

---

### POSITIONING
*Rotation, support position, spacing, goalpost coverage, shadow defense*

**Reward pattern:**
- Continuous shaping (fires every step, small value)
- Distance/angle to ideal position as a smooth curve, not a threshold
- Phase-gate: positioning rewards should tighten as phase advances
  (Bronze: loose, Champion+: strict geometric precision)
- ZeroSum wrapper usually appropriate — good positioning by one team
  is bad for the other

**State setter:** Random state is usually sufficient. Positioning behaviors
are general enough that natural game states provide adequate coverage.

**"Working" signal:** Hard to see in W&B directly. Look for:
- ELO improvement without a corresponding goal-count increase (smarter, not luckier)
- Reduction in "out of position" demo deaths in RocketSimVis

**Common failure modes:**
- Reward too continuous → drowns event-based rewards via gradient dilution
  (keep weights low: 1–20f range)
- Position defined in world-space instead of relative-to-ball/goal space →
  breaks under game flow, especially in 3v3
- Conflicts with retreat/rotation rewards → bot freezes trying to satisfy both

---

### TEAMWORK
*Passing, receiving, spacing, numerical advantage, role awareness*

**Reward pattern:**
- Coupled across agents: passing reward requires both passer and receiver
  to behave correctly. Design sender-side and receiver-side as separate rewards
  that fire on the same event from different perspectives
- ZeroSum always: teamwork rewards are inherently competitive
- Gating: don't reward passes that weren't set up (use OptionGatingWrappers)

**State setter:** Multi-agent aware. Need states where a pass is actually
a viable action — ball moving laterally, teammate in position. Pure random
state rarely produces this.

**"Working" signal:**
- Assists per episode trending up in W&B
- 2v2/3v3 ELO diverging upward from 1v1 ELO (teamwork is mode-specific)

**Common failure modes:**
- Sender rewarded without receiver → bot learns to throw the ball, not pass it
- Passing reward fires when ball hits opponent → use team filtering
- Teamwork rewards active before bot can reliably aerial → reward is unreachable,
  wastes gradient budget. Gate to Phase 5+.

---

### DEFENSE
*Saves, clears, backboard reads, shadow defense, goal line coverage*

**Reward pattern:**
- Mix of event-based (save fires once) and continuous shaping (shadow positioning)
- Defensive rewards almost always need ZeroSum — a save is a direct opponent loss
- Anti-overcommit pairing: every defensive reward should have a corresponding
  overcommitment penalty. A save that causes positional collapse is net negative.
- Directional: clears must verify ball is moving away from own goal, not just touched

**State setter:** Defensive scenario states — ball near own goal, opponent in
possession. RandomState covers some of this but dedicated defensive states
improve sample efficiency significantly for save-specific rewards.

**"Working" signal:**
- Goals-against per episode trending down (lagging indicator, slow)
- `SaveReward` + `BigClearReward` activation rates as leading indicators
- `DoubleCommitPenalty` staying low (bot not abandoning position to chase)
- In RocketSimVis: bot doesn't ball-chase off its own goal line

**Common failure modes:**
- Save reward fires on clears that land in corner → opponent immediately recovers.
  Require minimum clear distance/speed.
- Defensive bot never challenges → overcommit penalty too high relative to save reward
- Shadow defense reward conflicts with `EmergencySaveCommitReward` → bot hesitates
  on last-ditch saves. Tune penalty scaling so emergency saves are never suppressed.

---

### PREDICTING NEXT PLAYS / OPPONENT READS
*Preemptive saves, challenge timing, interception, overcommit exploitation*

**Reward pattern:**
- Hardest category to reward directly — the bot must be in the right position
  BEFORE the event occurs, which is a long-horizon credit assignment problem
- Use leading indicator rewards: reward getting to the correct position early,
  not just succeeding at the outcome
- Pair preemptive rewards with outcome rewards: `EarlyDangerDetectionReward`
  (leading) + `SaveReward` (outcome). If only the outcome fires, the bot
  learns to react, not predict.
- Window-based: define a time window (e.g. 1–2s before ball arrives) in which
  the positioning reward fires. Outside the window, no reward.

**State setter:** Scenario states are critical here. "Opponent has ball in
corner, your bot is in net" is the seed state for preemptive read training.

**"Working" signal:**
- In RocketSimVis: bot moves to cover before the opponent commits to a shot

**Common failure modes:**
- Leading indicator reward too sparse → credit assignment fails, bot never
  connects early positioning to eventual save outcome
- Prediction reward conflicts with rotation → bot camps in net instead of rotating
- Overtrained on preemptive reads → bot freezes waiting for shots that don't come.
  Balance with active pressure rewards.

---

### BOOST MANAGEMENT
*Collection routing, conservation, boost denial, efficiency under pressure*

**Reward pattern:**
- Almost entirely continuous shaping — boost management is a persistent behavior,
  not an event
- Avoid rewarding boost amount directly → bot sits on boost pads and avoids the ball
- Reward boost-efficient plays: reaching the ball with less boost used, or
  maintaining boost above threshold while still contesting
- Denial (stealing opponent boost) is a separate reward from conservation —
  design and validate them independently

**State setter:** Default random state is fine. Boost management is general
enough that natural game distributions cover adequate scenarios.

**"Working" signal:**
- Average boost at ball contact trending upward (bot conserves before committing)
- Boost pickup frequency staying high (bot routes through pads efficiently)
- In RocketSimVis: bot doesn't arrive at ball on 0 boost in critical moments
- ELO improvement in sustained pressure scenarios (boost management pays off late)

**Common failure modes:**
- Boost conservation reward too high → passive play, bot won't commit aerials
- Pickup reward alone → bot ignores ball to collect pads
- No pressure qualifier → bot conserves boost even when it should be attacking.
  Require ball proximity or game context for conservation rewards.

---

### CAR CONTROL / RECOVERY / MOBILITY / SPEED
*Landing recovery, wavedash, flip cancel, speed preservation, wall exits*

**Reward pattern:**
- Technique rewards: fire on correct execution of a specific movement pattern
  (wavedash detected by handbrake + landing + no flip)
- Recovery rewards: multi-wheel landing bonus, penalty for nose-first crashes
- Speed preservation: reward maintaining velocity through a maneuver vs. before
- These are low-weight, high-frequency rewards — they should shape baseline
  movement without competing with game-state rewards

**State setter:** Dedicated recovery states are high value — spawn car mid-fall
at various angles and heights to force recovery practice. Without this,
recovery rewards only fire on organic bad landings (rare at Phase 5+).

**"Working" signal:**
- Average landing wheel count trending upward (smoother recoveries)
- In RocketSimVis: bot doesn't bounce or spin out on wall exits
- Speed at ball contact trending upward (better approach efficiency)

**Common failure modes:**
- Recovery reward fires on any 4-wheel landing → bot takes unnecessary bounces
  to farm the reward. Require landing after airborne minimum height/time.
- Speed preservation reward too high → bot never dodges or commits aerials
  (dodging kills speed momentarily)
- Wavedash reward fires on ground-level dodges → require minimum height before
  the landing that triggers it

---

### GENERAL / OTHER
*Experimental rewards, composite behaviors, cross-category work*

**Reward pattern:**
- If it doesn't fit a category above, define its category before coding it.
  "General" is not a design pattern — it's a flag that the trigger condition
  needs more thought.
- Composite rewards (combining multiple signals into one class) should be
  documented with each component's individual contribution so they can be
  debugged independently via W&B sub-metrics.

**State setter:** Depends on what the reward targets. Default to random state
and reassess if activation rate is too low after initial training run.

**"Working" signal:** Define this explicitly before writing code.
If you can't state what "working" looks like in W&B, the reward isn't
specified enough to implement yet.

**Common failure modes:**
- "General" rewards that are actually poorly-specified mechanic rewards →
  activation rate near zero, no learnable signal
- Composite rewards that mask individual component failure → add per-component
  W&B logging so you can see which sub-signal is dead

**W&B tells you about the reward. RocketSimVis tells you about the bot.**
These are not the same thing. A reward can be healthy while the bot has learned
a degenerate shortcut. Always confirm behavior visually before concluding a
mechanic has been learned. Treat W&B metrics as health indicators and
leading signals, never as proof of success.

---

## 7B. Mechanic Spec Template

Fill this out before any code is written. The category determines which
patterns and failure modes to watch for (see 7A).
```
## Mechanic Spec: [Name]
Date: YYYY-MM-DD
Category: MECHANIC | POSITIONING | TEAMWORK | DEFENSE | PREDICTION | BOOST | CAR CONTROL | GENERAL
Target Phase: PHASE_X_NAME
Machine: M1 (live) | M2 (experiment)

### What is it?
[One sentence plain English description]

### Trigger Condition
[Exact observable state(s) that fire the reward. Be specific — height thresholds,
velocity requirements, player state flags, etc.]

### Anti-Farming Defense
[What prevents the bot from looping this trigger cheaply without game value?]

### State Setter Required?
[ ] No — random state provides enough coverage
[ ] Yes — describe spawn geometry:
    Ball: [position, velocity, height]
    Car: [orientation, velocity, distance to ball]
    Frequency: [% of episodes]

### Reward Structure
[ ] Event-based (fires once on success)
[ ] Continuous shaping (fires every step)
[ ] Hybrid (continuous approach + event bonus)
ZeroSum: [ ] Yes  [ ] No
Productive use window: [N/A | Xs]

### Expected Activation Rate
[% of steps this should fire — your estimate before running it]

### W&B Metric Name
[Name of the custom metric you'll log, e.g. "flip_reset_acquisition_rate"]

### Reward Health Check (W&B)
[What metric, and what range looks healthy vs. suspicious?
Be specific about what farming would look like here so you know to watch for it.]

### Behavior Confirmation (RocketSimVis)
[What does the bot's movement/decision actually look like when this is working?
Describe it in plain English as if you're watching a replay.
This is the real success criterion — W&B alone cannot confirm this.]

### Rewards This Overlaps With
[Check RewardCatalog.md. List any that share trigger conditions or target behavior]

### Starting Weight
[Estimate based on: activation_rate × desired_contribution_to_total_signal]

### Notes
[Anything else Claude Code needs to know to implement this correctly]

---

## 7C. Time-Dependent Reward Coding

When a reward needs to gate on duration, cooldown, or time since an event, follow these rules.

### Always use float seconds, never raw ticks

All timing in this codebase is `float` seconds via `state.deltaTime`. Never use
`lastTickCount` arithmetic for timing — it couples to tick-rate assumptions and is
inconsistent with every other reward class.

Fallback when deltaTime is zero or missing:
```cpp
float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
```

### Cooldown / minimum re-trigger interval

Use a **per-player countdown timer** stored in a map keyed by `player.carId`.
The countdown decrements every step regardless of whether the trigger condition is met —
this ensures the cooldown expires correctly even if the player is in continuous contact.

```cpp
// Members
float cooldownSeconds;                                           // set in constructor
mutable std::unordered_map<uint32_t, float> _cooldownRemaining; // cleared in Reset()

// Constructor: accept milliseconds (user-facing), store as seconds (internal)
MyReward(float cooldownMs = 500.0f) : cooldownSeconds(cooldownMs / 1000.0f) {}

virtual void Reset(const GameState& initialState) override {
    _cooldownRemaining.clear();
}

// In GetReward():
float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
float& cd = _cooldownRemaining[player.carId];   // lazy-init to 0.0f
if (cd > 0.0f) { cd = RS_MAX(0.0f, cd - dt); return 0.0f; }
// ... compute reward ...
if (reward > 0.0f) cd = cooldownSeconds;
return reward;
```

### Duration windows (how long since an event)

Use a **global `currentTime`** accumulated once per frame in `PreStep()`, plus a
per-player `lastEventTime` map. This avoids the "accumulate-per-player" bug where
`GetReward()` is called once per player (inflating the counter in 2v2/3v3).

```cpp
float currentTime = 0.0f;
std::unordered_map<uint32_t, float> lastEventTime;

virtual void Reset(...) override { lastEventTime.clear(); currentTime = 0.0f; }
virtual void PreStep(const GameState& state) override {
    currentTime += (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
}
// In GetReward(): check (currentTime - lastEventTime[id]) < windowSeconds
```

**Do NOT** accumulate `currentTime` in `GetReward()` — that runs once per player, so
in 3v3 you'd accumulate 3× per step.

### Duration gating (reward while a condition holds)

Use a **per-player duration counter** that resets when the condition breaks:

```cpp
std::unordered_map<uint32_t, float> durationActive;

// In GetReward():
float& dur = durationActive[player.carId];
float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
if (conditionMet) { dur += dt; }
else              { dur = 0.0f; return 0.0f; }
// Bonus scales with dur, or triggers once dur > threshold
```

### Per-instance vs per-episode scope

- **Per-instance:** Automatic — all state is in instance members, not statics.
- **Per-episode:** Guaranteed by `Reset()` clearing maps. The training loop calls
  `Reset()` at every episode boundary. A map NOT cleared in `Reset()` leaks state
  across episodes, producing invisible bugs that look like strange activation patterns.

### Common bugs to avoid

| Bug | Symptom | Fix |
|-----|---------|-----|
| Accumulate time in `GetReward` (multi-player) | Timer runs 2–6× fast in 2v2/3v3 | Move accumulation to `PreStep` |
| Forget to `Reset()` per-player map | Cooldown/state persists across episodes | Always `.clear()` in `Reset()` |
| Use `uint64_t lastTickCount` for cooldown | Breaks if `tickSkip` changes; inconsistent | Use `float` seconds + `deltaTime` |
| Only tick countdown when `ballTouchedStep` | Cooldown never expires during dribble | Tick countdown every step regardless of touch |
| Set cooldown even when reward == 0 | First legitimate touch after condition fails is blocked | Only set cooldown when `reward > 0` |
