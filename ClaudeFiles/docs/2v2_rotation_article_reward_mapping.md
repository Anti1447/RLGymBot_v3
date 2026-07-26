# 2v2 Rotation Article → Reward Coverage Analysis

Source: https://www.rocketleague-help.com/2v2 ("Best Rocket League 2v2 Tips" — rotation vs.
positioning, the 3x3-square rotation grid, and 7 common scenarios).

Purpose: map every concept in the article to the reward classes that already train it, so we
don't duplicate coverage. Cross-checked against `RewardCatalog.md` and `RewardConfig.cpp`
directly (not just agent summaries).

## Concept → Reward mapping

| Article Concept | Covered By | Phase Active | Notes |
|---|---|---|---|
| Don't flick/commit at 0 boost, waste boost on a useless touch, push into teammate's corner, break rotation for boost/demo | `SimpleOvercommitmentPenalty`, `AdvancedOvercommitmentPenalty`, `CornerPlayPenalty` | 5+ / 5+ / Champion+ | General overcommit + corner-crowding discipline |
| Don't early-challenge if teammate isn't rotating back | `DoubleCommitPenalty`, `LastManOvercommitPenalty` | 1–6+ / 4+ | `DoubleCommitPenalty` checks the *scorer's own* commitment while a closer teammate is already first man (Rev 3.0 fix); `LastManOvercommitPenalty` penalizes the last-man-back closing on ball when it isn't an emergency |
| 3x3 grid: "same square = bad" (double commit) | `DoubleCommitPenalty`, `AntiLayeringPenalty`, `DefensiveZoneOverlapPenalty`, `NoMansLandPenalty`, `OverflipPenalty` | 1–6+ / **Champion+** / **Champion+** / **Champion+** / **Champion+** | 5 overlapping rewards target the same "don't stack with teammate" idea from different angles |
| "Same row = bad unless defending" | `AntiLayeringPenalty` (±200 Y-unit band, 2v2/3v3, exempt near own goal) | **Champion+ only** | Closest literal match to the article's row rule |
| "Stay close" / adjacent-square spacing / 2nd-man in midfield (Square 5) | `InstantSpacingReward` (distance band), `ActiveSupportingPositionReward` (far-post/penalty-spot/wide-diagonal slots), `DiagonalZoneCoverageReward` (3rd-man quadrant), `NoMansLandPenalty` (don't sit in open center strip) | 2+ / 5+ / **Champion+** / **Champion+** | Multiple rewards shape the "hold formation without stacking" idea |
| Risky boost grab mid-field when ball is centered (opens up defensive squares) | Not directly covered — `BoostPathReward`/`RotationPathBoostDenialReward` reward efficient/role-based routing but don't penalize a boost grab specifically because it vacates a defensive zone | — | Narrow, unverified — see notes below |
| Scenario 1 — Far-post rotation when ball is in the corner | `ActiveSupportingPositionReward` (far-post slot), `DiagonalZoneCoverageReward` | 5+ / Champion+ | Covered |
| Scenario 2 — Prefer ground play over risky air unless a clean shot/pass is on | No direct match | — | See gap notes below |
| Scenario 3 — Closest defender challenges early while teammate shadows/rotates back | `ShadowDefenseVsPossessionReward`, `DefensivePositioningChallengeReward`, `ChallengeAngleQualityReward` | Champion+ / Champion+ / Champion+ | Covered |
| Scenario 4 — Bad pass received: rotate back, don't force the touch | Only generic overcommit penalties apply; no pass-quality-specific gate found | — | See gap notes below |
| Scenario 5 — Pass awareness (only pass if teammate can actually see/receive it) | `PassingSetupReward`, `ReceivingPassReward`, `PassToOpponentPenalty` | 5+ / 5+ / 5+ | Implicitly covered — these are event-gated on an actual completed pass to a real teammate position, so a blind/no-teammate pass simply doesn't score the positive rewards and risks `PassToOpponentPenalty` |
| Scenario 6 — Goal-line coverage while teammate dribbles near net | `GoalpostPositioningReward`, `UncoveredNetPenalty`, `BackboardDefenseReward` | 5+ / 3+ / 6+ | Covered |
| Scenario 7 — Don't center the ball; clear to corners/downfield | `OwnNetCenteringPenalty`, `CornerPlayPenalty` | 5+ / Champion+ (mode-based) | Directly named for this exact behavior |

## The one real gap found: Champion-tier zone rewards are dormant at the current training phase

`RewardConfig.cpp` sets `constexpr TrainingPhase CURRENT_PHASE = TrainingPhase::PHASE_5_DIAMOND;`.
The rewards that most literally implement the article's 3x3-grid "same square / same row" rule —
`AntiLayeringPenalty`, `DefensiveZoneOverlapPenalty`, `NoMansLandPenalty`, `OverflipPenalty`, and
`DiagonalZoneCoverageReward` — are only wired into `GetPhasedRewards()` from **Phase 6 Champion
onward**. They exist in the codebase and are fully implemented, but they are not part of the
reward set actually being trained right now at Diamond.

This is an observation, not a recommendation — whether to phase-gate any of these earlier (and at
what weight) is a call for Nathan, weighed against the gradient-dilution cost of adding more
simultaneous signals to Diamond's existing set (which already includes `DoubleCommitPenalty(100)`,
`LastManOvercommitPenalty`, `ActiveSupportingPositionReward(9.5)`, `GoalsideReward(0.55)`, and
others covering the same territory).

## Secondary, lower-confidence notes (not action items — flagged for a closer look only if Nathan wants to pursue them)

- **Scenario 2 (ground play preference):** no existing reward penalizes choosing a risky air play
  over a safer ground play. This could be a genuine gap, but it would need careful scoping — the
  reward set has a deliberate, heavily-weighted push toward aerial mechanics (`AirDribbleSetupReward`,
  `InvertedAirDribbleReward`, `PredictiveBounceReadReward`, etc.), so a blanket "prefer ground"
  reward risks fighting those signals rather than complementing them.
- **Scenario 4 (bad-pass discipline):** whether `ReceivingPassReward` or `OverflipPenalty` actually
  catch "forcing a touch on an uncontrollable/bad pass" specifically was not verified by reading
  `PassingRewards.h` in full during this pass — noted as an open question, not a confirmed gap.

## Bottom line

Nearly everything in the article is already implemented in the reward set, in most cases by
several overlapping reward classes. `CLAUDE.md` names gradient dilution as the #1 problem with
~85 active rewards and ~5000 total weight already active at Phase 6. No new reward code is
recommended from this article. If Nathan wants to act on anything here, the highest-leverage move
already sitting in the code is deciding whether to phase-gate the Champion-tier zone rewards
(`AntiLayeringPenalty`, `DefensiveZoneOverlapPenalty`, `NoMansLandPenalty`, `OverflipPenalty`,
`DiagonalZoneCoverageReward`) earlier into Diamond — not writing anything new.
