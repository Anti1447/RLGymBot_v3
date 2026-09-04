# Investigation: InstantSpacingReward vs. InstantSpacingTransitionReward (2v2/3v3)

Date: 2026-08-29 (implemented 2026-08-30)
Status: Implemented. InstantSpacingTransitionReward Rev 1.1 adds the sustained
CLOSE/FAR trickle and the demo-respawn fix described below. InstantSpacingReward
is commented out (not deleted) at 4 of its 5 RewardConfig.cpp sites; the Phase 5
Diamond site was already retired earlier and left as-is. See Reward_Change_Log.md
2026-08-30 for exact weights and reasoning.
Sources: `InstantSpacingReward` (RotationRewards.h:331-385, Rev 1.1) and
`InstantSpacingTransitionReward` (InstantSpacingTransitionReward.h, Rev 1.0, added
2026-08-27 as a comparison sibling — same precedent as DoubleCommitTransitionPenalty).
Goal stated: fully retire the continuous reward in favor of the discrete one.
Live context: RewardConfig.cpp:827-828 already runs this experiment at one site
(Phase 5 Diamond) — continuous commented out, transition weight bumped 4.0f -> 50.0f
with the inline note "Discrete transition needs more weight to be effective, since it
only fires on the first frame of a transition." That note is the first hint of the
central problem below.

## 1. What each reward actually does

**InstantSpacingReward** (continuous): every step, for every living non-demoed
teammate pair, computes horizontal-only (X/Y) distance and classifies it CLOSE / GOOD
/ FAR against `RotationConfig::ForPhase`'s `minTeammateSpacing`/`maxTeammateSpacing`/
`enableMaxSpacing`. Pays a per-step bonus/penalty scaled by how far into that zone the
pair is, averaged across however many teammates a player has. Zero persistent state
(`Reset`/`PreStep` are both no-ops) — every step is independently re-evaluated from
scratch.

**InstantSpacingTransitionReward** (discrete): uses the *identical* zone classification
(byte-for-byte, documented and verified), but tracks each pair's confirmed zone across
steps and pays a single flat `+/-entryReward` only on the step a pair's zone changes
into or out of GOOD, after the new zone has held continuously for `confirmHoldSec`
(0.2s). Silent otherwise, including while a pair sits in the same zone indefinitely.

## 2. Six scenarios for InstantSpacingReward, clearest -> least clear value

### Scenario 1 (clearest) — sustained GOOD spacing, 2v2, whole episode
Two teammates hold a stable triangle distance inside the GOOD band the entire point.
**Value:** a small, constant positive bonus every step, unambiguous — this is the
reward's textbook target case.

### Scenario 2 (easy) — sustained CLOSE clustering, multi-second stretch
Two teammates ball-watch shoulder to shoulder well under `minTeammateSpacing` for
several seconds. **Value:** a penalty scaled by how far under the threshold they are,
every step, for as long as it continues — equally unambiguous, and exactly the
"stop triple-committing" behavior this reward exists to suppress.

### Scenario 3 (moderate) — 3v3, mixed pair outcomes averaged into one scalar
Player A is well-spaced from B (GOOD) but too close to C, who wandered into them
through no fault of A's (CLOSE). The reward averages both pairings into one number for
A. **Value** requires a judgment call the codebase has already visibly struggled with:
Rev 1.1's own changelog entry describes raising the good-spacing bonus specifically
because one-good-one-far was netting negative for the innocent player. Averaging
dilutes a player's own correct behavior by a partner's mistake; whether that's fair
design or needs to be MIN/SUM instead of AVG isn't derivable, only tunable.

### Scenario 4 (moderate-hard) — vertical separation during an aerial
A grounded defender sits directly under a teammate going for a high aerial —
horizontal distance is ~0, so full CLOSE penalty applies despite there being no real
congestion (this is often *correct* backup positioning, not clustering). Rev 1.1
already made this horizontal-only by design (to fix the opposite bug — full 3D
distance calling stacked positions "good"), but that fix doesn't distinguish
legitimate depth-support from a genuine ground-level clump. **Value** is genuinely
unclear without a Z-aware carve-out nobody has designed yet.

### Scenario 5 (hard) — kickoff-adjacent transient spacing
The instant the kickoff gate lifts (ball drifts enough to exit the parked-ball check),
players are still in kickoff-formation spacing, which the changelog itself documents
as under even Bronze's minimum (509-809uu spawns vs. 1200uu+ thresholds). The gate is
a hard ball-state boolean, not a "time since kickoff ended" ramp, so full penalty can
apply the instant the gate lifts, before either player has had a chance to reposition.
**Value** — full penalty (mechanically correct), zero penalty, or graded by elapsed
time — is a real judgment call the current gate doesn't resolve gracefully.

### Scenario 6 (hardest) — immediately after a teammate respawns from a demo
A demoed player returns at a fixed team-relative spawn point dictated by wherever play
happened to be at the moment of the demo — not a decision either player made. The
`isDemoed` check exempts the reward only *while* demoed; the instant they respawn,
they're fully eligible again with no grace period, so a purely unlucky spawn-adjacent
teammate can immediately start accruing a CLOSE penalty for a fluke of engine spawn
geometry. **Value** is undecidable from GameState alone — same causality problem as
Scenario 6 in the DoubleCommitPenalty investigation (bounce vs. demo vs. decision).

## 3. The same six scenarios under InstantSpacingTransitionReward

### Scenario 1 — sustained GOOD spacing
**Difference, and the central issue for a full replacement:** a pair's very first
sighting (episode start, or first time seen) initializes `confirmed = pending =
<current zone>` with **no payout** — there's nothing to transition from yet. A pair
that reaches GOOD once and then simply *stays* there for the rest of the episode
never fires again. Under the continuous reward this pair earns steady positive
reinforcement for the entire duration; under the discrete reward it earns it exactly
once (if it entered GOOD from elsewhere) or **never** (if it started in GOOD). This
is not a minor nuance — it means "maintain good spacing" carries literally zero
ongoing training signal once achieved, which directly undercuts what CLAUDE.md's own
Section 7A calls for in the POSITIONING category: continuous shaping, precisely
because positioning quality needs to be scored *continuously*, not just at the moment
it changes.

### Scenario 2 — sustained CLOSE clustering
**Same structural gap, and the more dangerous direction of it.** The flat
`-entryReward` fires once on entry to CLOSE and then goes silent for as long as the
pair remains clustered — no matter how much longer it lasts, and with no severity
scaling (a a pair drifting to just-under-threshold CLOSE pays the identical flat
penalty as a pair stacked on top of each other, unlike the continuous reward's
proportional scaling). **This is the issue the RewardConfig.cpp:828 comment is
already reacting to** ("only fires on the first frame") — bumping the weight from 4.0f
to 50.0f makes the *single* event bigger, but does not restore any cost for *staying*
clustered. A policy under a pure discrete replacement can learn to eat one clustering
penalty and then camp there for the rest of the point at zero further cost — worth
verifying this isn't already happening in the Phase 5 Diamond run via
`Penalty/DoubleCommit`-style metrics cross-referenced with match length, or
RocketSimVis directly.

### Scenario 3 — mixed pair outcomes (3v3 averaging vs. per-pair events)
**Real aggregation-philosophy difference, no clean winner.** The continuous reward
always produces one smoothed number per player, diluted by a bad partner. The
discrete reward tracks each pair independently and sums whichever pair-events fire
for a player that step (documented: a player in two pairs can be paid for both if
both transition the same step). Consequence: a player with one *stable* good pairing
and one *stable* bad pairing gets a burst of feedback when either relationship first
settles, then nothing further about the persistent bad one — asymmetric and highly
dependent on how static vs. volatile each pairing happens to be, which isn't
obviously more or less "correct," just different in a way worth being aware of.

### Scenario 4 — vertical separation during an aerial
**No difference.** Zone classification is byte-for-byte identical between the two
classes by explicit design (confirmed in InstantSpacingTransitionReward.h's own
header comment), so switching neither fixes nor worsens this ambiguity — it inherits
it unchanged.

### Scenario 5 — kickoff-adjacent transient spacing
**Genuine improvement, and the strongest argument for the migration.** Both classes
gate on the identical kickoff condition, but the discrete reward's "first sighting,
no payout" rule means the very first post-kickoff zone classification — whatever it
is — never costs anything, by construction, without needing a separate kickoff-aware
carve-out. The continuous reward has no equivalent grace and needed an explicit,
separately-tuned kickoff gate (Rev 1.1) specifically because it penalized kickoff
formation every episode before that fix. This scenario is a case where the discrete
design is structurally more robust, not just differently shaped.

### Scenario 6 — immediately after a demo respawn
**This is the one I'd flag as an actual bug, not just a design tradeoff.** The
discrete reward's demo handling *suspends* a pair's processing while either player is
demoed but deliberately does **not** erase the pair's `confirmedZone` — "the pre-demo
confirmedZone baseline survives respawn," by explicit design, matching this session's
"suspend, don't erase" convention used elsewhere (flip resets, dribble carries) where
preserving state across a demo is the right call because the underlying situation is
continuous. Spacing is different: the pre-demo confirmed zone reflects a *positioning
relationship that no longer exists* — the respawn point is essentially random with
respect to the surviving teammate's current position. By preserving the stale
baseline and comparing the post-respawn distance against it, this design is *more*
likely than the continuous reward to fire a spurious transition event (reward or
penalty) off pure spawn-point luck once `confirmHoldSec` elapses, since it's actively
set up to detect a "change" relative to a baseline that has nothing to do with the
respawn. The continuous reward has the same blind spot (no grace period either), but
it doesn't compound it by anchoring a transition-detector to stale pre-demo data. A
fresh re-initialization on respawn (confirmed = pending = <zone at first post-respawn
sighting>, no payout — same as any other pair's first sighting) would be the safer
default here, unlike flip-reset/dribble-carry state where continuity across a demo is
actually meaningful.

## 4. What this means for "completely use InstantSpacingTransitionReward"

Scenario 4 carries over unchanged (no new risk) and Scenario 5 is a genuine win. But
Scenarios 1, 2, and 6 are real problems for a full, unconditional replacement:

- **1 & 2 together** mean the discrete reward teaches "notice the moment your spacing
  changes," not "maintain good spacing" or "don't stay clustered" — the second and
  third of which is arguably the actual point of this reward, and is explicitly the
  CLAUDE.md-documented pattern for the POSITIONING category (continuous shaping, not
  event-gated). Fully retiring the continuous reward removes all cost for camping in
  a bad arrangement after paying one entry penalty, and all reinforcement for holding
  a good one.
- **6** looks like an actual oversight in how the "suspend, don't erase" convention
  got applied to a reward where pre-demo state isn't meaningfully continuous with
  post-respawn state, and should probably be fixed (reset the pair fresh on respawn)
  regardless of what happens with the broader replacement question.

I don't think a straight swap is safe yet. Two honest paths forward, not a
recommendation for either without your call:

**A. Keep both, permanently** (like DoubleCommitPenalty/DoubleCommitTransitionPenalty)
— treat the discrete reward as a sharper "you just fixed/broke it" credit-assignment
layer on top of the continuous reward's ongoing "how are you doing right now" signal,
rather than a replacement. This matches what CLAUDE.md's positioning-category guidance
would suggest and is the lowest-risk option, but doesn't fulfill "completely use"
literally.

**B. Actually retire the continuous reward**, but first close the Scenario 2 gap —
some form of ongoing cost for *staying* in CLOSE (even a small flat per-step trickle
penalty while a pair's confirmed zone is CLOSE, distinct from the entry event) is
probably required, or the training run risks learning that clustering is a one-time,
affordable toll rather than a sustained cost. This reintroduces some continuous
character into a "discrete" reward, which is worth being upfront about rather than
pretending it's a clean architecture swap.

Either way, Scenario 6's stale-baseline issue is worth fixing regardless of which path
you pick.
