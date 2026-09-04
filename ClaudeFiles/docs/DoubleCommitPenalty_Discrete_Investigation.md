# Investigation: Discrete (transition-triggered) DoubleCommitPenalty

Date: 2026-08-29
Status: Implemented as DoubleCommitTransitionPenalty.h (Rev 1.0, 2026-08-29). Wired
alongside DoubleCommitPenalty at all 5 existing RewardConfig.cpp sites for direct
W&B comparison. See Reward_Change_Log.md 2026-08-29 for exact implementation choices
made against each open question below.
Source reward: `DoubleCommitPenalty` (MultiModalRewards.h:40-186), Rev 3.0.
Scope: 2v2/3v3 only (unchanged — the class already no-ops in 1v1 via `hasTeammate`).
Not in scope: `KickoffDoubleCommitPenalty` — a different class, kickoff-only, untouched here.

## 1. What the current reward actually does

Every step, for every non-first-man player who is within `commitDistance` (1500uu) of
the ball and actively committing toward it (speed ≥ 500uu/s, within a 60° cone) while a
teammate is closer (and no emergency-defense exemption applies), a per-player timer
(`commitStartTime`) accumulates `deltaTime`. Once that timer exceeds `commitThreshold`
(0.5s), the reward returns a flat `-penaltyPerBot * commitCount` **on every subsequent
qualifying step**, for as long as the condition holds. This is CLAUDE.md's "continuous
shaping" pattern: cheap to reason about, but it means total penalty scales with how many
*steps* the mistake lasts, not with anything more meaningful, and it never scales by
`deltaTime` (an existing quirk, not something this investigation touches).

## 2. What "discrete, transition-only" means, step by step

1. Extract the exact same boolean gate the reward already computes (`hasTeammate`,
   distance bounds, emergency gate, `firstManId`, `IsActivelyCommitting`, `commitCount >= 1`)
   into a single `bool isDoubleCommitting` per player per step — no behavior change yet,
   just a refactor.
2. Track `wasDoubleCommitting[carId]` from the previous step (the same
   `!prevHad && nowHas` idiom already used elsewhere in this exact file —
   `GoodRecoveryReward`'s `wasInAir` and `MiddleRotationReward`'s `wasInOffensiveZone`
   are the local precedent).
3. Fire a single penalty exactly on the rising edge (`!was && now`). Never fire on the
   falling edge — a penalty for *stopping* the bad behavior would be backwards.
4. The existing `commitThreshold` (0.5s) ramp-up still has a job to do here: it's the
   anti-noise debounce that decides how long the boolean must hold true before the
   "transition" is real rather than jitter. In the continuous version this just delays
   when per-step penalties start; in the discrete version this literally decides how
   many events get created, so it matters more here, not less (see Scenario 4).
5. Decide what `commitCount` scaling means at a single instant rather than sampled every
   step for the reward's duration — and what happens when `commitCount` *changes* while
   a player is still flagged (see Scenario 3).

That's the mechanical part. The genuinely hard part is everything below.

## 3. Six scenarios, easiest → hardest to assign the correct value

### Scenario 1 (trivial) — clean single commit, clean break-off
2v2. B is not first man, closes to within 1500uu at 60°+ alignment, holds it for 0.5s+,
then peels off cleanly (drops out of the cone or below `minCommitSpeed`) before anything
else changes. `commitCount` is 1 the entire time.
**Value:** one rising edge → one flat `-penaltyPerBot * 1` at the transition instant.
No ambiguity in timing or magnitude — this is the case the whole design targets.

### Scenario 2 (easy) — episode ends via a game event, not a behavior change
Same setup, but instead of B consciously disengaging, A (first man) simply reaches and
touches the ball. B's own gates (distance/alignment) will naturally go false shortly
after as the ball moves away — no new logic needed for the falling edge.
**Value:** still a single flat penalty at the rising edge. The only question worth
naming is philosophical, not mechanical: B still gets penalized even though the
"harm" was brief and A resolved it fine. That's intentional — this reward scores the
*decision* to double-commit, not the outcome, so a brief-but-real mistake still counts.

### Scenario 3 (moderate) — commitCount escalates mid-episode
3v3. B starts double-committing alone (`commitCount=1`, fires, small penalty). While B
is still flagged, C *also* starts double-committing (`commitCount` becomes 2 for both).
Per-player edge detection gives C their own full transition, scaled to `commitCount=2` —
clean. But B's penalty already fired at `commitCount=1` and, because B never had an
edge (B was already `true`), a naive implementation never re-scores B even though the
team situation just got worse (three players are now on the ball, not two).
**Value:** requires an actual decision, not a formula — either (a) accept the
asymmetry (each player is scored once, at their own onset severity, full stop), or
(b) add a second trigger class ("re-score everyone currently flagged when `commitCount`
itself changes"), which reintroduces some of the continuous reward's plumbing just for
this one case. My default recommendation is (a) — simplicity — but flagging it because
it's a real, not obvious, choice.

### Scenario 4 (moderate-hard) — boundary flicker (anti-farming risk)
Player sits almost exactly at the `commitDistance` edge or the `commitAlignCos` cone
edge, and ordinary movement jitter flips the boolean true/false/true/false several
times within under a second. A naive "fire on every rising edge" implementation fires
*multiple* discrete penalties for what is really one continuous marginal event — worse
than the current continuous version, whose 0.5s sustained-timer resets on any false
frame and would never even reach threshold for this case.
**Value:** the flat "0 or 1 penalty" framing breaks down. Getting this right requires
re-introducing hysteresis: either (a) keep requiring 0.5s of *unbroken* true before the
transition fires (same as today, just gating the edge instead of gating per-step
payout), or (b) add a cooldown after a fired event so re-crossing within some window
doesn't re-fire. Either works, but the debounce/cooldown window length is a tunable
that needs picking, not derived — genuine design judgment.

### Scenario 5 (hard) — legitimate first-man handoff with no bad decision by anyone
Two teammates converge on a 50/50 bounce. A is closest and reads it correctly; B holds
back correctly (not committing). The ball bounces awkwardly and, for a couple of
frames, B's angle to the ball happens to become shorter than A's — a completely normal,
healthy positional swap that happens constantly around loose-ball bounces. B is now
"first man" and A is now "not first man," but A's momentum — already committed toward
the ball from the read they made *before* the bounce — still satisfies
`IsActivelyCommitting`. A gets flagged as double-committing almost immediately after
*losing* first-man status, for a decision A made correctly under the information they
had a fraction of a second earlier.
**Value:** genuinely hard. Firing full penalty here punishes a bounce, not a read.
The likely fix is a short grace period after any first-man handoff (suppress the
transition for N frames for whoever just lost the role) — but "how long" is not
derivable from first principles, and there's no cheap signal in `GameState` that
distinguishes "A is still finishing out a good read" from "A is genuinely tunnel-
visioned and won't disengage." This needs a tunable validated against RocketSimVis,
not a closed-form answer.

### Scenario 6 (hardest) — demo-induced handoff + reaction latency
3v3. A is first man; B and C hang back correctly. A gets demoed mid-chase by an
opponent. `firstManId` recomputes instantly next step (demoed players are already
excluded from that loop), handing first-man status to whichever of B/C is closer — but
physically, a bot can't teleport into "correct" positioning the instant a teammate is
removed from the game. For a few frames, the *other* of B/C may still register as
actively committing (they were reasonably shadowing the play as a contingency) and get
flagged for a "double commit" that is really unavoidable reaction latency to a sudden,
externally-caused roster change — not a positioning mistake at all. Two compounding
problems: (1) this needs its own grace period, plausibly different in length from
Scenario 5's bounce-caused handoff, since a demo is rarer and more chaotic than a
routine bounce; and (2) the reward has no way to observe *why* a handoff happened
(bounce vs. demo vs. boost-out vs. genuine misread) — it only sees resulting positions
and velocities, never the cause.
**Value:** not reliably derivable from the state alone. Any heuristic here (e.g., track
"was the outgoing first man demoed last step" and suppress the next N frames for the
inheriting player) is an approximation stacked on Scenario 5's approximation, and
stacking more special cases starts to rival the complexity of the reward itself. This
is the point where "how much engineering effort buys how much correctness" stops being
a code question and becomes a product decision — worth explicitly deciding to accept a
small false-positive rate here rather than chasing it, unless RocketSimVis shows it's
actually common enough to matter.

## 4. What this means for a first implementation

Scenarios 1-2 are free — the discrete rewrite handles them with no new logic beyond the
edge-detection refactor. Scenario 4 is close to free too (the existing 0.5s threshold,
repurposed as an edge-gate instead of a per-step gate, already solves it). Scenarios 3,
5, and 6 are where "discrete" stops being a pure refactor and starts requiring new
tunables (re-fire-on-escalation, first-man-handoff grace period, demo grace period) that
trade implementation complexity against how much of the false-positive tail is worth
chasing.

**Recommended default for a first pass**, pending your call: implement 1/2/4 as
described (edge-triggered, existing 0.5s debounce reused as the edge-gate, single flat
`-penaltyPerBot * commitCount` at the transition instant), accept the Scenario 3
asymmetry (score each player once at their own onset severity), add a short first-man-
handoff grace period for Scenario 5 (skip the transition if the player was first man
within the last ~0.3-0.5s), and explicitly *not* special-case demos for Scenario 6 in
v1 — instead watch its false-positive rate via a new W&B metric
(`DoubleCommit/TransitionCount` vs. the old continuous fire rate) before deciding
whether it's common enough to justify the extra state tracking.

Open questions for you before I implement:
- Scenario 3: accept the per-player-onset-only scoring, or want team-level re-scoring on escalation?
- Scenario 5: is a first-man-handoff grace period worth adding in v1, or defer until RocketSimVis shows it's a real problem?
- Target phase(s): same phases DoubleCommitPenalty is currently active in (RewardConfig.cpp — Phases 1/2/4/5/6/9, weights 5.0-100.0f), or a subset?
