Problem: When something isn't working, it's not always clear whether the problem is reward design (wrong trigger, farmable) or weight (right reward, wrong scale) or exploration (reward is fine, policy never encounters it). You often adjust both simultaneously, which makes it impossible to learn from.
Fix: Change One Variable Per Iteration Protocol
Before each adjustment, write one sentence: "I think the problem is X and I expect changing Y to produce Z in W&B." Then change only Y. This sounds obvious but in practice the loop often adjusts design + weight + state setter simultaneously, and you can't tell what worked.
Additionally, add a REWARD_CHANGE_LOG.md (date, what changed, hypothesis, result). Over a full phase this becomes a high-signal knowledge base Nathan and Claude Code can reference — especially useful for the "we tried this before" moments that currently rely on memory.

# Reward Change Log

One entry per change. Format: Date | Change | Hypothesis | Result
Result is filled in after ~500M–1B steps of observation.

---

## Template
| Field | Content |
|---|---|
| Date | YYYY-MM-DD |
| What changed | Class name + what was modified (new / weight X→Y / retired / design change) |
| Hypothesis | "I think the problem is X and I expect changing Y to produce Z in W&B" |
| Result | What actually happened. Fill in after observation. |

---

## Log

| Date | Change | Hypothesis | Result |
|---|---|---|---|
| 2026-04-05 | Phase 6 Champion baseline established. RewardCatalog.md created with 85+ active rewards. | Initial catalog created to provide canonical reference for future reward modifications. | Complete — RewardCatalog.md now lists all Phase 6 rewards, weights, and enable/disable status. |
| 2026-04-14 | Implemented 3-tier team boost cooperation system. Created BoostCooperationRewards.h with KickoffCornerBoostAssignmentReward (weight 3.0, PHASE_2), RotationBoostAwarenessReward (weight 0.5, PHASE_3), RoleBasedBoostPreferenceReward (weight 0.3, PHASE_4). | Team boost conflicts on kickoff 3v3 cause inefficient pad distribution and wasted positioning. Hypothesis: proximity-based kickoff pad assignment (excluding ball-closest player), passive rotation awareness, and gentle role-based nudges will improve team coordination without disrupting ball pursuit priority. | Pending — shadow mode validation required before live deployment to verify activation rates and ensure no farming or ball-pursuit latency regression. |
| 2026-04-16 | ShotPlacementReward (ShootingRewards.h): MAX_DIST 4000→2500, BEHIND_BONUS 0.5→0.75. Weight unchanged at 80.0 (Phase 6). | Defender at 4000 UU was treated as fully open — too generous, washed out the "covered vs uncovered" signal. Tightening to 2500 sharpens the placement gradient; raising BEHIND_BONUS pushes shots toward where defenders aren't facing. Expect `shot_placement_reward` mean to drop ~10–20% (stricter scoring) and shot success rate vs covered shots to climb in RocketSimVis. | Pending — observe over 500M–1B steps. |
| 2026-04-16 | SpeedScale_PowerShot (CommonRewards.h): extended upper cap from 140 kph→1.5x to 180 kph→2.0x via new third segment (140–180 kph linear ramp 1.5→2.0). Affects all SpeedScaledShotReward composites (4 active in Phase 6). | Curve previously plateaued at 140 kph (3889 UU/s), giving the bot zero marginal incentive to shoot harder. RL realistic max ~180 kph. Hypothesis: extending the bonus encourages harder strikes without changing baseline reward shape. Expect mean shot speed at contact to creep up 5–10 kph and `speed_scaled_shot_reward` mean to rise ~8–12%. | Pending — observe over 500M–1B steps. |