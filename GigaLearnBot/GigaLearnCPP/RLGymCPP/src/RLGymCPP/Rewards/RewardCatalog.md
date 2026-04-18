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
| AirDribbleDemoAssist | AdvancedDemoRewards.h | Air dribble → bump | 8 | 5+ | Active |
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
| AirDribbleBumpReward | 20.0f | MECHANICS | Bump during air dribble | Yes | Aggressive air play |
| CompositeFlickReward | 30.0f | MECHANICS | Flick variety (Advanced+Backflip+Musty+Exploration) | Yes | Multi-flick rewards combined |
| FlickOutplayReward | 30.0f | MECHANICS | Dribble flick vs opponent | Yes | Context-aware flick |
| EnhancedSpeedFlipReward | 15.0f | MECHANICS | Speed flip quality | No | Recovery technique |
| SupersonicReward | 4.0f | MECHANICS | Supersonic achievement | No | Speed bonus |

## Recovery & Movement

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| ProperLandingReward | 0.75f | MECHANICS | Multi-wheel landing | No | Smooth recovery |
| AdvancedRecoveryReward | (disabled) | MECHANICS | Recovery quality | No | Replaced by enhanced variants |
| BumpedFlipRecoveryReward | (disabled) | MECHANICS | Recovery after bump | No | Rare edge case |

## Kickoff

| Reward | Weight | Category | Trigger | ZeroSum? | Notes |
|---|---|---|---|---|---|
| KickoffSpeedReward | 10.0f | KICKOFF | Speed achievement | No | Primary kickoff signal |
| ClosestPlayerKickoffSideReward | 15.0f | KICKOFF | Closest player positioning | No | Strategic kickoff placement |
| KickoffDoubleCommitPenalty | 15.0f | KICKOFF | Both teammates commit | No | Prevents bad kickoffs |
| SpeedflipKickoffReward | 20.0f | KICKOFF | Speed flip execution | No | Technical kickoff bonus |
| KickoffSupportBoostReward | 6.0f | KICKOFF | Support grabs corner boost | No | 2v2+ support play |

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
| NumericalAdvantageAttackingReward | 50.0f | OFFENSE | Attack in 2v1+ | Yes | Numeric leverage |
| PossessionControlReward | 25.0f | OFFENSE | Ball possession | Yes | Offensive control |

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
| WavedashBasicReward | 0.5f | MECHANICS | Deprecated | Low priority |
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