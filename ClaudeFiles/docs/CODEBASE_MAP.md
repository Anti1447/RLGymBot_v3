# Codebase Map — Detailed Reference

Deeper detail than belongs in `CLAUDE.md` itself. See `CLAUDE.md` Section 1A
for the compact category map and mega-file/config-anchor tables — start
there. This file is the backing detail: per-file class listings for the
`Rewards/` directory, the `GigaLearnCPP/src/` tree, `RLBotCPP/`/`rlbot/`
structure, and the `python_scripts/` triple-copy situation.

Line counts and class names below are a snapshot from 2026-07-22 — they
drift as files are edited. Treat this as a starting point for `Grep`, not
a guarantee.

## Rewards/ — per-file class listing

Single/few-class files (one line each, alphabetical):

- `AdvancedDemoRewards.h` (981) — `ContextualDemoReward`, `FailedDemoAttemptPenalty`, `SpawnCampingPenalty`, `SoloDefenderBumpReward`, `AirDribbleDemoAssist` (deprecated), `SecondManOffensiveDemoReward`, `MasterDemoTacticsReward`, `StrategicDemoReward`, `InterceptClearingDemoReward`, `ThirdManDemoReward`
- `AdvancedKickoffRewards.h` (1057) — `SpeedFlipKickoffReward`, `DelayedKickoffReward`, `FakeKickoffReward`, `ReactiveKickoffReward` (retired), `PredictiveSecondManReward`, `DiagonalBoostKickoffReward`, `SpanishDemoKickoffReward`
- `AdvancedOpponentOvercommitmentReward.h` (211) — single class
- `AdvancedOvercommitmentPenalty.h` (209) — single class
- `AdvancedPlayRewards.h` (645) — `AdvancedDoubleTapReward`, `WallPassReward`, `WallToAerialReward`, `MasterAdvancedPlayReward`
- `AerialOutplayReward.h` (237) — single class
- `AerialRewards.h` (701) — `DoubleJumpAerialReward`, `PitchControlAerialReward`, `AirDribbleSetupReward`, `InvertedAerialReward`
- `AirDribbleAccuracyReward.h` (544) — single class `AirDribbleAccuracyReward`
- `AirDribbleBumpConversionReward.h` (448) — single class (shadow mode)
- `AirDribbleBumpReward.h` (242) — single class (deprecated)
- `AirDribbleClearingReward.h` (463) — single class
- `AirDribbleExplorationReward.h` (153) — single class
- `AirDribbleUtilities.h` (108) — shared helper functions, not a Reward
- `AirRollMultiplierReward.h` (107) — `AirRollMultiplierGate`
- `AirborneOnPossessionLostPenalty.h` (76) — single class
- `AngleFlickRewards.h` (259) — `AngleFlickRewardBase`, `AngleFlick45Reward`, `AngleFlick90Reward`
- `BackboardInterceptionReward.h` (91) — single class
- `BallGoalProximityReward.h` (57) — single class
- `BallPrediction.h` (252) — shared ball-trajectory prediction utility (`BallPred::Cache`), not a Reward
- `BallRaceAwarenessReward.h` (81) — `OpponentInterceptAwarePenalty`
- `BallSideNumericalAdvantageReward.h` (113) — single class
- `BallSteerReward.h` (202) — single class
- `BoostCooperationRewards.h` (280) — corner-boost/rotation cooperation rewards
- `BoostManagementRewards.h` (2066) — see mega-file section below
- `BoostStealReward.h` (129) — single class
- `ChallengeAngleQualityReward.h` (100) — single class
- `CommonRewards.h` (1766) — see mega-file section below
- `CompleteMechanicsRewards.h` (7818) — see mega-file section below
- `CompositeFlickReward.h` (547) — combined flick-variety reward
- `ConfidenceRewards.h` (644) — `HesitationPunishment`, `ChallengeCommitmentReward`, `FirstToBallReward`, `ImmediateShotReward`, `ExcessiveShadowPunishment`, `AerialCommitmentReward`, `MomentumMaintenanceReward`, `EarlyAerialReward`, `OpponentPassInterceptionReward`, `FiftyAggressionReward`
- `CounterAttackReward.h` (202) — single class
- `CounterLaunchDiagonalReward.h` (331) — single class (shadow mode, shares `ThirdManContext`)
- `DefensiveClearingRewards.h` (122) — `BigClearReward`
- `DemoAdvantageReward.h` (167) — single class
- `DemoInterceptLeadReward.h` (126) — single class
- `DemoLeadApproachReward.h` (67) — single class
- `DemoOpportunityReward.h` (75) — single class
- `DoomseeDisshShotReward.h` (320) — single class — back-wall roll → drop → flip-conversion shot
- `DoubleJumpPopReward.h` (344) — single class
- `DribbleFlickRewards.h` (169) — `DribbleFlickRewardBase`, `FrontFlipFlickReward`, `DiagonalFlickReward`, `SideFlickReward`
- `FlickExplorationReward.h` (174) — `FlickVarietyReward` (renamed to avoid clash with `CompleteMechanicsRewards.h`'s own `FlickExplorationReward` class)
- `FlipDuringContestPenalty.h` (80) — single class
- `FlipResetApproachReward.h` (334) — single class
- `FlipResetMetricsCollector.h` (380) — utility/metrics class, not a Reward
- `FlipResetReward.h` (548) — 3-layer flip reset system (validator/classifier/reward)
- `GodTierMovementRewards_Enhanced.h` (573) — `EnhancedSpeedFlipReward`, `EnhancedZapDashReward`, `EnhancedMenaFlipReward`, `EnhancedWallDashReward`
- `GoalMultiplierReward.h` (200) — single class (disabled — "too dilute")
- `GoalViewReward.h` (133) — single class (Rolv port; unfarmable potential-based)
- `GoalsideReward.h` (73) — single class
- `GroundedAtContactReward.h` (72) — single class
- `HookshotReward.h` (350) — `DirectionChangeReward`, `HookShotReward` (see also `HookShotNotes/HookshotIntegrationGuide.md`)
- `ImpendingShotDefenseReward.h` (139) — single class
- `IntelligentWavedashReward.h` (295) — single class
- `InvertedAirDribbleReward.h` (352) — single class
- `KickoffCounterReward.h` (191) — `KickoffCounterHitReward`
- `KickoffCounterpositionReward.h` (205) — single class
- `KickoffNoChaseTeamPenalty.h` (178) — single class
- `KickoffRandomizationReward.h` (221) — single class (deprecated/low priority)
- `KickoffRewards.h` (1382) — `KickoffSpeedReward`, `KickoffFirstTouchReward`, `KickoffFormReward`, `KickoffReward`, `KickoffContactPunishmentReward`, `KickoffBallPositionReward`, `KickoffCompositeReward`, `FlipDuringKickoffReward`, `KickoffFlipReward`
- `KickoffStrategyReward.h` (293) — single class
- `LastManOvercommitPenalty.h` (105) — single class
- `LateralPushDribbleReward.h` (378) — single class
- `MovementAndKickoffRewards_Enhanced.h` (393) — `LandingQualityReward`, `EnhancedKickoffReward`, `MovementAndKickoffCombinedReward`
- `MultiModalRewards.h` (928) — `DoubleCommitPenalty`, `MiddleRotationReward`, `GoodRecoveryReward`, `TeammteBumpPenalty`, `PassingSetupReward`, `ReceivingPassReward`, `PassingAccuracyReward`
- `NonPrimaryAirPenalty.h` (79) — single class
- `OpenNetPushReward.h` (84) — single class
- `OptionControlReward.h` (321) — single class
- `OptionGatingWrappers.h` (189) — `ScoringRewardWrapper`, `PassingRewardWrapper`, `OptionGatingWrapper`
- `OutplayExplorationReward.h` (247) — `OutplayExplorationReward`, `OutplayCreativityReward`
- `OutplayRewards.h` (1545) — `FlickOutplayReward`, `FlickOnGoalReward`, `CutOutplayReward`, `FakeChallengeOffenseReward`, `FakeChallengeDefenseReward`, `FlickShotReward`
- `PassingRewards.h` (690) — `BackPassReward`, `ForwardPassReward`, `HighPassReward`, `OffensivePlayReward`, `InfieldPassReward`, `DownfieldPassReward`, `PressureEscapeBackPassReward`
- `PlayerReward.h` (47) — template wrapper `PlayerReward<T>`
- `PositionAnalysis.h` (338) — helper class (overcommitment detection utilities)
- `PositioningRewards.h` (1195) — `CornerPlayPenalty`, `CenterPlayReward`, `OwnNetCenteringPenalty`, `ActiveSupportingPositionReward`, `DeepBackPostCoverageReward`, `KickoffSupportBoostReward`, `AntiLayeringPenalty`, `DefensiveZoneOverlapPenalty`, `OverflipPenalty`, `DiagonalZoneCoverageReward`, `NoMansLandPenalty`
- `PossessionRewards.h` (710) — `TimeToBalPossessionReward`, `ContinuousControlledTouchReward`, `DelayedControlledTouchOutcomeReward`, `MultiAgentPossessionReward`, `OffensivePossessionPositionReward`
- `PossessionSideDemoReward.h` (406) — single class
- `PowerShotRewards.h` (489) — `PowerShotImpactAngleReward`, `PowerShotMassAlignmentReward`, `PowerShotDodgeTimingReward`, `PowerShotContextGate`, `KickoffMassAlignmentReward`
- `PowerslideTurnRewards.h` (317) — `PowerslideSharpTurnReward`, `PowerslideRotationRewardBase`, `Powerslide180ForwardReward`, `Powerslide180ReverseReward`, `Powerslide360Reward`
- `PredictiveBounceReadReward.h` (447) — state-machine pre-bounce-read reward
- `PredictiveDemoRewards.h` (1132) — `MomentumLeadDemoReward`, `BallChaserAmbushReward`, `PostTouchShadowDemoReward`, `AerialVulnerabilityDemoReward`, `RespawnHuntReward`, `FunnelExitDemoReward`, `DoubleCommitDemoReward` (all shadow mode)
- `PreemptiveSaveRewards.h` (844) — `EarlyDangerDetectionReward` (disabled), `ShadowDefenseReward`, `GoalpostPositioningReward`, `BallLandingPositionReward`, `DefensiveReadinessReward`, `GoalLineClearanceReward`, `MultiAirTouchReward`, `PreemptiveSaveCombinedReward`
- `RecoveryExplorationReward.h` (265) — single class (4 sub-channels)
- `RecoveryRewards.h` (1190) — `ReverseRecoveryReward`, `DirectionalFlipRecoveryReward`, `SmallPadRecoveryRouteReward`, `BackflipRecoveryReward`, `PowerslideJumpFlipReward`, `BoostDownRecoveryReward`, `GoalPostRecoveryReward`, `BumpedFlipRecoveryReward`, `ReverseHalfFlipReward`, `WavedashJumpFlipChainReward`, `CeilingDoubleJumpReward`, `GroundToWallFlipReward`
- `Reward.h` (66) — base `class Reward` + `struct WeightedReward`
- `RewardDiagnostician.h` (344) — diagnostic/analysis utility, not a Reward
- `RewardMath.h` (306) — shared goal-geometry/trajectory math helpers, not a Reward
- `RewardMetricsCollector.h` (171) — utility, not a Reward
- `RewardWrapper.h` (236) — base `class RewardWrapper` + `MathRewardWrapper`
- `RolvBallTravelReward.h` (173) — ball-travel port from rlgym-tools
- `RolvDemoReward.h` (140) — single class (port, not yet wired in)
- `RolvVelocityPlayerToBallRewards.h` (232) — `RolvVelocityPlayerToBallReward`, `DotQuotientVPBReward`, `TrajectoryComparisonVPBReward`
- `RotationRewards.h` (1119) — `InstantSpacingReward`, `RetreatAfterTouchReward`, `RoleAwarenessReward`, `SecondManAdvanceReward`, `PassingLaneSetupReward`, `SupportPressureReward`, `ProgressiveRotationReward`, `BoostAwareRotationReward`, `DefensivePositioningChallengeReward`
- `ShootingRewards.h` (996) — `PowerShotReward`, `AccurateShotReward`, `ContestedShotReward`, `BasicOutplayReward`, `OpenGoalShotReward`, `NumericalAdvantageAttackingReward`, `DribbleBumpReward`, `ShotPlacementReward`
- `ShotExplorationRewards.h` (276) — `ShotExplorationReward`, `ShotCreativityReward`
- `SimpleOvercommitmentPenalty.h` (207) — single class, multi-gate
- `SmartWallPlayRewards.h` (784) — `WallClearReward`, `WallShotReward` (deprecated), `WallOutplayReward`, `WallTeamPassReward`, `WallPossessionGiveawayPenalty`, `MasterSmartWallPlayReward`
- `SoloPlayRewards.h` (290) — `SoloPlayComboReward`, `DualThreatReward`
- `SpatialPassingLaneRewards.h` (284) — `PassingLaneNetworkReward`, `SpatialFakeReward`
- `SpecializedFlickRewards.h` (271) — `BackFlipFlickReward`, `MustyFlickReward`
- `ThirdManContext.h` (179) — shared per-step context struct, not a Reward
- `ThirdManStallReward.h` (319) — single class (shadow mode, shares `ThirdManContext`)
- `TornadoSpinTakeoffReward.h` (242) — single class
- `UncoveredNetPenalty.h` (101) — single class
- `WallCeilingRedirectReward.h` (531) — wall/ceiling/corner bounce → aerial redirect
- `WallDashInputReward.h` (100) — single class
- `WallDashReward.h` (113) — single class
- `WallReboundPenalty.h` (174) — `PoorWallReboundPenalty`
- `ZeroSumReward.h`/`.cpp` (26) — `ZeroSumReward` wrapper

### Mega-files (full class list)

- **`CompleteMechanicsRewards.h` (7818 lines, ~75 classes)** — `ProperLandingReward`, `HalfFlipReward`, `PowerSlideControlReward`, `WallHitReward`, `WallPopReward`, `WallPinchReward`, `GroundPinchReward`, `CeilingPinchReward`, `CornerClearReward`, `AirRollShotReward`, `DemoAwarenessReward`, `BackboardReboundReward`, `FiftyFiftyReward`, `EmergencySaveCommitReward`, `ShadowDefenseVsPossessionReward`, `CatchBallReward`, `CatchBallOnCarReward`, `ShotAccuracyReward`, `ShotAccuracyInBoundsReward`, `OffBallAerialPenalty`, `SecondaryAerialPenalty`, `BackboardDefenseReward`, `FakeChallengeReward`, `OutplayReward`, `CeilingShotReward`, `FlickReward`, `AdvancedFlickReward`, `AirDribbleReward`, `UnifiedAirDribbleReward`, `SpeedFlipReward`, `AdvancedRecoveryReward`, `ShotOnGoalReward`, `LongShotReward`, `OpenNetShotReward`, `ShotQualityReward`, `FlickExplorationReward` (name clashes with the standalone file's `FlickVarietyReward` — check which one is actually included before assuming), `DribbleControlReward`, `GroundToAirDribbleReward`, `DemoOffenseReward`, `DemoBumpOutplayReward`, `DemoChasingReward`, `RedirectReward`, `BounceDribbleReward`, `MustyVariationReward`, `EnhancedDoubleTapReward`, `UnifiedDoubleTapReward`, `ReverseAerialReward`, `FieldSpacingReward`, `PossessionControlReward`, `PossessionControlRewardv2`, `HandbrakeAerialReward`, and more — this file alone is ~15% of the directory's total line count. Always `Grep` for the class before reading.
- **`AdvancedRewards.h` (2732 lines, ~24 classes)** — `AirTouchReward`, `AerialFirstTouchReward`, `JumpTouchReward`, `WallTouchReward`, `HighBallTouchReward`, `DribbleMasteryReward`, `DribbleReward`, `DribbleSetupReward`, `BallRedirectReward`, `SaveAttemptReward`, `FastAerialReward`, `HalfFastAerialReward`, `PassReward`, `PassToOpponentPenalty`, `DefensivePositionReward`, `UpsideDownAerialReward`, and others.
- **`BoostManagementRewards.h` (2066 lines, ~23 classes)** — `SmallPadCollectionReward`, `PadCenterAccuracyReward`, `BigBoostApproachReward`, `BoostPathReward`, `BoostStealReward` (name also exists as its own file — check which is registered), `SaveBoostReward`, `SupersonicReward`, `SupersonicBoostPenalty`, `BoostWastePenaltyReward`, `BoostStarvationReward`, `DefensiveRotationBoostReward`, `RotationPathBoostDenialReward`, `KickoffBoostRetentionReward`, and others.
- **`CommonRewards.h` (1766 lines, ~35 classes)** — the "core primitives" file: `GoalReward`, `OwnGoalPenalty`, `VelocityBallToGoalReward`, `EnhancedVelocityBallToGoalReward`, `VelocityPlayerToBallReward`, `TouchBallReward`, `SpeedReward`, `WavedashBasicReward`, `WaveDashHeightReward`, `RolvWavedashReward`, `PickupBoostReward`, `AirReward`, `TouchAccelReward`, `DirectionalAccelerationReward`, `StrongTouchReward`, `ClosestPlayerSpeedReward`, `SpeedflipKickoffReward`, `KickoffDoubleCommitPenalty`, `KickoffCounterReward` (dead/unregistered dup of the standalone file's class, per `RewardCatalog.md`), and others.

## GigaLearnCPP/src/ — PPO learner, model wrappers, inference

```
GigaLearnCPP/src/
├── public/GigaLearnCPP/
│   ├── Framework.h               — common includes/typedefs for the lib
│   ├── Learner.h / Learner.cpp   — top-level Learner class (training loop driver)
│   ├── LearnerConfig.h           — LearnerConfig struct (checkpoints, device, etc.)
│   ├── SkillTrackerConfig.h      — skill/MMR tracker config struct
│   ├── PPO/PPOLearnerConfig.h    — PPO hyperparameter config
│   ├── PPO/TransferLearnConfig.h — transfer-learning config
│   └── Util/
│       ├── AvgTracker.h          — running-average utility
│       ├── InferUnit.cpp/.h      — inference wrapper used by RLBotClient for live play
│       ├── KeyPressDetector.cpp/.h — dev hotkey detection (e.g. toggle render)
│       ├── MetricSender.cpp/.h   — sends metrics to Python (wandb) via socket
│       ├── RenderSender.cpp/.h   — sends render/vis data to Python receiver
│       ├── Report.cpp/.h         — training report/logging struct
│       ├── Timer.h               — simple timer utility
│       ├── Utils.cpp/.h          — misc helpers
│       └── ModelConfig.h         — distinct from GigaLearnBot/src/ModelConfig.h; used by RLBotClient/rlbotmain
└── private/GigaLearnCPP/
    ├── FrameworkTorch.h          — libtorch-specific framework includes
    ├── PolicyVersionManager.cpp/.h — tracks/saves policy versions for self-play
    ├── MCTS/
    │   ├── MCTSNode.cpp/.h       — MCTS tree node (MuZero-style search)
    │   ├── MCTSSearch.cpp/.h     — MCTS search algorithm
    │   └── ReanalysisBuffer.h    — MuZero reanalysis buffer
    ├── PPO/
    │   ├── PPOLearner.cpp/.h     — core PPO training algorithm
    │   ├── ExperienceBuffer.cpp/.h — rollout storage
    │   └── GAE.cpp/.h            — Generalized Advantage Estimation
    └── Util/
        ├── Models.cpp/.h         — neural net model definitions (policy/critic nets)
        ├── MixerModelWrapper.cpp/.h, MixerBlock.h — mixer-architecture model wrapper
        ├── CUDAOptimizer.h       — CUDA-specific optimizer helpers
        ├── InferenceOptimizer.h  — inference-time optimization
        ├── MagSGD.cpp/.h         — custom optimizer
        ├── RLTokenizer.h         — tokenizer utility
        └── WelfordStat.h         — Welford's online variance/mean algorithm
```

## RLBotCPP/ — vendored RLBot v5 C++ client library

```
RLBotCPP/
├── CMakeLists.txt, LICENSE, README.md, .gitignore
├── inc/rlbot/   — public headers: bot.h, botmanager.h, botprocess.h, color.h,
│                  controller.h, errorcodes.h, flatbuffercontainer.h, interface.h,
│                  matchsettings.h, namedrenderer.h, packets.h, platform.h,
│                  renderer.h, rlbot.h, rlbot_generated.h, scopedrenderer.h,
│                  server.h, sockets.h, statesetting.h
├── lib/inc/     — bundled third-party dependency headers
└── src/         — bot.cc, botmanager.cc, botprocess.cc, color.cc, interface.cc,
                   matchsettings.cc, namedrenderer.cc, platform_linux.cc,
                   platform_windows.cc, renderer.cc, scopedrenderer.cc,
                   server.cc, sockets_linux.cc, sockets_windows.cc, statesetting.cc
```

`GigaLearnBot/src/RLBotClient.cpp` and `rlbotmain.cpp` build against this.

## rlbot/ — RLBot v5 plugin config (15 files)

```
rlbot/
├── rlbot.cfg                  — main RLBot v5 match/bot config
├── appearance.cfg             — bot car appearance config
├── port.cfg                   — port config
├── queue_config.json          — match queue config
├── CppPythonAgent.py          — Python shim that launches/talks to the C++ bot process
├── CppPythonAgent.cfg         — config for the above agent
├── match_monitor.py           — monitors live match state
├── match_queue.py             — queue management script
├── mmr_tracker_interface.py   — interface to MMR tracking
├── MMR_TRACKER_README.md      — docs for MMR tracker
├── requirements.txt           — Python deps for this plugin
├── RefreshEnv.cmd             — Windows env refresh script
└── README.md, LICENSE
```

## python_scripts/ — the three copies

**Canonical: `GigaLearnBot/python_scripts/`** (edit here, not the copies):
| File | Purpose |
|---|---|
| `reward_llm_generator.py` | LLM (Claude API)-based reward generation & compile-testing pipeline; CLI with `--checkpoint`, `--mode fix\|new\|both`, `--dry-run`, `--validate-code` |
| `reward_analysis_auto.py` | Automated reward analysis pipeline, run at every checkpoint save; entry point `run_analysis(checkpoint_path, checkpoints_root, total_timesteps, wandb_run)` |
| `reward_gradient_analysis.py` | Analyzes per-reward metrics (weight, mean_value, variance, activation_rate) to flag dominance/dead-signal/farming |
| `metric_receiver.py` | Socket receiver that forwards training metrics into a wandb run |
| `render_receiver.py` | Render/visualization receiver forwarding to RocketSimVis over a socket |

**`GigaLearnBot/GigaLearnCPP/python_scripts/`** — CMake-copied mirror; also has an extra `render_main.py` not in the canonical copy. `reward_analysis_auto.py` here is byte-identical to canonical.

**`GigaLearnBot/build/python_scripts/`** — CMake-copied mirror, populated at configure time via `configure_file` COPY (see memory: `project_build_python_scripts_copy`). Missing `reward_llm_generator.py` — only has `metric_receiver.py`, `render_receiver.py`, `reward_analysis_auto.py`, `reward_gradient_analysis.py`. **New checkpoint-time modules must be added to the CMake copy list or imports fail at runtime.**
