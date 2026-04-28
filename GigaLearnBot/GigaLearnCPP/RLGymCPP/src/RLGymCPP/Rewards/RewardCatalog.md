Problem: Claude Code sessions don't have a persistent "what already exists" reference. Time is spent re-reading headers to avoid duplicate rewards.
Fix: Reward Catalog File
A single RewardCatalog.md that lives in the project and gets updated when rewards are added. Each entry: reward class name, what it fires on, current phase weight, and current status (active / shadow / retired). Claude Code reads this first every session. Nathan updates it in 30 seconds when a reward ships.
This also helps with the gradient dilution problem — you can spot overlapping rewards at a glance before they compound in training.

Edit this file so it lists all existing rewards within RLGymCPP\Rewards folder, their trigger conditions, anti-farming defenses, expected activation rates, expected weights, W&B metric names, "working" signals at 500M steps, and any existing rewards they overlap with. 
This will serve as a reference for all future reward additions and modifications. Please also add a brief description of each reward's purpose and how it contributes to the overall training objectives. We want to include dates and revisions for each reward if possible. 
Also add tags to each reward for grouping, and parent/child rewards that are related to them.

# Reward Catalog
Source of truth for all reward classes. Read this before creating any new reward.
Update Status and Weight columns whenever a reward ships, is retired, or changes phase.

| Class | File | Trigger | Current Weight | Phase | Status |
|---|---|---|---|---|---|
| GoalReward | CommonRewards.h | Goal scored | 250–450 | All | Active |
| VelocityPlayerToBallReward | CommonRewards.h | Car moving toward ball | 0.5–2.0 | 1–3 | Active |
| VelocityBallToGoalReward | CommonRewards.h | Ball moving toward opp goal | 15 | All | Active |
| TouchBallReward | CommonRewards.h | Any ball contact | 5 | 1–2 | Active |
| StrongTouchReward | CommonRewards.h | Ball contact above speed threshold | 25–30 | 1–4 | Active |
| TouchAccelReward | CommonRewards.h | Ball contact with acceleration | 15–20 | 1–4 | Active |
| AirTouchReward | CommonRewards.h | Ball contact while airborne | 15–30 | 2–4 | Active |
| AerialFirstTouchReward | CommonRewards.h | First aerial touch on a play | 25–28 | 2–4 | Active |
| HighBallTouchReward | CommonRewards.h | Touch above height threshold | 7.5–15 | 2–4 | Active |
| JumpTouchReward | CommonRewards.h | Touch during jump | 10 | 1–2 | Active |
| ShotReward | CommonRewards.h | Ball on target toward goal | 15 | 3+ | Active |
| SaveReward | CommonRewards.h | Defensive save | 30 | 3+ | Active |
| AssistReward | CommonRewards.h | Assist on goal | 25 | 3+ | Active |
| BumpReward | CommonRewards.h | Car-car bump | 20 | 3+ | Active (ZeroSum) |
| DemoReward | CommonRewards.h | Demo | 60 | 3+ | Active (ZeroSum) |
| PickupBoostReward | CommonRewards.h | Boost pickup | 5 | 1 | Active |
| PowerSlideControlReward | CommonRewards.h | Powerslide with control | 0.5–0.55 | 1–3 | Active |
| AirReward | CommonRewards.h | Car airborne | 0.25 | 1 | Active |
| FaceBallReward | CommonRewards.h | Car facing ball | 0.3 | 1 | Active |
| VelocityReward | CommonRewards.h | Car speed | 0.4 | 1 | Active |
| FlipResetReward | FlipResetReward.h | Underside contact → HasFlipReset() | See config | 6+ | Active |
| UpsideDownAerialReward | AdvancedRewards.h | Inverted + airborne + boosting | TBD | 5+ | Needs weight review |
| FastAerialReward | AdvancedRewards.h | Fast aerial sequence detection | See file | 4+ | Active |
| DribbleBumpReward | AdvancedRewards.h | Aggressive ground dribble + bump | 2 | 3+ | Active |
| DemoAwarenessReward | AdvancedDemoRewards.h | Demo positioning awareness | 2 | 3+ | Active |
| AirDribbleDemoAssist | AdvancedDemoRewards.h | Air dribble → bump | 8 | 5+ | DEPRECATED (pending AirDribbleBumpConversionReward validation) |
| ProgressiveRotationReward | RotationRewards.h | Phase-gated rotation quality | 5–20 | All | Active |
| RetreatAfterTouchReward | RotationRewards.h | Retreat to rotation after touch | 3–5 | 2+ | Active |
| BoostAwareRotationReward | RotationRewards.h | Rotation with boost awareness | 5–8 | 6+ | Active |
| InstantSpacingReward | RotationRewards.h | Team spacing quality | 1 | 2+ | Active |
| KickoffCornerBoostAssignmentReward | BoostCooperationRewards.h | 3v3 kickoff corner pad assignment | 3.0 | 2+ | Active |
| RotationBoostAwarenessReward | BoostCooperationRewards.h | Passive rotation boost awareness | 0.5 | 3+ | Active |
| RoleBasedBoostPreferenceReward | BoostCooperationRewards.h | Gentle role-based boost positioning | 0.3 | 4+ | Active |
| SimpleOvercommitmentPenalty | SimpleOvercommitmentPenalty.h | Overcommit on ball | 5–30 | 5+ | Active |
| AdvancedOvercommitmentPenalty | AdvancedOvercommitmentPenalty.h | Predictive overcommit | 12–20 | 5+ | Active |
| AdvancedOpponentOvercommitmentReward | AdvancedOpponentOvercommitmentReward.h | Exploit opp lockout | 16–18 | 6+ | Active |
| GoalLineClearanceReward | DefensiveClearingRewards.h | Clear from goal line | 5–15 | 3+ | Active (ZeroSum) |
| BigClearReward | DefensiveClearingRewards.h | Large defensive clear | 10 | 6+ | Active (ZeroSum) |
| CornerClearReward | DefensiveClearingRewards.h | Clear from corner | 3 | 6+ | Active (ZeroSum) |
| BackboardDefenseReward | PositioningRewards.h | Defensive backboard coverage | 5 | 6+ | Active |
| GoalpostPositioningReward | PositioningRewards.h | Goalpost positioning | 10 | 5+ | Active |
| ActiveSupportingPositionReward | PositioningRewards.h | Support position off-ball | 8 | 5+ | Active |
| OwnNetCenteringPenalty | PositioningRewards.h | Penalty for own-net centering | 10–15 | 5+ | Active |
| OpponentPassInterceptionReward | ConfidenceRewards.h | Intercept opponent pass | 10–15 | 5+ | Active (ZeroSum) |
| EarlyDangerDetectionReward | PreemptiveSaveRewards.h | Early danger read | 1.2 | 6+ | Active |
| EmergencySaveCommitReward | PreemptiveSaveRewards.h | Emergency save commit | 4 | 5+ | Active |
| DoubleCommitPenalty | PossessionRewards.h | Both teammates commit | 5 | 6+ | Active |
| PossessionControlReward | PossessionRewards.h | Ball possession maintenance | 15 | 6+ | Active (ZeroSum) |
| ShadowDefenseVsPossessionReward | PossessionRewards.h | Shadow vs possession tradeoff | 1.9 | 6+ | Active (ZeroSum) |
| DefensivePositioningChallengeReward | PositioningRewards.h | Defensive challenge positioning | 2.5 | 6+ | Active |
| PassingSetupReward | PassingRewards.h | Setup for teammate pass | 40 | 5+ | Active (ZeroSum) |
| ReceivingPassReward | PassingRewards.h | Receive pass from teammate | 40 | 5+ | Active (ZeroSum) |
| HighPassReward | PassingRewards.h | High pass to teammate | 10 | 5+ | Active (ZeroSum) |
| ForwardPassReward | PassingRewards.h | Forward pass to teammate | 10 | 5+ | Active (ZeroSum) |
| PassToOpponentPenalty | PassingRewards.h | Pass landing at opponent | 25 | 5+ | Active |
| NumericalAdvantageAttackingReward | AdvancedPlayRewards.h | Attack in numerical advantage | 4–30 | 3+ | Active (ZeroSum) |
| OptionControlReward | OptionControlReward.h | Strategic optionality positioning | 0.5 | 4+ | Active (gating) |
| DribbleSetupReward | KickoffRewards.h | 4-phase dribble setup | See config | 5+ | Active |
| KickoffContactPunishment | KickoffRewards.h | Bad kickoff contact | See config | All | Active |
| KickoffBallPosition | KickoffRewards.h | Kickoff ball positioning | See config | All | Active |
| IntelligentWavedashReward | IntelligentWavedashReward.h | Wavedash execution | See file | 4+ | Active |
| WallDashReward | WallDashReward.h | Wall dash mechanic | See file | 4+ | Active |
| HookshotReward | HookshotReward.h | Hookshot shot | See file | 5+ | Active |

## Status Key
- **Active** — live in current phase reward set
- **Shadow** — logging only, 0.0f return (validation in progress)
- **Retired** — was active, removed; kept for reference
- **Needs weight review** — active but weight hasn't been validated this phase

---

# Phase 6 Champion (Updated 2026-04-05)

Complete reward list for Phase 6 Champion training. This is the authoritative source for current active rewards and their weights.

## Core Mechanics & Ball Touch

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| StrongTouchReward | 40.0f | MECHANICS | Powerful touch (110-160 UU) | Yes | Stricter thresholds for Champion |
| TouchAccelReward | 90.0f | MECHANICS | Touch with acceleration | Yes | High weight for quality contact |
| PowerSlideControlReward | 0.55f | MECHANICS | Powerslide with control | No | Technique shaping |
| EnhancedVelocityBallToGoalReward | 25.0f | OFFENSE | Ball toward goal (improved formula) | Yes | Replaces legacy VelocityBallToGoal |

## Aerial & Advanced Mechanics

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| AerialFirstTouchReward | 60.0f | MECHANICS | First touch airborne | Yes | Primary aerial contact |
| FirstToBallReward | 10.0f | POSITIONING | Control initial contact | Yes | Fast ball race |
| PitchControlAerialReward | 5.0f | POSITIONING | Pitch control via aerial | Yes | Aerial dominance |
| AirDribbleSetupReward | 7.0f | MECHANICS | Air dribble setup | Yes | Transition to air dribble |
| UnifiedAirDribbleReward | (disabled) | MECHANICS | Air dribble execution | Yes | Replaced by FlipReset focus |
| AirDribbleBumpReward | 20.0f | MECHANICS | Bump during air dribble | Yes | DEPRECATED (pending AirDribbleBumpConversionReward validation) |
| CompositeFlickReward | 30.0f | MECHANICS | Flick variety (Advanced+Backflip+Musty+Exploration) | Yes | Multi-flick rewards combined |
| FlickOutplayReward | 30.0f | MECHANICS | Dribble flick vs opponent | Yes | Context-aware flick |
| EnhancedSpeedFlipReward | 15.0f | MECHANICS | Speed flip quality | No | Recovery technique |
| SupersonicReward | 4.0f | MECHANICS | Supersonic achievement | No | Speed bonus |

## Recovery & Movement

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| ProperLandingReward | Phase 6: 1.5f | MECHANICS | Multi-wheel landing (airtime ≥0.2s, 2+ wheels) | No | 2026-04-20: 1.0→1.5 (+50%) |
| AdvancedRecoveryReward | 0.4f | MECHANICS | Recovery quality (powerslide landing) | No | Keep at 0.4f; powerslide gate is narrow |
| BumpedFlipRecoveryReward | Phase 6: 0.3f | MECHANICS | Recovery after bump/collision | No | 2026-04-20: 0.2→0.3 (+50%) |
| WavedashBasicReward | Phase 6: 0.45f | MECHANICS | Flip→ground (non-kickoff) | No | 2026-04-20: 0.3→0.45 (+50%) |
| OffBallAerialPenalty | Phase 6: 10.0f | PENALTY | Airborne >0.5s AND ball >3000 UU; 2x if boost <30 | No | 2026-04-20: boost-scaling added |
| SecondaryAerialPenalty | Phase 6: 8.0f, Phase 7+: 8.0f | PENALTY | Airborne during aerial (ball >250 UU) when teammate closer to ball | No | 2026-04-20: NEW. 0.3s grace, 150 UU slack; 1v1 immune |

## Kickoff

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| KickoffSpeedReward | 10.0f | KICKOFF | Speed achievement | No | Primary kickoff signal |
| ClosestPlayerKickoffSideReward | 15.0f | KICKOFF | Closest player positioning | No | Strategic kickoff placement |
| KickoffDoubleCommitPenalty | 15.0f | KICKOFF | Both teammates commit | No | Prevents bad kickoffs |
| SpeedflipKickoffReward | RETIRED | KICKOFF | Speed flip execution | No | RETIRED 2026-04-26 → replaced by SpeedFlipKickoffReward |
| SpeedFlipKickoffReward | 2.0f | KICKOFF | 3-phase: flip detection + contact quality | No | Kickoff/SpeedFlip/FlipDetected, /ContactSuccess |
| DiagonalBoostKickoffReward | 1.5f | KICKOFF | Corner boost grab + commit + contact | No | Kickoff/DiagBoost/PartialReward, /FullReward |
| DelayedKickoffReward | 1.5f | KICKOFF | Intentional delay → opponent commits first | No | Kickoff/Delayed/Success |
| FakeKickoffReward | 2.0f | KICKOFF | Sell approach → abort → exploit | No | Kickoff/Fake/Success |
| ReactiveKickoffReward | 1.5f | KICKOFF | Mid-kickoff read: counter speedflip/fake/delay | No | Kickoff/Reactive/ScenarioA, /B, /C |
| PredictiveSecondManReward | 1.5f | KICKOFF | Second man pre-positions for predicted outcome | No | Kickoff/SecondMan/CorrectRead, /PrereadBonus |
| SpanishDemoKickoffReward | 2.0f | KICKOFF | Second man demos opp kicker (2v2+) | No | Kickoff/SpanishDemo/DemoFired, /PayoffSuccess |
| KickoffSupportBoostReward | 6.0f | KICKOFF | Support grabs corner boost | No | 2v2+ support play |
| AntiLayeringPenalty | 15.0f | POSITIONING | Per-step penalty when teammate within ±200 Y units (2v2/3v3 only, exempt near own goal) | No | Penalty/AntiLayering/PenaltyFrames, /AvgPairsPerFrame |
| DefensiveZoneOverlapPenalty | 15.0f | POSITIONING | Per-step penalty when 2+ teammates in same X-zone (left/center/right) while ball is in own defensive third (2v2/3v3 only, exempt if near net) | No | Penalty/DefZoneOverlap/PenaltyFrames, /AvgPairsPerFrame |
| OverflipPenalty | 8.0f | POSITIONING | One-shot penalty when non-first-man flips toward ball within 3000 UU — overcommit flip detection | No | Penalty/Overflip/Count, /AvgDistToBall |
| DiagonalZoneCoverageReward | 3.0f | POSITIONING | Continuous reward for third man (furthest from ball) being in the quadrant diagonal to the ball's quadrant (2v2/3v3, skip when ball in def third or near axes) | No | Positioning/DiagonalZone/CorrectFrames, /TotalEligible |
| NoMansLandPenalty | 8.0f | POSITIONING | Per-step penalty for non-first-man sitting in center field strip (|x| < 2000) after 0.3s grace; exempt if near own goal or ball (2v2/3v3 only) | No | Penalty/NoMansLand/PenaltyFrames |

## Boost Management

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| PickupBoostReward | 50.0f | BOOST | Pad collection | No | PRIMARY boost incentive |
| SaveBoostReward | 0.65f | BOOST | Boost conservation | No | Efficient usage |
| SmallPadCollectionReward | 6.0f | BOOST | Small pad pickup | No | Route efficiency |
| BoostPathReward | 20.0f | BOOST | Efficient pad routing | No | Continuous collection |
| BigBoostPadCenterReward | 15.0f | BOOST | Center big pad | No | Arena control |
| DefensiveRotationBoostReward | 14.0f | BOOST | Boost in defensive rotation | No | Positioning consideration |
| SupersonicBoostPenalty | 25.0f | PENALTY | Supersonic without boost | No | Efficiency penalty |
| BoostWastePenaltyReward | 20.0f | PENALTY | Boost waste | No | Efficiency penalty |
| BoostStealReward | 120.0f | OFFENSE | Steal opponent boost | Yes | Denial reward |
| BoostStarvationReward | 2.5f | OFFENSE | Opponent low boost | Yes | Exploitation reward |

## Shooting & Scoring

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| GoalReward | 1250.0f | OUTCOME | Goal scored / conceded | No | PRIMARY outcome signal |
| GoalMultiplierReward | (disabled) | OFFENSE | Goal style bonus (1.0-3.0x) | Yes | Disabled; too dilute |
| OwnGoalPenalty | 800.0f | OUTCOME | Own goal | No | Severe penalty |
| ShotPlacementReward | 80.0f | SHOOTING | Shot accuracy | No | On-target placement |
| SpeedScaledShotReward × ShotAccuracyInBoundsReward | 100.0f | SHOOTING | Speed × accuracy | Yes | Composite shot quality |
| AirTouchReward × ShotAccuracyInBoundsReward | 30.0f | SHOOTING | Aerial shot quality | Yes | Composite aerial shot |
| BallRedirectReward × ShotAccuracyInBoundsReward | 40.0f | SHOOTING | Redirect accuracy | Yes | Composite deflection |
| BackboardReboundReward | 40.0f | SHOOTING | Offensive backboard play | Yes | Creative scoring |
| DoomseeDisshShotReward | 20.0f | SHOOTING | Dooms-style dissh | Yes | Advanced mechanic |
| SoloPlayComboReward | 6.0f ZeroSum | OFFENSE | 2+ distinct touches by same player (400 UU + 0.25s gap) → goal-threatening shot within 1.5s; any other player touch resets sequence | Yes | SoloPlay/Combo/* |
| DualThreatReward | 5.0f ZeroSum | OFFENSE | Ball carrier with goal-directed ball AND unmarked teammate in scoring zone (2v2/3v3 only, per-step 0.05f) | Yes | SoloPlay/DualThreat/* |

## Defense & Clearing

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| SaveReward | 50.0f | DEFENSE | Save execution | Yes | Primary defensive reward |
| BigClearReward | 8.0f | DEFENSE | Large clear (distance/speed) | Yes | Defensive output |
| GoalLineClearanceReward | 10.0f | DEFENSE | Goal line clearance | Yes | Last-line defense |
| BackboardDefenseReward | 6.0f | DEFENSE | Defensive backboard | No | Backboard coverage |
| DefensivePositionReward | 3.0f | POSITIONING | Defensive positioning quality | No | Spatial awareness |
| DefensivePositioningChallengeReward | 1.5f | POSITIONING | Challenge during defense | No | Aggressive defense |
| TimeToBalPossessionReward | 2.5f | OFFENSE | First to ball (race) | Yes | Possession initiation |

## Penalties & Overcommitment

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| DoubleCommitPenalty | 50.0f | PENALTY | Both teammates commit | No | Prevents clustering |
| SimpleOvercommitmentPenalty | 25.0f | PENALTY | Overcommit on ball | No | Presence discipline |
| OwnNetCenteringPenalty | 40.0f | PENALTY | Overly centered in net | No | Positioning discipline |
| PassToOpponentPenalty | 50.0f | PENALTY | Pass to opponent | No | Ball security |
| CornerPlayPenalty | 30.0f | PENALTY | Poor corner play | No | Situational discipline |
| LowBoostOppHalfFiftyPunish | 5.0f | PENALTY | Low boost in opp half | No | Risk management |

## Offensive Play & Demos

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| BumpReward | 100.0f | OFFENSE | Opponent bump (non-ZeroSum) | No | Offensive impact |
| DemoReward | 350.0f | OFFENSE | Opponent demo (non-ZeroSum) | No | Aggressive play |
| DemoReward (ZeroSum) | 500.0f | OFFENSE | Opponent demo (ZeroSum) | Yes | Defensive value |
| SecondManOffensiveDemoReward | 140.0f | OFFENSE | 2nd-man demo (MIDDLE role) | No | Coordinated aggression |
| PossessionSideDemoReward | Phase 5: 5.0, Phase 6: 12.0, Phase 7+: 15.0 | TEAMWORK | Demo/bump last defender during team possession in attacking half | Yes (ZeroSum 1.0) | Tactical possession-side demo |
| NumericalAdvantageAttackingReward | 50.0f | OFFENSE | Attack in 2v1+ | Yes | Numeric leverage |
| PossessionControlReward | 25.0f | OFFENSE | Ball possession | Yes | Offensive control |
| AirDribbleBumpConversionReward | 0.0f (shadow) / 10.0f (live) | MECHANICS | Air dribble ≥2 touches → bump nearest defender while ball goal-bound → confirm shot survives 0.5s | Yes (ZeroSum 0.5f) | Phase 6+ Shadow Mode M2. Supersedes AirDribbleBumpReward, AirBumpDemoReward, AirDribbleDemoAssist. Rev 1.0 2026-04-21. |
| AirDribbleExplorationReward | 1.5f | MECHANICS | Airborne ball contact at non-standard orientation (inverted/sideways), unusual touch timing (multi-tap <0.25s or delayed >2s), or high power delta >1200 UU/s | No | Phase 6+. 3 channels: orientation diversity, touch rhythm, power air touch. Complements AirDribbleAccuracyReward. Rev 1.0 2026-04-21. |
| FlickVarietyReward | 4.0f | MECHANICS | State machine: carry → flip detected → ball separated. Scores side/behind/underside ball position, inverted/sideways orientation, flash (<0.15s) or patient (>1.5s) carry timing | No | Phase 6+. No bonus for standard front flicks (covered by CompositeFlickReward). 3.0s cooldown. Rev 1.1 2026-04-22 (renamed from FlickExplorationReward to avoid conflict with CompleteMechanicsRewards.h). |
| RecoveryExplorationReward | 1.0f | CAR CONTROL | 4 sub-rewards: ceiling wavedash, wall-to-wall wavedash, squishy save momentum (driving through net curve), boost-to-surface (<0.6s contact after boost) | No | Phase 6+. Complements IntelligentWavedashReward (ground/wall) and WavedashBasicReward. Rev 1.0 2026-04-21. |
| OutplayExplorationReward | 3.0f | MECHANICS | Ball touched + opponent threatening (within 1800 UU, 450+ UU/s toward ball or flipping) → 3 channels: aerial outplay (airborne + redirect score >0.25), wall outplay (on wall + redirect >0.2), sharp redirect (any touch redirect score >0.65) | No | Phase 6+. Fills aerial/wall gaps left by FlickOutplayReward (ground flick) and CutOutplayReward (ground z<260). Rev 1.0 2026-04-24. |
| OutplayCreativityReward | 4.0f | MECHANICS | Ball touched + opponent threatening + redirect score >0.2 → 2 channels: inverted/sideways car orientation, low-boost (<30) commitment | No | Phase 6+. Rewards unusual car state during outplay touches. Paired with OutplayExplorationReward. Rev 1.0 2026-04-24. |
| ShotExplorationReward | 3.0f | MECHANICS | Ball touched → shot on goal → 3 channels: wide-angle (|vx/vy|>0.35), height variety (aerial >400 UU / low <100 UU), field position (own half/corner/wall) | No | Phase 6+. Complements ShotPlacementReward (defender coverage) — measures WHERE bot shoots from and HOW angled. Independent 2s cooldown per channel. Rev 1.0 2026-04-24. |
| ShotCreativityReward | 4.0f | MECHANICS | Ball touched → shot on goal → 3 channels: inverted/sideways car orientation, redirect geometry (car-ball vel divergence), first-touch shot (no touch in last 0.8s) | No | Phase 6+. Rewards unusual car state at shot moment. Shared IsShotOnGoal gate. Independent cooldowns: orient 2.5s, redirect 2.0s, first-touch 3.0s. Rev 1.0 2026-04-24. |

## Predictive Demo Rewards (Shadow Mode — Phase 6)

All 7 classes are in `PredictiveDemoRewards.h`. All registered with `shadowMode=true` in Phase 6 Champion. Zero policy impact until shadow wrapper is removed. W&B metrics under `PredictiveDemo/*` namespace.

| Class | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| MomentumLeadDemoReward | 2.5f | PREDICTION | Demo opponent AFTER tracking their predicted future position (lead alignment > current alignment + 0.05) with confirmed lead | No | Phase 5+. Scales with opp speed. If DemosLed=0 at 50M steps, lower MIN_LEAD_ALIGNMENT to 0.45. 4s cooldown. Rev 1.1 2026-04-25. |
| BallChaserAmbushReward | 2.0f | PREDICTION | Demo opponent while ball-chasing (chase_align > 0.72) while unaware of bot (vel toward bot < 0.40) | No | Phase 4+. Two quality signals multiplied: chase lock × unawareness. 4s cooldown. Rev 1.1 2026-04-25. |
| PostTouchShadowDemoReward | 2.0f | PREDICTION | Demo opponent within 2s of their ball touch, at predicted follow-through vector. 1.5× pre-read bonus if bot was already heading there before the touch | No | Phase 4+. KEY metric: PreReadBonusFired — watch ratio vs BaseRewardFired. 4s cooldown. Rev 1.1 2026-04-25. |
| AerialVulnerabilityDemoReward | 2.5f | PREDICTION | Demo airborne opponent committed to aerial (vel→ball > 0.55, ball z > 400 UU). Scales with height + flip bonus | No | Phase 5+. Bot must also be airborne (not ground-sniping). Flip tier adds 1.0 bonus. 4s cooldown. Rev 1.1 2026-04-25. |
| RespawnHuntReward | 1.5f | PREDICTION | Demo opponent → route to their spawn zone (routedToSpawn gate) → demo again within 3.5s of respawn. Partial 0.5f for ambush presence only | No | Phase 5+. Monitor for spawn-camp obsession. routedToSpawn is critical anti-farming gate. 6s cooldown. Rev 1.1 2026-04-25. |
| FunnelExitDemoReward | 2.0f | PREDICTION | Ball in corner → bot positions at exit path (1400 UU from corner) → demo opponent within 2s of ball leaving corner. Timeliness multiplier rewards early intercept | No | Phase 4+. If AtExitPosition/CornerEntries low, check AMBUSH_CONE (700 UU). 3s cooldown. Rev 1.1 2026-04-25. |
| DoubleCommitDemoReward | 2.0f | PREDICTION | In 2v2/3v3: ALL opponents committed to ball (vel→ball > 0.60, within 2800 UU) with no defender; bot is 2nd man (not closest to ball) | No | Phase 5+, 2v2/3v3 only (returns 0 in 1v1). Start at very low weight — highest potential for weird emergent behaviors. 5s cooldown. Rev 1.1 2026-04-25. |

## Positioning & Rotation

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| InstantSpacingReward | 4.0f | POSITIONING | Team spacing to formation | No | Formation control |
| ProgressiveRotationReward | 15.0f | POSITIONING | Progressive rotation (Champion tier) | No | Rotation quality |
| BoostAwareRotationReward | 3.0f | POSITIONING | Rotation + boost awareness | No | Contextual rotation |
| RetreatAfterTouchReward | 2.5f | POSITIONING | Retreat after touch | No | Rotation prep |
| ActiveSupportingPositionReward | 10.0f | POSITIONING | Support position upfield | No | 2nd-man holding |
| GoalpostPositioningReward | 8.0f | POSITIONING | Goalpost coverage | No | Defensive coverage |
| ShadowDefenseVsPossessionReward | 3.0f | POSITIONING | Shadow vs possession trade | Yes | Tactical balance |

## Teamwork & Passing

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| PassingSetupReward | 36.0f | TEAMWORK | Setup for teammate pass | Yes | Passing opportunity |
| ReceivingPassReward | 18.0f | TEAMWORK | Receive & use pass | Yes | Receiving effectiveness |
| AssistReward | 30.0f | TEAMWORK | Goal assist | Yes | Passing payoff |
| PassToOpponentPenalty | 50.0f | PENALTY | Bad pass | No | Ball security |
| PassingLaneNetworkReward | 7.0f | TEAMWORK | Passing lane control (team modes) | No | Spatial network (2v2/3v3 only) |
| InfieldPassReward | 15.0f | TEAMWORK | Lateral pass | No | Pass quality |
| DownfieldPassReward | 15.0f | TEAMWORK | Long downfield pass | No | Tactical passing |
| SpatialFakeReward | 5.0f | OFFENSE | Spatial fake maneuver | No | Deception play |

## Dribbling

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| DribbleReward | 1.0f | MECHANICS | Dribble maintenance | Yes | Ball retention |
| DribbleMasteryReward | 4.0f | MECHANICS | Advanced dribble techniques | Yes | Control quality |
| CatchBallOnCarReward | 1.0f | MECHANICS | Ball caught on roof | Yes | Setup for dribble |
| DribbleSetupReward | 1.0f | MECHANICS | Setup for dribble | Yes | Positioning |

## Defense & Interception

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| OpponentPassInterceptionReward | 15.0f | DEFENSE | Intercept opponent pass | Yes | Aggressive defense |
| EarlyDangerDetectionReward | (disabled) | DEFENSE | Early danger read | No | Replaced by contextual rewards |
| EmergencySaveCommitReward | 2.5f | DEFENSE | Emergency save commitment | No | Last-ditch save |

## Disabled / Phase 5 Only

| Reward | Phase 5 Weight | Category | Reason | Notes |
|---|---|---|---|---|
| HalfFlipReward | 2.0f | MECHANICS | Deprecated | Covered by EnhancedSpeedFlip |
| WavedashBasicReward | Phase 6: 0.45f | MECHANICS | Active — see Recovery & Movement section | — |
| ReverseAerialReward | 9.0f | MECHANICS | Deprecated | Niche mechanic |
| HighBallTouchReward | 15.0f | MECHANICS | Deprecated | Covered by AerialFirstTouch |
| WallTouchReward | 20.0f | MECHANICS | Deprecated | Low priority |
| WallShotReward | 20.0f | MECHANICS | Deprecated | Covered by shot composites |
| WallPinchReward | 10.0f | MECHANICS | Deprecated | Niche mechanic |
| CeilingShotReward | 12.0f | MECHANICS | Deprecated | Advanced variant |
| FlipResetApproachReward | (Phase 5 focus) | MECHANICS | Phase 6: replaced by SimpleFlipReset | See Phase 6 for current flip reset approach |
| UnifiedAirDribbleReward | 50.0f | MECHANICS | Deprecated | Replaced by setup+bump combo |
| VelocityBallToGoalReward | 30.0f | OFFENSE | Deprecated | Replaced by EnhancedVelocityBallToGoal |
| AirRollShotReward | 6.0f | SHOOTING | Deprecated | Low activation |
| HookShotReward | 5.0f | SHOOTING | Deprecated | Niche mechanic |
| DirectionChangeReward | 15.0f | OFFENSE | Deprecated | Low priority |
| CutOutplayReward | 15.0f | OFFENSE | Deprecated | Context unclear |
| FakeChallengeOffenseReward | 1.5f | OFFENSE | Deprecated | Low priority |
| AerialCommitmentReward | 15.0f | COMMITMENT | Deprecated | Redundant with activation |
| ChallengeCommitmentReward | 15.0f | COMMITMENT | Deprecated | Redundant |
| KickoffSpeedReward (Phase 5) | 20.0f | KICKOFF | Replaced | Replaced by SpeedflipKickoff |
| KickoffRandomizationReward | 15.0f | KICKOFF | Deprecated | Low priority |
| KickoffNoChaseTeamPenalty | 15.0f | KICKOFF | Deprecated | Covered by DoubleCommit |
| CornerClearReward | 3.0f | DEFENSE | Deprecated | Low priority |
| EarlyDangerDetectionReward | 1.0f | DEFENSE | Deprecated | Replaced by contextual signals |
| AdvancedOvercommitmentPenalty | 8.0f | PENALTY | Phase 5 only | Simplified in Phase 6 |
| GoalMultiplierReward | 350.0f | OFFENSE | Deprecated in Phase 6 | Caused gradient dilution |
| FirstToBallReward (Phase 5) | 8.0f | POSITIONING | Still active | Updated for Phase 6 |
| BumpReward (ZeroSum) | 30.0f | OFFENSE | Still active | Now split between ZeroSum + non |
| UnifiedDoubleTapReward | 80.0f | MECHANICS | Phase 6 deprecated | Advanced mechanic, deprioritized |
| PostKickoffMidBoostReward | 15.0f | KICKOFF | Phase 6 deprecated | Low priority |

---

## Summary Statistics

- **Total Active Rewards (Phase 6):** ~85 (including composites)
- **Unique Reward Classes:** ~70
- **Total Signal Weight (approximate):** ~5000+
- **ZeroSum Rewards:** ~35
- **Non-ZeroSum Rewards:** ~50
- **Highest Weight:** GoalReward (1250.0f)
- **Lowest Active Weight:** PowerSlideControl (0.55f)

---

## How to Use This Catalog

1. **Before proposing a new reward:** Search this page for overlaps.
2. **When modifying a weight:** Document the change in `Reward_Change_Log.md` with date, reason, and expected outcome.
3. **When deprecating a reward:** Move it to "Disabled" section, update RewardConfig.cpp, and log it.
4. **When promoting from shadow:** Update Status here and in RewardConfig.cpp simultaneously.

---

## Key References

- **RewardConfig.cpp** (lines 630-834): Source of truth for Phase 6 Champion weights
- **Reward_Change_Log.md**: Audit trail for every weight change, enable/disable
- **CLAUDE.md Section 7A**: Work Type Reference (reward patterns by category)
- **CLAUDE.md Section 7B**: Mechanic Spec Template (required pre-implementation spec)