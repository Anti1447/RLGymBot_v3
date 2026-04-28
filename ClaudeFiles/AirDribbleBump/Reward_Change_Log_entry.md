## [2026-04-21] AirDribbleBumpConversionReward — Rev 1.0 — NJH + Claude

### Status
**Draft — Shadow Mode ready.** Not yet enabled in any phase. Target enablement: Phase 6 Champion on Machine 2 after 200–500M steps of shadow observation.

---

### Hypothesis

A narrow, event-gated reward for the complete air dribble bump mechanic will teach the bot to convert air dribbles into unsaveable shots by removing the last defender — a mechanic confirmed to be effective at Champion+ and nearly unstoppable against shadow-defending opponents. The three existing rewards covering parts of this mechanic (`AirDribbleBumpReward`, `AirBumpDemoReward`, `AirDribbleDemoAssist`) all have gating loose enough to be farmed by proxy behaviors (chaotic aerial ramming, short-dribble-then-bump patterns that don't establish a real carry, bumps where the ball is not actually goal-bound). This reward replaces all three with a single event payout that fires only when **all four conditions** of the mechanic are simultaneously satisfied.

### Supersedes

- `AirDribbleBumpReward` (CompleteMechanicsRewards.h) — phase-based state machine, but no "ball is goal-bound" gate and no "opponent was actually the defender" gate. Farmable.
- `AirBumpDemoReward` (CompleteMechanicsRewards.h) — doesn't require multiple air touches at all. Rewards any airborne bump near the ball. Not the mechanic.
- `AirDribbleDemoAssist` (AdvancedDemoRewards.h) — 0.3s airborne + 2 touches is too permissive and there is no post-bump conversion check.

None of the three are currently in `GetPhasedRewards()` for Phase 5 Diamond, so no weight retirement is needed at enablement. Recommend deleting them from the codebase once this reward is validated in Phase 6.

---

### Mechanic Definition

The air dribble bump is a four-part sequence:

1. **Dribble established** — attacker has multiple air touches establishing control, not a single pop.
2. **Ball aimed** — at the moment the attacker redirects to bump, the ball is already travelling goal-bound.
3. **Defender removed** — attacker bumps or demos the nearest defender to the ball.
4. **Conversion survives** — the ball's trajectory remains goal-bound immediately after the bump (i.e., the bump didn't accidentally clip the ball off-line).

All four are required. Partial credit for any subset creates exploitable proxy rewards.

---

### Gating (all must be true at bump frame, unless noted)

| Gate | Threshold | Rationale |
|---|---|---|
| `player.eventState.bump OR .demo` this step | required | Direct contact event |
| Opponent's `eventState.bumped OR .demoed` this step | required | Triangulates which opponent was hit (not nearest-proxy) |
| Opponent distance to our car | ≤ 350 uu | Proximity check on the contact |
| Recent air touches in last 1.5s | ≥ 2 | Dribble was established, not just a pop |
| Ball z at bump frame | ≥ 300 uu | Ball is airborne (excludes ground bumps) |
| Ball velocity alignment to opp goal | ≥ 0.5 | Ball was aimed before the car diverted to bump |
| Bumped opponent == nearest opponent to ball | required | Prevents farming on teammate-adjacent chasers in 3v3 traffic |
| Nearest-opp-to-ball distance | ≤ 2500 uu | The hit opponent was actually a relevant defender |
| Not in kickoff | required | Prevents kickoff bump-chaos farming |
| Per-player cooldown since last payout | ≥ 5s | Prevents repeated payout within a single play |
| Post-bump goal-bound check (+0.5s after contact) | `BallIntersectsOpponentGoalMouth(1.5s, false, true)` | Confirms the bump didn't ruin the shot |

### Payout Structure (multiplicative bonuses on base)

| Component | Value |
|---|---|
| Base (bump) | 15.0 |
| Base (demo) | 25.0 |
| × 1.5 if car ends up between ball and opp goal during 0.5s window (blocks recovery) | multiplier |
| × 1.2 if the contacted opponent was the **last defender** (no teammate behind them) | multiplier |

Max possible payout: demo × last-defender × front-block = 25 × 1.5 × 1.2 = **45.0**.

Wrapping: `ZeroSumReward(new AirDribbleBumpConversionReward(), 0.5f)` in `RewardConfig.cpp`. Zero-sum opponent symmetry is intentional — the bumped team deserves a negative signal proportional to how valuable the bumper's payout was.

---

### Anti-Farming Considerations

- **One payout per airborne sequence** — enforced by 5s cooldown on `lastPayoutTime`, which resets after payout commit.
- **Pending-conversion blocks re-arming** — while a pending evaluation is in flight, new bumps cannot re-arm the reward. Prevents stacking.
- **Goal-bound requirement at bump time AND 0.5s later** — two separate checks. Can't farm by "bumping while ball happens to drift toward net momentarily."
- **Last-defender bonus is gated** — not just "opponent near own goal" but "no other defender further back." Prevents rewarding a front-challenge bump as if it were a goal-line demo.
- **Kickoff exclusion** — full reset of airTouch log and pending state when in kickoff conditions.

---

### Reward Health — W&B Metrics

**What:** confirms the reward *mechanically* fires correctly. These are diagnostic. They do NOT confirm the bot is performing the intended behavior.

| Metric | Computation | Target at Phase 6 maturity (1v1) |
|---|---|---|
| `AirDribbleBumpConv/AirBumpContacts per ep` | `s_airBumpContacts` (any airborne bump near ball) | Baseline only — rate that gets filtered |
| `AirDribbleBumpConv/Qualified per ep` | passed all pre-bump gates | 0.02–0.1 |
| `AirDribbleBumpConv/QualifyRate` | `Qualified / AirBumpContacts` | 0.1–0.3 (most airborne bumps should be filtered out) |
| `AirDribbleBumpConv/ConversionRate` | `PostBumpGoalBound / Qualified` | ≥ 0.75 — if lower, bumps are disrupting aim |
| `AirDribbleBumpConv/DemoShare` | `demos / (demos + bumps)` | 0.2–0.4 (demos rarer but higher-value) |
| `AirDribbleBumpConv/FrontBlockRate` | `frontBlocks / PostBumpGoalBound` | 0.3–0.6 |
| `AirDribbleBumpConv/LastDefenderRate` | `lastDefenderHits / PostBumpGoalBound` | Higher in 1v1 (approaches 1.0), lower in 3v3 |
| `AirDribbleBumpConv/AvgRewardPerConv` | `rewardSum / PostBumpGoalBound` | 18–32 (confirms bonuses are stacking) |

**Alert thresholds:**
- `ConversionRate < 0.5` → gate is leaking, bumps are ruining the shot. Tighten `minGoalAlignment` or `minBallHeightAtBump`.
- `QualifyRate > 0.5` → gate is too loose. Likely `maxDefenderToBallDist` or `maxBumpContactDist` set too wide.
- `Qualified per ep > 0.3` in 3v3 → suspected farming via aerial chaos. Audit via RocketSimVis.

### Behavior Confirmation — RocketSimVis

**What:** confirms the bot is actually performing the *mechanic*, not gaming the metric. Cannot be replaced by W&B numbers.

Required visual verifications before weight commit:
- Bot demonstrably sets up an air dribble carry **first**, then diverts to the defender. Not the reverse (diverting first, hitting ball last).
- Bot doesn't reliably trigger on 3v3 traffic where multiple opponents are near the ball and the "nearest to ball" assignment is coincidental.
- Bot isn't trading bumps with a teammate's chaser to farm payouts.
- Bot respects the cooldown — no 2-in-a-row payouts from a single aerial cluster.
- In 1v1 replays, the bumped opponent is visibly the goalie / shadow-defender, not a random car.
- When the conversion window rejects (ball went off-line), the bot's subsequent policy doesn't degrade (no learned aversion to all bumps).

---

### Phase Gating

- **Phase 1–5:** Disabled (not included in `GetPhasedRewards()`). Air dribble fundamentals (`AirDribbleSetup`, `AirDribbleAccuracy`, `AirDribbleClearing`) need to be stable first.
- **Phase 6 Champion:** Enable in Shadow Mode (`config.shadowMode = true`) on Machine 2 for 200–500M steps. Weight in config = 0 via Shadow Mode flag; only metrics are logged.
- **Phase 6 → 7 transition:** If Shadow Mode metrics hit targets, commit real weight `10.0f` wrapped in ZeroSum(0.5f). Disable Shadow Mode.
- **Phase 7 GC:** Increase weight to `20.0f` if `ConversionRate` stays ≥ 0.75.
- **Phase 8 SSL:** Consider scaling `basePayoutBump` / `basePayoutDemo` downward if the reward is driving policy collapse into aggressive aerial behavior.

### Rollout Plan

1. **Commit header** to `RLGymCPP/Rewards/AirDribbleBumpConversionReward.h`.
2. **Add include** to `RewardConfig.h`.
3. **Enable in Phase 6 Champion** as Shadow Mode:
   ```cpp
   AirDribbleBumpConversionReward::Config cfg;
   cfg.shadowMode = true;
   WeightedReward(new ZeroSumReward(new AirDribbleBumpConversionReward(cfg), 0.5f), 0.0f),
   ```
4. **Add W&B logging** in `ExampleMain.cpp` `StepCallback` — poll `GetAndResetMetrics()` per interval, log all 8 metrics under `AirDribbleBumpConv/` prefix.
5. **Run on Machine 2** for 200–500M steps. Do not touch Machine 1.
6. **Audit:** Check W&B thresholds AND watch 10 random 1v1 games in RocketSimVis.
7. **Commit weight:** Set `shadowMode = false` and weight to `10.0f`. Log decision in this file as Rev 1.1.
8. **Monitor for regression** in other aerial rewards — if `AirDribbleAccuracy` activations drop sharply, the bot may be abandoning finishing dribbles in favor of bump-farming. Revert if seen.

### Open Questions

- **3v3 applicability:** Web research consistently says the mechanic is "brutally effective in 1v1, usable in 2v2, rare in 3v3." Should we restrict the reward to `state.players.size() < 6` or scale payout by game mode? Leaving unscaled for Rev 1.0; revisit if 3v3 conversion rate is unexpectedly high (farming signal).
- **Demo vs bump weighting:** Pros consistently prefer demos for this play but demos are rarer in training because the collision-speed threshold is high. If `DemoShare` < 0.1 after Phase 6, consider lowering `basePayoutDemo` to avoid sparse-event variance.
- **Interaction with `ContextualDemoReward`:** Existing demo reward already rewards removing goalies. Some overlap is intentional (double-signal on the most valuable case) but watch for double-counting if `ContextualDemo` is also active in Phase 6.
- **Flip reset car-to-car deferral consistency:** This reward involves bumping opponents mid-aerial. Confirm it doesn't nudge policy toward the same local optimum (jumping on cars) that car-to-car flip resets were deferred to avoid. Watch `bumped-teammate` rate in metrics.
