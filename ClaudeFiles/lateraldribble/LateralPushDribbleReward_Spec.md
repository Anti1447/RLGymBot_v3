# Mechanic Spec — `LateralPushDribbleReward`

**Author:** NJH + Claude
**Date:** 2026-06-30
**Revision:** 1.0 (spec — pre-implementation review)
**Phase target:** Phase 5 Diamond+ (offensive setup mechanic; enter at conservative weight)
**File:** `RLGymCPP/Rewards/LateralPushDribbleReward.h`

---

## 1. Design Intent

Teach the bot a **lateral push dribble**: shepherd a *grounded* ball in front of the
car (not carried on the roof) and steer it **across the field toward the opponent's
far corner**, kept **slow and flat** so the bot can re-hit / flick at the end of the
push. This is an *offensive setup* primitive — it manufactures a cross-field angle
into the opponent's weak side rather than forcing a straight-on contest.

**Target** (per request):
- The opponent corner **furthest from the ball's current position** (the cross-field
  corner), **OR**
- Anywhere on the line **from that far corner to the opponent net** — with **halfway**
  between corner and net as the preferred aim point.

**Hard constraints** (per request):
- Ball horizontal speed **< 2100 uu/s** (controllable; still hittable).
- Ball **z-velocity minimal: |ball.vel.z| < 300 uu/s** (flat roll, not a pop / bounce).

This is deliberately **not** `DribbleReward` (roof carry) and **not** `DribbleSetupReward`
(catch/pop mechanic). It rewards the *ground-level steering push* phase that precedes
those, scoped to a directional objective.

---

## 2. Geometry

Coordinate frame: +Y toward Orange goal; Blue (team 0) attacks +Y, Orange (team 1)
attacks −Y. All target math is done in the **horizontal (x,y) plane**.

- `opponentGoalY = (team==BLUE) ? +BACK_WALL_Y : −BACK_WALL_Y`  (±5120)
- **Net center (horizontal):** `N = (0, opponentGoalY)`
- **Corner aim points:** `(±cornerAimX, opponentGoalY ∓ cornerAimYInset)` — pulled
  inside the wall (`cornerAimX≈3200`) and slightly off the goal line
  (`cornerAimYInset≈200`) so the ball isn't steered into the corner wall fillet.
- **Far corner `C`:** the corner with the **greater horizontal distance from the ball**
  (the cross-field one). Chosen per-frame but sign-latched per `carId` with a small
  hysteresis margin so a ball drifting across x=0 doesn't flip the target every frame.

**Aim cone (the "corner ↔ net, halfway-best" region):**
- `dirCorner = normalize(C − B)`, `dirNet = normalize(N − B)` (B = ball horiz pos).
- **Bisector** `= normalize(dirCorner + dirNet)` → the angular "halfway" axis = **max reward**.
- `halfAngle = acos(dot(dirCorner, dirNet)) / 2`.
- A push velocity direction scores via `smoothstep(cos(halfAngle + coneSlack), 1, align)`
  where `align = dot(ballVelDirHoriz, bisector)`.
  → **1.0** when pushing straight at the halfway point, **partial** anywhere inside the
  corner↔net cone, **0** outside.

---

## 3. Architecture (multiplicative gates, fail-fast)

Per house style: per-player state keyed by `carId`; ordered hard gates that early-return
0; final payout is a **product of [0..1] factors** so any weak dimension collapses the
reward (no additive constellation farming).

**Hard gates (early-return 0):**
1. `player.isDemoed` → 0.
2. `!player.isOnGround` → 0 (push dribble is a ground mechanic).
3. `ball.pos.z > lowBallMaxZ` (≈150) → 0 (ball must be grounded).
4. **Active-shepherd gate:** time since this player last touched the ball
   (`player.ballTouchedStep` resets a per-`carId` timer) must be `< recentTouchWindow`
   (≈0.45s). Prevents claiming a ball someone else is pushing / a ball just sitting.
5. **Flat-roll gate:** `|ball.vel.z| > maxBallVelZ` (300) → 0.
6. **In-front geometry:** `forwardOffset ∈ [minForwardOffset, maxForwardOffset]`,
   `|lateralOffset| ≤ maxLateralOffset`, horiz `dist ∈ [minPushDist, maxPushDist]`.
   (`forwardOffset = rotMat.forward·(ball−car)`, `lateralOffset = rotMat.right·(ball−car)`.)
7. **Speed gate:** `ballSpeedHoriz ∈ [minBallSpeed, maxBallSpeed]` (350 … 2100).
8. **Direction gate:** cone `dirScore > 0`.

**Payout factors (each [0..1]):**
- `dirScore` — cone alignment (dominant term).
- **`crossFactor`** *(Rev 1.1)* — cross-field component floor. `crossComp = (ball.vel.x ·
  cornerSign) / ballSpeed` is the fraction of speed pointing toward the **far-corner x-side**;
  `crossFactor = smoothstep(minCrossFrac, minCrossFrac+band, crossComp)`. Gated (0 below the
  floor). Kills straight-up-field "shots" the wide cone would otherwise pass when the ball is
  far from goal and corner/net subtend a small angle.
- **`yTaperFactor`** *(Rev 1.1)* — y-depth taper. `yToward` grows from own half toward the
  opponent goal line (|y|=5120). Full until `yTaperStart` (3800), decaying linearly to
  `yTaperFloor` (0) by `yTaperEnd` (4800). Stops rewarding a corner-bound push once the angle
  gets tight near goal — deep in y you should be shooting/centering, not cornering.
- `speedFactor` — 1.0 below `softSpeedKnee` (≈1600), ramps **down** to ≈0.3 at 2100
  (keeps it slow/controllable, not a blast), plus a short ramp **up** off `minBallSpeed`.
- `flatFactor = 1 − |ballVelZ|/maxBallVelZ` — flatter rolls score higher inside the band.
- `centerFactor = 1 − |lateralOffset|/maxLateralOffset`, applied as `0.6 + 0.4·centerFactor`
  (gentle — a *lateral* push legitimately runs the ball off-center).
- `proximityFactor` off `dist`, applied as `0.6 + 0.4·proximityFactor`.
- `sustain` — `0.2 + 0.8·clamp(holdTime/sustainFullTime)`; short floor ramp for <0.06s.

`reward = rewardScale · dirScore · crossFactor · yTaperFactor · speedFactor · flatFactor ·
centerTerm · proxTerm · sustainTerm`, clamped `[0, maxPerFrame]`. `rewardScale = 0.30`
(sits in the 0.1–0.5 shaped band).

**Shadow mode:** when `config.shadowMode`, compute everything and record metrics but
`return 0.0f` (no optimizer gradient) — for Machine 2 behavioral confirmation first.

---

## 4. Anti-Farming / Goodhart Audit

| Trap | Defense |
|---|---|
| **Stationary co-presence** (park nose on a still ball) | `minBallSpeed` gate + direction cone requires actual velocity toward target |
| **Hover / pop farming** | grounded-ball gate (`lowBallMaxZ`) + `flatFactor` + hard `|velZ|<300` gate |
| **Blast-and-claim** (one hard hit) | `maxBallSpeed` 2100 ceiling + loses proximity/forward gates as ball escapes |
| **Wrong-direction shepherd** | `dirScore` multiplicative; 0 outside corner↔net cone |
| **Straight-on "shot" far from goal** *(Rev 1.1)* | `crossFactor` floor — push must carry ≥`minCrossFrac` of speed toward the far-corner x-side |
| **Cornering from deep** *(Rev 1.1)* | `yTaperFactor` decays full→0 across `yToward` 3800→4800 |
| **Target flip jitter** at x≈0 | per-`carId` sign latch + hysteresis margin on far-corner choice |
| **Claiming a teammate's push** | `recentTouchWindow` active-shepherd gate keyed to *this* player's touch |
| **Sustain snowball with episode length** | per-frame product is bounded by `maxPerFrame`; sustain saturates at 1.0 |

**Closure check (diagnostic, not paid):** track per-frame change in ball→aim distance.
If reward fires but `closureRate ≤ 0`, the bot is being paid while *not* making
cross-field progress → tighten the cone or raise `minBallSpeed`.

---

## 5. W&B Metrics (`GetAndResetMetrics()` static-atomic idiom)

| Metric | Meaning | Healthy band | Alert |
|---|---|---|---|
| `activationRate` | active frames / eligible-context frames (on-ground + low ball) | 2–15% | `0` after 30M steps → loosen geometry gates |
| `avgBallSpeed` | mean horiz ball speed while active | 900–1800 | `>2000` → bot riding the ceiling; lower `softSpeedKnee` |
| `avgDirScore` | mean cone alignment while active | `>0.55` | `<0.4` → cone too wide / aim wrong |
| `avgVelZ` | mean `|ball.vel.z|` while active | `<150` | `>220` → bobbling; tighten `maxBallVelZ` |
| `avgCrossComp` | mean cross-field fraction toward far corner | `>0.35` | `<0.2` → cone passing near-straight pushes; raise `minCrossFrac` |
| `avgYTaper` | mean y-depth taper multiplier while active | `>0.85` | `<0.6` → most activations are deep in y; the taper is doing the work, check intent |
| `closureRate` | mean per-frame ball→aim distance closed (uu/frame) | `>0` | `≤0` → farming without progress |
| `rewardSum` | total reward issued | — | track vs weight |

**Reward Health vs Behavior Confirmation are separate checks** — these atomics confirm
the reward *fires correctly*; RocketSimVis (§6) confirms the bot *does the thing*.

---

## 6. RocketSimVis Behavioral Checklist (Shadow Mode, Machine 2 ~30×)

- [ ] Ball stays **on the ground** through the rewarded window (no pop).
- [ ] Ball travels **toward the far/cross-field corner**, not the near post.
- [ ] Push speed visibly **controllable** — bot can still catch/flick at the end.
- [ ] No reward when ball is **carried on roof** (that's `DribbleReward`'s job).
- [ ] No reward when another car is the one pushing.
- [ ] Target does **not** flip-flop frame-to-frame when ball crosses center.
- [ ] When ball is already near the corner, aim shifts smoothly toward the **net** half.

---

## 7. Rollout Plan

1. **Isolation, Shadow Mode** on Machine 2 (`shadowMode=true`, weight in config but no
   gradient). Confirm W&B health bands + RocketSimVis checklist. **One variable at a time.**
2. Promote to Machine 1 at **conservative weight (~1.0–1.5)**; watch `activationRate` and
   `closureRate` don't collapse other shaped rewards (gradient dilution is the standing
   bottleneck — this is one more signal in the 40+ stack).
3. If healthy over ~20M steps, consider raising weight or wrapping in `ZeroSumReward`
   only if you want it competitive rather than pure-acquisition.

---

## 8. Open Empirical Questions (track before/after)

1. **`cornerAimX` (3200) vs wall:** does the ball get steered into the corner fillet at
   high field-x? May need to drop to ~2800 or scale inset with proximity to goal line.
2. **`recentTouchWindow` (0.45s):** long enough to bridge dribble taps, short enough to
   not claim a free-rolling ball? Sweep 0.30–0.60.
3. **`softSpeedKnee` (1600):** is 1600→2100 falloff enough to discourage near-ceiling
   pushes, or does the bot still farm at ~2050? Watch `avgBallSpeed`.
4. **Cone `coneSlack` (≈0.18 rad) + cross floor:** the `crossFactor` floor (Rev 1.1) now
   backstops the wide cone far from goal. Open: is `minCrossFrac=0.18` too strict when the
   ball legitimately needs a shallow cross-field angle? Sweep 0.12–0.25 against `avgCrossComp`.
5. **`yTaperStart`/`yTaperEnd` (3800/4800):** does full suppression by 4800 cut off legit
   near-post setups, or is 4800 still too deep? Tune against `avgYTaper` + RocketSimVis.
6. **Overlap with `DribbleSetupReward` PHASE_ROLLING / `BallSteerReward`:** confirm no
   double-pay on the same frames; may want a mutual-exclusion gate.

---

## 9. Config Defaults (summary)

```
lowBallMaxZ        150     maxBallVelZ        300     // flat-roll (requested)
minPushDist         60     maxPushDist        320
minForwardOffset    30     maxForwardOffset   300     maxLateralOffset 160
minBallSpeed       350     maxBallSpeed      2100     softSpeedKnee   1600  // <2100 (requested)
cornerAimX        3200     cornerAimYInset    200     coneSlack       0.18
minCrossFrac      0.18     crossFracBand     0.15     // cross-field floor (Rev 1.1)
yTaperStart       3800     yTaperEnd         4800     yTaperFloor      0.0   // y-depth taper (Rev 1.1)
recentTouchWindow 0.45     cornerLatchMargin  250
sustainFullTime   0.50     rewardScale       0.30     maxPerFrame     0.50
shadowMode       false
```
