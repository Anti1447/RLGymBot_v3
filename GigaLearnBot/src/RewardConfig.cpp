#pragma once
#include "RewardConfig.h"
#include <iostream>
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/PassingRewards.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/ShootingRewards.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/OptionControlReward.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/OptionGatingWrappers.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/SimpleOvercommitmentPenalty.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/AdvancedOvercommitmentPenalty.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/AdvancedOpponentOvercommitmentReward.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/CompositeFlickReward.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/AirRollMultiplierReward.h"

std::vector<WeightedReward> GetPhasedRewards() {
    switch (CURRENT_PHASE) {

        // ========================================================================
        // PHASE 1: BRONZE (0-1B steps)
        // Focus: Ball chasing, boost collection, basic touches
        // Key: Zero-sum on ALL competitive rewards
        // ========================================================================
    case TrainingPhase::PHASE_1_BRONZE:
        return std::vector<WeightedReward>{

                // Player-Ball
                WeightedReward(new FaceBallReward(), 0.25f),
                WeightedReward(new VelocityPlayerToBallReward(), 4.0f),  // REDUCED
                
                WeightedReward(new ZeroSumReward(new StrongTouchReward(20, 100), 0.3f), 60.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new JumpTouchReward(), 0.3f), 10.0f),  // Learn to jump

                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 2.0f),

                // Boost
                WeightedReward(new PickupBoostReward(), 10.0f),
                WeightedReward(new SaveBoostReward(), 0.2f),

                // Movement and Recovery
                WeightedReward(new ProperLandingReward(), 0.2f),
                WeightedReward(new AdvancedRecoveryReward(), 0.2f),
                WeightedReward(new PowerSlideControlReward(), 0.25f),
                WeightedReward(new AirReward(), 0.25f),

                // Event Rewards
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 80.0f),
                WeightedReward(new GoalReward(), 150.0f)

                
        };

        // ========================================================================
        // PHASE 2: SILVER (1-3B steps)
        // Focus: Ground control, simple aerials, basic recoveries
        // ========================================================================
    case TrainingPhase::PHASE_2_SILVER:
        return std::vector<WeightedReward>{
                // Keep important ones from Stage 2
                WeightedReward(new VelocityPlayerToBallReward(), 1.0f),  // VERY REDUCED
                WeightedReward(new ZeroSumReward(new StrongTouchReward(25, 110), 0.3f), 80.0f),  // Stricter
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 20.0f),
                WeightedReward(new InstantSpacingReward(RotationPhase::GOLD), 2.0f),  // UPGRADED
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 15.0f),
                WeightedReward(new PowerSlideControlReward(), 0.5f),
                WeightedReward(new GoalReward(-1.0f), 300.0f),

                // NEW: Aerial focus
                WeightedReward(new ZeroSumReward(new AirTouchReward(), 0.3f), 30.0f),  // MAIN FOCUS
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 7.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new JumpTouchReward(), 0.3f), 10.0f),  // Learn to jump
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 10.0f),

                // NEW: Recovery (needed for aerials)
                WeightedReward(new HalfFlipReward(), 3.0f),
                WeightedReward(new WavedashBasicReward(), 5.0f),
                //WeightedReward(new AirReward(), 0.3f),
                WeightedReward(new ProperLandingReward(), 0.6f),
                WeightedReward(new AdvancedRecoveryReward(), 0.5f),

                // NEW: Boost management (critical for aerials)
                WeightedReward(new PickupBoostReward(), 20.0f),  // INCREASED
                WeightedReward(new SaveBoostReward(), 0.6f),
                WeightedReward(new SmallPadCollectionReward(), 2.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 2.0f),
                WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
                WeightedReward(new PadCenterAccuracyReward(), 10.0f),
                WeightedReward(new BigBoostApproachReward(), 8.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

                // NEW: Team boost cooperation (3v3 kickoff corner assignment)
                //WeightedReward(new KickoffCornerBoostAssignmentReward(), 3.0f),

                // NEW: Basic Shots
                WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 25.0f),
                //WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 25.0f),
                //WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f), // updated reward on 01/30/26

                // NEW: Minor possession shaping
                //WeightedReward(new DefensivePositionReward(), 2.5f),
                // TimeToBalPossession removed from BRONZE phase (P4 weight rebalancing)
                // NEW: Retreat after touch (prepare for rotation)
                //WeightedReward(new RetreatAfterTouchReward(RotationPhase::GOLD), 3.0f),

                //WeightedReward(new DemoAwarenessReward(), 1.5f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 80.0f),

                // Rotation handled entirely by ProgressiveRotationReward (includes spacing + retreat internally)
                WeightedReward(new ProgressiveRotationReward(RotationPhase::PLATINUM), 5.0f),

                // NEW: Basic shots
                WeightedReward(new ZeroSumReward(new ShotReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 30.0f)
        };

        // ========================================================================
        // PHASE 3: GOLD (3-5B steps)
        // Focus: Wall play, power hits, basic defense
        // ========================================================================
    case TrainingPhase::PHASE_3_GOLD:
        return std::vector<WeightedReward>{
            WeightedReward(new TrajectoryComparisonVPBReward(), 1.0f),  // replaces VPTB with more nuanced velocity shaping. dont use at the same time as other VPTB to avoid gradient confusion
                // Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/velocity_player_to_ball_reward.py
                WeightedReward(new ZeroSumReward(new StrongTouchReward(70, 130), 0.3f), 100.0f),  // Stricter
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new DirectionalAccelerationReward(), 0.3f), 60.0f),
                WeightedReward(new ZeroSumReward(new EnhancedVelocityBallToGoalReward(), 1.0f), 50.0f),  // ← 5x boost
                //WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 10.0f),
                WeightedReward(new PowerSlideControlReward(), 0.7f),
                // GoalViewReward: potential-based shaping via goal-mouth solid angle (Rolv / Ng et al. 1999).
                // NOT wrapped in ZeroSumReward — the class already returns +/- by team internally.
                WeightedReward(new GoalViewReward(0.9958f), 10.0f),
                // CornerToLaneReward: rewards moving ball toward center lane when in opponent corner.
                // Discourages corner-to-corner pinching; x-component reward only.
                WeightedReward(new ZeroSumReward(new CornerToLaneReward(), 0.5f), 15.0f),

                // NEW: Aerial focus
                //WeightedReward(new AirReward(), 0.2f),
                //WeightedReward(new ZeroSumReward(new AirTouchReward(), 0.3f), 50.0f),  // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 0.3f), 70.0f),
                //WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.5f), 12.0f),  // retired Rev 1.0 — Phase 6+ only
                WeightedReward(new PitchControlAerialReward(),10.0f),
                WeightedReward(new ZeroSumReward(new EarlyAerialReward(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 10.0f), // taper toward P4-6 plateau; now zero-sum for consistency (was unwrapped — see Reward_Change_Log 2026-07-28)
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new MultiAirTouchReward(), 0.3f), 40.0f),
                // WeightedReward(new ZeroSumReward(new JumpTouchReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 60.0f),
                WeightedReward(new ZeroSumReward(new AirRollShotReward(), 0.3f), 60.0f),
                WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.3f), 60.0f),
                //WeightedReward(new ZeroSumReward(new DoomseeDisshShotReward(), 0.3f), 60.0f),

                // NEW: Recovery (needed for aerials)
                //WeightedReward(new HalfFlipReward(), 3.0f),
                //WeightedReward(new WavedashBasicReward(), 5.0f),
                //WeightedReward(new EnhancedSpeedFlipReward(), 12.0f),
                //WeightedReward(new ReverseRecoveryReward(), 0.4f),
                //WeightedReward(new DirectionalFlipRecoveryReward(), 0.4f),
                WeightedReward(new ProperLandingReward(), 1.1f),
                WeightedReward(new AdvancedRecoveryReward(), 0.3f),
                WeightedReward(new PowerslideJumpFlipReward(), 0.6f),
                WeightedReward(new BumpedFlipRecoveryReward(), 0.3f),
                WeightedReward(new SupersonicReward(), 2.5f),
                WeightedReward(new VelocityReward(), 0.12f),
                WeightedReward(new BoostDownRecoveryReward(), 0.25f),

                // Powerslide turning mechanics (Phase 3 intro — conservative weights)
                WeightedReward(new PowerslideSharpTurnReward(), 1.2f),
                //WeightedReward(new Powerslide180ForwardReward(), 1.5f),
                //WeightedReward(new Powerslide180ReverseReward(), 1.5f),
                //WeightedReward(new Powerslide360Reward(), 2.0f),

                //Kickoffs
                //WeightedReward(new KickoffVelocityPlayerToBallReward(), 3.0f),
                WeightedReward(new KickoffSpeedReward(), 15.0f),
                WeightedReward(new ZeroSumReward(new KickoffBallPositionReward(), 1.0f), 15.0f),
                WeightedReward(new ClosestPlayerKickoffSideReward(), 10.0f),
                WeightedReward(new KickoffDoubleCommitPenalty(), 3.5f),
                WeightedReward(new KickoffStrategyReward(), 20.0f),  // Reward diverse kickoff strategies
                //WeightedReward(new SpeedFlipKickoffReward(), 2.0f),
                //WeightedReward(new DiagonalBoostKickoffReward(), 4.0f),
                //WeightedReward(new DelayedKickoffReward(), 2.0f),
                //WeightedReward(new FakeKickoffReward(), 2.0f),
                //WeightedReward(new ReactiveKickoffReward(), 2.0f),
                //WeightedReward(new PredictiveSecondManReward(), 2.0f),
                //WeightedReward(new SpanishDemoKickoffReward(), 2.0f),
                // Kickoff - Penalize team if nobody commits to the ball
                //WeightedReward(new KickoffNoChaseTeamPenalty(), 30.0f),
                // Kickoff - Support players grab corner/mid boost during kickoff (2v2+)
                //WeightedReward(new KickoffSupportBoostReward(), 10.0f),
                //WeightedReward(new KickoffFormReward(), 10.0f),
                // Approach quality: centerline x-alignment × y-direction × speed
                //WeightedReward(new KickoffMassAlignmentReward(), 20.0f),

                // NEW: Boost management (critical for aerials)
                WeightedReward(new PickupBoostReward(), 90.0f),  // INCREASED
                WeightedReward(new SaveBoostReward(), 0.75f),
                WeightedReward(new SmallPadCollectionReward(), 12.0f),
                WeightedReward(new SmallPadRecoveryRouteReward(), 4.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 210.0f),
                WeightedReward(new ZeroSumReward(new RotationPathBoostDenialReward(), 0.5f), 60.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 3.5f),
                WeightedReward(new BoostWastePenaltyReward(), 40.0f),
                //WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
                WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 6.0f),
                WeightedReward(new PadCenterAccuracyReward(), 20.0f),
                WeightedReward(new BigBoostApproachReward(), 8.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

                // NEW: Dribbling
                WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 2.0f),
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.3f), 4.0f),
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.4f), 0.5f),
                WeightedReward(new ZeroSumReward(new CatchBallReward(), 0.4f), 1.0f),
                WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 0.5f),
                WeightedReward(new ZeroSumReward(new FlickReward(), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new AdvancedFlickReward(), 0.5f), 10.0f),
                //WeightedReward(new ZeroSumReward(new CompositeFlickReward(), 0.3f), 40.0f),
                WeightedReward(new ZeroSumReward(new FlickOutplayReward(), 0.5f), 5.0f),

                // NEW: Basic Shots
                WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 35.0f),
                WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f), // updated reward on 01/30/26

                // NEW: Minor possession shaping and defense
                //WeightedReward(new DefensivePositionReward(), 3.0f),
                WeightedReward(new ZeroSumReward(new ChallengeAngleQualityReward(), 0.3f), 1.4f),  // Angle of challenge approach vs threat vector
                WeightedReward(new BackboardDefenseReward(), 4.0f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f),  // P4: 2.5 → 2.0
                WeightedReward(new ZeroSumReward(new PossessionControlRewardv2(), 1.0f), 10.0f),   // P4: 10.0 → 1.0 (light intro) [v2: threat-gated]
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 0.3f), 3.0f),
                WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 0.8f), // Added to Option Control above
                WeightedReward(new DefensivePositioningChallengeReward(), 1.8f),
                WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 8.0f),
                WeightedReward(new PoorWallReboundPenalty(), 40.0f),
                WeightedReward(new ZeroSumReward(new OpponentPassInterceptionReward(), 1.0f), 12.0f),
                WeightedReward(new GoalpostPositioningReward(), 8.0f),
                WeightedReward(new EmergencySaveCommitReward(), 6.0f),
                WeightedReward(new UncoveredNetPenalty(), 3.0f),
                WeightedReward(new SimpleOvercommitmentPenalty(), 24.0f),  // Phase 1: Resource management & positioning discipline
                WeightedReward(new DoubleCommitPenalty(), 40.0f),
                WeightedReward(new ActiveSupportingPositionReward(), 7.0f),
                WeightedReward(new ClosestPlayerSpeedReward(), 7.0f),

                // NEW: Passing rewards
                WeightedReward(new ZeroSumReward(new BackPassReward(), 1.0f), 3.0f),
                WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 20.0f),

                // Rotation handled entirely by ProgressiveRotationReward (includes spacing + retreat internally)
                    WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 2.0f),
                    WeightedReward(new InstantSpacingReward(RotationPhase::CHAMPION_PLUS), 5.0f),  // UPGRADED
                    WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 12.0f),
                    WeightedReward(new BoostAwareRotationReward(RotationPhase::CHAMPION_PLUS), 2.0f),

                // NEW: Numerical advantage attacking (2v1+ situations)
                WeightedReward(new ZeroSumReward(new NumericalAdvantageAttackingReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new BallSideNumericalAdvantageReward(), 1.0f), 0.6f),  // Press when opponent over-advances past ball

                WeightedReward(new DemoAwarenessReward(), 2.5f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 240.0f),
                WeightedReward(new BumpedPenalty(), 80.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 360.0f),
                WeightedReward(new ZeroSumReward(new RolvDemoReward(1.0f, 1.0f, 1.0f), 0.5f), 300.0f),
                WeightedReward(new ZeroSumReward(new DemoLeadApproachReward(), 0.3f), 35.0f),
                WeightedReward(new ZeroSumReward(new DemoInterceptLeadReward(), 0.3f), 15.0f),
                WeightedReward(new ThirdManDemoReward(), 80.0f),
                    // Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/demo_reward.py

                WeightedReward(new ZeroSumReward(new SpeedScaledShotReward(), 0.3f), 250.0f),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 100.0f),
                WeightedReward(new GoalReward(-1.0f), 1300.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 140.0f)
        };

        // ========================================================================
        // PHASE 4: PLATINUM (5-8B steps)
        // Focus: Air control, 50/50s, boost management, dribbling intro
        // ========================================================================
    case TrainingPhase::PHASE_4_PLATINUM:
    {
        auto* thirdManCtx = new ThirdManContext();   // one per arena (per GetPhasedRewards call)
        return std::vector<WeightedReward>{
            WeightedReward(new TrajectoryComparisonVPBReward(), 0.45f),  // replaces VPTB with more nuanced velocity shaping. dont use at the same time as other VPTB to avoid gradient confusion
            // Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/velocity_player_to_ball_reward.py
            WeightedReward(new ZeroSumReward(new StrongTouchReward(80, 130), 0.3f), 50.0f),  // Stricter
            WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 25.0f),
            WeightedReward(new ZeroSumReward(new DirectionalAccelerationReward(), 0.3f), 50.0f),
            WeightedReward(new ZeroSumReward(new EnhancedVelocityBallToGoalReward(), 1.0f), 40.0f),  // ← 5x boost
            WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 40.0f),
            //WeightedReward(new ZeroSumReward(new OpenNetPushReward(), 1.0f), 20.0f),  // 2026-06-04 push ball into open net (far bottom corner)
            WeightedReward(new PowerSlideControlReward(), 0.5f),
            // GoalViewReward: potential-based shaping via goal-mouth solid angle (Rolv / Ng et al. 1999).
            // NOT wrapped in ZeroSumReward — the class already returns +/- by team internally.
            WeightedReward(new GoalViewReward(0.9972f), 45.0f),
			WeightedReward(new BallGoalProximityReward(), 0.45f), // no zerosum needed since this is just a proximity shaping reward, not directly competitive
            // CornerToLaneReward: rewards moving ball toward center lane when in opponent corner.
            // Discourages corner-to-corner pinching; x-component reward only.
            WeightedReward(new ZeroSumReward(new CornerToLaneReward(), 0.5f), 40.0f),
            WeightedReward(new OwnGoalPenalty(-1.0f), 1200.0f),


            // NEW: Aerial focus
            //WeightedReward(new AirReward(), 0.2f),
            //WeightedReward(new ZeroSumReward(new AirTouchReward(), 0.3f), 50.0f),  // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
            WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 0.3f), 160.0f),
            WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.5f), 5.0f),
            //WeightedReward(new DoubleJumpAerialReward(), 50.0f),  // stepping stone: teach pure double-jump before PitchControl can fire
            WeightedReward(new PitchControlAerialReward(), 14.0f),
            
            //WeightedReward(new ZeroSumReward(new EarlyAerialReward(), 0.5f), 20.0f),
            WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 5.0f), // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
            //WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 10.0f),
            //WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 10.0f),
            WeightedReward(new ZeroSumReward(new MultiAirTouchReward(), 0.3f), 60.0f),
            // UnifiedAirDribbleReward: base progress-toward-goal channel. A second, independent
            // instance is intentionally also registered below (~line 529) inside a
            // MathRewardWrapper({UnifiedAirDribbleReward, ShotAccuracyInBoundsReward}, MULTIPLY) —
            // that second instance is a shot-accuracy-gated bonus channel layered on top of this
            // one, not an accidental duplicate. Do not remove either without removing both.
            WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.3f), 40.0f),
            WeightedReward(new ZeroSumReward(new UnifiedDoubleTapReward(), 0.3f), 100.0f),
             //WeightedReward(new ZeroSumReward(new JumpTouchReward(), 0.3f), 20.0f),
            //WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 20.0f),
            //WeightedReward(new ZeroSumReward(new AirRollShotReward(), 0.3f), 40.0f),
            WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.3f), 60.0f),
            WeightedReward(new ZeroSumReward(new AerialOutplayReward(), 0.3f), 25.0f),  // Tactical awareness during aerials
            // WallCeilingRedirectReward (Rev 1.0) — SHADOW MODE (M2 validation, 200–500M steps).
            // Reads sidewall/corner/ceiling bounces (backboard excluded — owned by
            // BackboardReboundReward above) and pays an aerial redirect/continuation.
            // shadowMode=true → logs W&B metrics, returns 0.0f to the optimizer.
            //WeightedReward(new ZeroSumReward(new WallCeilingRedirectReward([]{ WallCeilingRedirectReward::Config c; c.shadowMode = false; return c; }()), 0.3f), 50.0f),
            //WeightedReward(new OutplayExplorationReward(), 6.0f),
            // OutplayCreativityReward: inverted/sideways car or low-boost (<30) during outplay touch.
            //WeightedReward(new OutplayCreativityReward(), 2.5f),
            //WeightedReward(new ZeroSumReward(new DoomseeDisshShotReward(), 0.3f), 150.0f),
            //WeightedReward(new InvertedAirDribbleReward(), 110.0f), // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
            //WeightedReward(new TornadoSpinTakeoffReward(), 5.0f), // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward

            //WeightedReward(new InvertedAirDribbleReward([]{ InvertedAirDribbleReward::Config c; c.shadowMode = false; return c; }()), 300.0f),
            //WeightedReward(new TornadoSpinTakeoffReward([]{ TornadoSpinTakeoffReward::Config c; c.shadowMode = false; return c; }()), 6.0f),

            // NEW: Recovery (needed for aerials)
            //WeightedReward(new HalfFlipReward(), 3.0f),
            //WeightedReward(new WavedashBasicReward(), 5.0f),
            //WeightedReward(new EnhancedSpeedFlipReward(), 12.0f),
            //WeightedReward(new ReverseRecoveryReward(), 0.4f),
            //WeightedReward(new DirectionalFlipRecoveryReward(), 0.4f),
            WeightedReward(new ProperLandingReward(), 1.0f),
            //WeightedReward(new AdvancedRecoveryReward(), 0.08f),
            WeightedReward(new PowerslideJumpFlipReward(), 0.5f),
            WeightedReward(new BumpedFlipRecoveryReward(), 0.2f),
            WeightedReward(new SupersonicReward(), 3.0f),
            //WeightedReward(new VelocityReward(), 0.06f),

            WeightedReward(new BoostDownRecoveryReward(), 1.00f),

            // Powerslide turning mechanics (Phase 3 intro — conservative weights)
            WeightedReward(new PowerslideSharpTurnReward(), 1.0f),
            //WeightedReward(new Powerslide180ForwardReward(), 1.5f),
            //WeightedReward(new Powerslide180ReverseReward(), 1.5f),
            //WeightedReward(new Powerslide360Reward(), 2.0f),

            //Kickoffs
            WeightedReward(new KickoffVelocityPlayerToBallReward(), 0.8f),
            WeightedReward(new KickoffSpeedReward(), 11.0f),
            WeightedReward(new ZeroSumReward(new KickoffBallPositionReward(), 1.0f), 15.0f),
            WeightedReward(new ClosestPlayerKickoffSideReward(), 10.0f),
            WeightedReward(new KickoffDoubleCommitPenalty(), 1.5f),
            WeightedReward(new KickoffStrategyReward(), 15.0f),  // Reward diverse kickoff strategies
            //WeightedReward(new SpeedFlipKickoffReward(), 2.0f),
            //WeightedReward(new DiagonalBoostKickoffReward(), 4.0f),
            //WeightedReward(new DelayedKickoffReward(), 2.0f),
            //WeightedReward(new FakeKickoffReward(), 2.0f),
            //WeightedReward(new ReactiveKickoffReward(), 2.0f),
            //WeightedReward(new PredictiveSecondManReward(), 2.0f),
            //WeightedReward(new SpanishDemoKickoffReward(), 2.0f),
            // Kickoff - Penalize team if nobody commits to the ball
            //WeightedReward(new KickoffNoChaseTeamPenalty(), 30.0f),
            // Kickoff - Support players grab corner/mid boost during kickoff (2v2+)
            //WeightedReward(new KickoffSupportBoostReward(), 10.0f),
            //WeightedReward(new KickoffFormReward(), 10.0f),
            // Approach quality: centerline x-alignment × y-direction × speed
            WeightedReward(new KickoffMassAlignmentReward(), 20.0f),
            // Counter the opponent's kickoff: opp touches first -> redirect back at their goal
            WeightedReward(new ZeroSumReward(new KickoffCounterHitReward(), 0.3f), 40.0f),

            // NEW: Boost management (critical for aerials)
            WeightedReward(new PickupBoostReward(), 100.0f),  // INCREASED
            WeightedReward(new SaveBoostReward(), 0.8f),
            WeightedReward(new SmallPadCollectionReward(), 50.0f),
            WeightedReward(new SmallPadRecoveryRouteReward(), 10.0f),
            WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 280.0f),
            WeightedReward(new ZeroSumReward(new RotationPathBoostDenialReward(), 0.5f), 80.0f),
            WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 4.0f),
            WeightedReward(new BoostWastePenaltyReward(), 15.0f),
            WeightedReward(new PostKickoffMidBoostReward(), 20.0f),
            WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 8.0f),
            WeightedReward(new PadCenterAccuracyReward(), 20.0f),
            WeightedReward(new BigBoostApproachReward(), 8.0f),
            WeightedReward(new DefensiveRotationBoostReward(), 25.0f),

            // NEW: Dribbling
            WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 2.0f),
            WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.3f), 4.0f),
            WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.4f), 0.8f),
            //WeightedReward(new ZeroSumReward(new CatchBallReward(), 0.4f), 0.5f),
            //WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 0.7f),
            WeightedReward(new ZeroSumReward(new FlickReward(), 0.5f), 15.0f),
            
            WeightedReward(new ZeroSumReward(new CompositeFlickReward(), 0.3f), 10.0f),
            WeightedReward(new ZeroSumReward(new FlickOutplayReward(), 0.5f), 20.0f),
            //WeightedReward(new ZeroSumReward(new FrontFlipFlickReward(), 0.5f), 10.0f),  // 2026-06-04 front-flip dribble flick
            //WeightedReward(new ZeroSumReward(new DiagonalFlickReward(), 0.5f), 10.0f),   // 2026-06-04 diagonal dribble flick
            //WeightedReward(new ZeroSumReward(new SideFlickReward(), 0.5f), 10.0f),      // 2026-06-07 pure sideways dribble flick
            WeightedReward(new ZeroSumReward(new BallSteerReward(), 0.4f), 0.7f),                                // 2026-06-19 ball-steering through dribble turns

            // NEW: Basic Shots
            WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 30.0f),
            WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 25.0f),
            WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f), // updated reward on 01/30/26

            // NEW: Minor possession shaping and defense
            //WeightedReward(new DefensivePositionReward(), 1.0f),
            WeightedReward(new ZeroSumReward(new ChallengeAngleQualityReward(), 0.3f), 0.75f),  // Angle of challenge approach vs threat vector
            WeightedReward(new BackboardDefenseReward(), 5.5f),
            WeightedReward(new ZeroSumReward(new BackboardInterceptionReward(), 0.5f), 15.0f),
            WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f),  // P4: 2.5 → 2.0
            WeightedReward(new ZeroSumReward(new PossessionControlRewardv2(), 1.0f), 22.0f),   // P4: 10.0 → 1.0 (light intro) [v2: threat-gated]
            WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 0.5f), 3.4f),
            WeightedReward(new ZeroSumReward(new ShadowDefenseReward(), 0.5f), 1.3f),
            WeightedReward(new ZeroSumReward(new SingleJumpLow5050Reward(), 0.5f), 1.1f),

            // Third-man tactics (SHADOW, M2 validation). Stall MUST precede CounterLaunch:
            // StallReward.PreStep calls ctx.Update(); CounterLaunch reads from it.
            // Self-gates to 0 in 1v1 (livingCount < 2). No policy impact while shadowMode=true.
            WeightedReward(new ThirdManStallReward(*thirdManCtx,
                []{ ThirdManStallReward::Config c; c.shadowMode = false; return c; }()), 20.0f),

            WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 0.6f), // Added to Option Control above
            WeightedReward(new DefensivePositioningChallengeReward(), 0.7f),
            WeightedReward(new LastManOvercommitPenalty(), 15.0f),
            WeightedReward(new UncoveredNetPenalty(), 6.0f),
            WeightedReward(new OpponentInterceptAwarePenalty(), 6.0f),
            WeightedReward(new ZeroSumReward(new ImpendingShotDefenseReward(), 0.5f), 15.0f),
            //WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 8.0f),
            WeightedReward(new PoorWallReboundPenalty(), 80.0f),
            WeightedReward(new ZeroSumReward(new OpponentPassInterceptionReward(), 1.0f), 25.0f),
            WeightedReward(new ZeroSumReward(new BallLandingPositionReward(), 1.0f), 25.0f),
            WeightedReward(new GoalpostPositioningReward(), 7.0f),
            WeightedReward(new EarlyDangerDetectionReward(), 1.0f),
            WeightedReward(new EmergencySaveCommitReward(), 4.0f),
            WeightedReward(new SimpleOvercommitmentPenalty(), 30.0f),  // Phase 1: Resource management & positioning discipline
            WeightedReward(new DoubleCommitPenalty(), 90.0f),
            WeightedReward(new ActiveSupportingPositionReward(), 7.0f),
            //WeightedReward(new LowBoostOppHalfFiftyPunish(), 100.0f),
            WeightedReward(new ClosestPlayerSpeedReward(), 5.0f),
            WeightedReward(new OffBallAerialPenalty(), 15.0f),
            WeightedReward(new SecondaryAerialPenalty(), 15.0f),
            WeightedReward(new NonPrimaryAirPenalty(), 15.0f),

            // NEW: Passing rewards
            //WeightedReward(new ZeroSumReward(new BackPassReward(), 1.0f), 4.0f),
            WeightedReward(new ZeroSumReward(new PassingLaneNetworkReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new SpatialFakeReward(), 1.0f), 15.0f),
            WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 45.0f),
            WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 22.0f),
            WeightedReward(new PassToOpponentPenalty(), 60.0f),

            // Rotation handled entirely by ProgressiveRotationReward (includes spacing + retreat internally)
            //WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 2.0f),
            WeightedReward(new InstantSpacingReward(RotationPhase::CHAMPION_PLUS), 8.0f),  // UPGRADED
            WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 20.0f),
            WeightedReward(new BoostAwareRotationReward(RotationPhase::CHAMPION_PLUS), 2.0f),

            // NEW: Numerical advantage attacking (2v1+ situations)
            WeightedReward(new ZeroSumReward(new NumericalAdvantageAttackingReward(), 1.0f), 30.0f),
            WeightedReward(new ZeroSumReward(new BallSideNumericalAdvantageReward(), 1.0f), 1.2f),  // Press when opponent over-advances past ball
            WeightedReward(new GoalsideReward(), 0.42f),  // Goalside fundamental: +1 between ball and own net, -0.4 offside (smooth 800uu ramp)

            WeightedReward(new DemoAwarenessReward(), 2.0f),
            WeightedReward(new ZeroSumReward(new BumpReward(), 1.0f), 200.0f),
            //WeightedReward(new BumpedPenalty(), 80.0f),
            WeightedReward(new ZeroSumReward(new DemoReward(), 1.0f), 250.0f),
            WeightedReward(new RolvDemoReward(1.0f, 1.0f, 1.0f), 250.0f),
            WeightedReward(new DemoLeadApproachReward(), 50.0f),
            WeightedReward(new ZeroSumReward(new DemoInterceptLeadReward(), 1.0f), 40.0f),
            WeightedReward(new ZeroSumReward(new DemoOpportunityReward(), 1.0f), 100.0f),
            WeightedReward(new ThirdManDemoReward(), 120.0f),
            // Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/demo_reward.py

                //WeightedReward(new ZeroSumReward(new PlayerVelocityToOpponentReward(), 1.0f), 5.0f),

            //WeightedReward(new ZeroSumReward(new SpeedScaledShotReward(), 0.3f), 300.0f),
            //WeightedReward(new ZeroSumReward(new ShotReward(), 1.0f), 100.0f),
            WeightedReward(
                new ZeroSumReward(
                    new MathRewardWrapper(
                        {
                            new ShotReward(),
                            new ShotAccuracyInBoundsReward()
                        },
                        MathOp::MULTIPLY
                    ),
                    1.0f
                ),
                500.0f
            ),
            WeightedReward(
                new ZeroSumReward(
                    new MathRewardWrapper(
                        {
                            new AdvancedFlickReward(),
                            new ShotAccuracyInBoundsReward()
                        },
                        MathOp::MULTIPLY
                    ),
                    1.0f
                ),
                50.0f
            ),
            // Second UnifiedAirDribbleReward instance (see the base instance + comment at line 324).
            // This one is gated by ShotAccuracyInBoundsReward via MULTIPLY — a separate
            // shot-accuracy-conditioned bonus channel, intentionally stacked with the base
            // progress reward above, not a duplicate registration.
            WeightedReward(
                new ZeroSumReward(
                    new MathRewardWrapper(
                        {
                            new UnifiedAirDribbleReward(),
                            new ShotAccuracyInBoundsReward()
                        },
                        MathOp::MULTIPLY
                    ),
                    1.0f
                ),
                30.0f
            ),
            WeightedReward(
                new ZeroSumReward(
                    new MathRewardWrapper(
                        {
                            new UnifiedDoubleTapReward(),
                            new ShotAccuracyInBoundsReward()
                        },
                        MathOp::MULTIPLY
                    ),
                    1.0f
                ),
                50.0f
            ),
            WeightedReward(new ZeroSumReward(new ShotPassReward(), 1.0f), 300.0f),
            WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 200.0f),
            WeightedReward(new GoalReward(-0.8f), 2500.0f),
            WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 250.0f)
        };
    }
    case TrainingPhase::PHASE_5_DIAMOND:
    {
        auto* thirdManCtx = new ThirdManContext();   // one per arena (per GetPhasedRewards call)
        return std::vector<WeightedReward>{
            //WeightedReward(new FaceBallReward(), 0.1f),
            //WeightedReward(new TrajectoryComparisonVPBReward(), 1.0f),  // replaces VPTB with more nuanced velocity shaping. dont use at the same time as other VPTB to avoid gradient confusion
                // Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/velocity_player_to_ball_reward.py
                // ZeroSumReward wrappers restored 2026-07-07: every other phase wraps these
                // three in ZeroSumReward(child, 0.3f); Phase 5 was missing it (unexplained
                // copy-paste omission) at the highest raw weights in the whole config.
                WeightedReward(new ZeroSumReward(new StrongTouchReward(50, 110), 0.3f), 25.0f),  // Stricter
                //WeightedReward(new HalfVolleyReward(), 10.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.2f), 35.0f),
                WeightedReward(new ZeroSumReward(new DirectionalAccelerationReward(), 0.2f), 50.0f),
                WeightedReward(new ZeroSumReward(new EnhancedVelocityBallToGoalReward(), 1.0f), 15.0f),  // ← 5x boost
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 10.0f),
                //WeightedReward(new ZeroSumReward(new OpenNetPushReward(), 1.0f), 25.0f),  // 2026-06-04 push ball into open net (far bottom corner)
                //WeightedReward(new PowerSlideControlReward(), 0.5f),
                // GoalViewReward: potential-based shaping via goal-mouth solid angle (Rolv / Ng et al. 1999).
                // NOT wrapped in ZeroSumReward — the class already returns +/- by team internally.
                //WeightedReward(new GoalViewReward(0.9975f), 40.0f),
                WeightedReward(new BallGoalProximityReward(), 0.6f), // no zerosum needed since this is just a proximity shaping reward, not directly competitive
                // CornerToLaneReward: rewards moving ball toward center lane when in opponent corner.
                // Discourages corner-to-corner pinching; x-component reward only.
                //WeightedReward(new ZeroSumReward(new CornerToLaneReward(), 0.5f), 20.0f),
                //WeightedReward(new OwnGoalPenalty(-1.0f), 1500.0f),
                //WeightedReward(new RolvBallTravelReward(
                //    /*selfToSelf*/    0.2f,   // dribble credit
                //    /*selfToTm8*/     1.0f,   // pass sender
                //    /*tm8ToSelf*/     1.0f,   // pass receiver
                //    /*selfToOpp*/    -1.0f,   // turnover penalty
                //    /*oppToSelf*/     0.5f,   // intercept credit
                //    /*selfToOppGoal*/ 1.0f,   // shot scoring (separate from GoalReward)
                //    /*selfToOwnGoal*/-1.0f    // concede penalty
                //), 25.0f),


                // NEW: Aerial focus
                //WeightedReward(new AirReward(), 0.2f),
                //WeightedReward(new ZeroSumReward(new AirTouchReward(), 0.3f), 50.0f),  // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 0.5f), 50.0f),
                WeightedReward(new ZeroSumReward(new BallRedirectReward(), 0.3f), 100.0f),
                WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.2f), 1.5f),
                //WeightedReward(new DoubleJumpAerialReward(), 50.0f),  // stepping stone: teach pure double-jump before PitchControl can fire
                // PitchControlAerialReward — disabled 2026-07-31: pays up to 27 raw (2.0 base +
                // 3.0 height + 1.0 pitch, ×1.5 boosted) for jump→pitch25-65°→doublejump-within-0.55s
                // with NO ball-distance/relevance gate at all, and isOnGround is true on walls too,
                // so it fires equally for wall-jumps. Suspected root cause of unnecessary off-ball
                // flipping/airtime — see Reward_Change_Log.md.
                //WeightedReward(new PitchControlAerialReward(), 3.0f),

                //WeightedReward(new ZeroSumReward(new EarlyAerialReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.2f), 8.0f), // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
                //WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 10.0f),
                //WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 10.0f),
                WeightedReward(new ZeroSumReward(new MultiAirTouchReward(), 0.2f), 10.0f),
                WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.2f), 20.0f),
                // RolvFlipResetReward: ported from rlgym-tools (credit Rolv-Arild). Introduces
                // flip-reset acquisition/use one phase earlier than the existing FlipReset*
                // trio (Phase 6). NOT ZeroSummed, matching FlipResetReward/SimpleFlipResetReward
                // convention (individual mechanic skill, not directly competitive).
                WeightedReward(new RolvFlipResetReward(), 400.0f),
                WeightedReward(new ZeroSumReward(new UnifiedDoubleTapReward(), 0.2f), 150.0f),
                //WeightedReward(new ZeroSumReward(new JumpTouchReward(), 0.3f), 20.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 40.0f),
                // AirRollShotReward Rev 3.0 (2026-07-07): re-enabled after fixing the gate
                // (was checking isFlipping/dodge-recency instead of genuine air-roll input).
                // Weight 40.0f is a fresh conservative estimate, not the stale pre-fix 100.0f
                // — re-tune once real activation data comes in. See Reward_Change_Log.md.
                //WeightedReward(new ZeroSumReward(new AirRollShotReward(), 0.3f), 80.0f),
                WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.4f), 100.0f),
                WeightedReward(new ZeroSumReward(new AerialOutplayReward(), 0.4f), 60.0f),  // Tactical awareness during aerials
                // WallCeilingRedirectReward — COMMENTED OUT 2026-06-25: provides indirect shaping
                // toward post-bounce position, which masks whether the bot is genuinely pre-reading
                // or just reacting. Replaced by PredictiveBounceReadReward for an explicit signal.
                //WeightedReward(new ZeroSumReward(new WallCeilingRedirectReward([] { WallCeilingRedirectReward::Config c; c.shadowMode = false; return c; }()), 0.3f), 50.0f),

                // PredictiveBounceReadReward (Rev 1.1) — rewards pre-bounce commitment to the
                // post-bounce intercept position. NOT ZeroSummed: individual spatial prediction
                // skill. W&B: PredictiveBounce/FullPreReadRate should rise from ~0 over 500M steps.
                WeightedReward(new PredictiveBounceReadReward(), 40.0f),
                //WeightedReward(new OutplayExplorationReward(), 6.0f),
                // OutplayCreativityReward: inverted/sideways car or low-boost (<30) during outplay touch.
                //WeightedReward(new OutplayCreativityReward(), 2.5f),
                WeightedReward(new ZeroSumReward(new DoomseeDisshShotReward(), 0.3f), 50.0f),
                //WeightedReward(new InvertedAirDribbleReward(), 110.0f), // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
                //WeightedReward(new TornadoSpinTakeoffReward(), 5.0f), // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward

                // NEW: Recovery (needed for aerials)
                //WeightedReward(new HalfFlipReward(), 1.0f),
                //WeightedReward(new ReverseHalfFlipReward(), 3.0f),
                WeightedReward(new RolvWavedashReward(), 4.0f),
                //WeightedReward(new WavedashBasicReward(), 5.0f),
                // WavedashJumpFlipChainReward — disabled 2026-07-31: pays up to 72 raw (15.0 base +
                // 5.0 speed + 4.0 timing, ×3.0 weight) for a wavedash→jump→flip input chain with
                // NO ball-distance/relevance gate at all. Highest unguarded per-event payout found
                // in the investigation — suspected primary cause of unnecessary off-ball flipping.
                // See Reward_Change_Log.md.
                //WeightedReward(new WavedashJumpFlipChainReward(), 3.0f),
                //WeightedReward(new EnhancedSpeedFlipReward(), 16.0f),
                //WeightedReward(new CeilingDoubleJumpReward(), 2.0f),
                //WeightedReward(new DirectionalFlipRecoveryReward(), 0.4f),
                WeightedReward(new ProperLandingReward(), 1.0f),
                WeightedReward(new AdvancedRecoveryReward(), 0.12f),
                WeightedReward(new PowerslideJumpFlipReward(), 0.5f),
                WeightedReward(new BumpedFlipRecoveryReward(), 0.4f),
                WeightedReward(new GoalPostRecoveryReward(), 0.5f),
                //WeightedReward(new SupersonicReward(), 3.0f),
                //WeightedReward(new VelocityReward(), 0.1f),

                //WeightedReward(new BoostDownRecoveryReward(), 1.0f),

                // Powerslide turning mechanics (Phase 3 intro — conservative weights)
                //WeightedReward(new PowerslideSharpTurnReward(), 1.5f),
                //WeightedReward(new Powerslide180ForwardReward(), 1.5f),
                //WeightedReward(new Powerslide180ReverseReward(), 1.5f),
                //WeightedReward(new Powerslide360Reward(), 2.0f),

                //Kickoffs
                // Retired 2026-07-19: pure subset of KickoffSpeedReward + ClosestPlayerKickoffSideReward's
                // closing-velocity signal (see Reward_Change_Log.md). Class kept intact for other phases.
                //WeightedReward(new KickoffVelocityPlayerToBallReward(), 1.0f),
                WeightedReward(new KickoffSpeedReward(), 20.0f),
                WeightedReward(new ZeroSumReward(new KickoffBallPositionReward(), 1.0f), 10.0f),
                //WeightedReward(new ClosestPlayerKickoffSideReward(), 10.0f),
                WeightedReward(new KickoffDoubleCommitPenalty(), 1.8f),
                //WeightedReward(new KickoffStrategyReward(), 15.0f),  // Reward diverse kickoff strategies
                // Kickoff - Penalize team if nobody commits to the ball
                //WeightedReward(new KickoffNoChaseTeamPenalty(), 20.0f),
                // Kickoff - Support players grab corner/mid boost during kickoff (2v2+)
                //WeightedReward(new KickoffSupportBoostReward(), 10.0f),
                //WeightedReward(new KickoffFormReward(), 20.0f),
                // Approach quality: centerline x-alignment × y-direction × speed
                //WeightedReward(new KickoffMassAlignmentReward(), 20.0f),
                // Counter the opponent's kickoff: opp touches first -> redirect back at their goal
                //WeightedReward(new ZeroSumReward(new KickoffCounterHitReward(), 0.3f), 20.0f),
                // Pre-shade laterally when opp speedflip detected; bonus for counter touch
                //WeightedReward(new KickoffCounterpositionReward(), 20.0f),

                // NEW: Boost management (critical for aerials)
                WeightedReward(new PickupBoostReward(0.2f), 90.0f),  // INCREASED; lossWeight=0.2 test (Phase 5 only, see Reward_Change_Log.md)
                //WeightedReward(new SaveBoostReward(), 0.6f),
                WeightedReward(new SmallPadCollectionReward(), 20.0f),
                WeightedReward(new SmallPadRecoveryRouteReward(), 10.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 350.0f),
                //WeightedReward(new ZeroSumReward(new RotationPathBoostDenialReward(), 0.5f), 60.0f),
                //WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 3.5f),
                //WeightedReward(new BoostWastePenaltyReward(), 15.0f),
                //WeightedReward(new KickoffBoostRetentionReward(), 2.0f),
                //WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 5.0f),
                //WeightedReward(new BigBoostApproachReward(), 5.0f),      // leave commented - isolating this test
                //WeightedReward(new PadCenterAccuracyReward(), 5.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 10.0f),

                // NEW: Dribbling
                WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 0.5f),
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.3f), 1.0f),
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.4f), 0.15f),
                // LateralPushDribbleReward (Rev 1.1, 2026-06-30): grounded push-dribble toward
                // far-corner<->net cone, kept slow (<2100 UU/s) and flat (|velZ|<300). Live at
                // conservative weight per rollout plan step 2. W&B: see GetAndResetMetrics().
                //WeightedReward(new LateralPushDribbleReward(), 2.5f),
                // DoubleJumpPopReward (Rev 1.1, 2026-06-30): carry (ball-on-roof grounded
                // >=0.3s) -> jump -> neutral double jump -> controlled height-gain pop, with
                // an air-dribble follow-through bonus. Not ZeroSummed — individual mechanic
                // execution, not a competitive exchange. No dedicated state setter yet;
                // relies on organic carries from CatchBallOnCarReward/DribbleMasteryReward.
                // W&B: see GetAndResetMetrics().
                //WeightedReward(new DoubleJumpPopReward(), 35.0f),
                //WeightedReward(new ZeroSumReward(new CatchBallReward(), 0.4f), 0.5f),
                //WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 1.0f),
                //WeightedReward(new ZeroSumReward(new FlickReward(), 0.5f), 20.0f),
                //WeightedReward(new ZeroSumReward(new AdvancedFlickReward(), 0.5f), 60.0f),
                WeightedReward(new ZeroSumReward(new CompositeFlickReward(), 0.3f), 10.0f),
                WeightedReward(new ZeroSumReward(new FlickOutplayReward(), 0.5f), 30.0f),
                WeightedReward(new ZeroSumReward(new FrontFlipFlickReward(), 0.5f), 25.0f),  // 2026-07-29 re-enabled: was never active in any phase, likely contributor to bot not flicking past defenders in 2v2/3v3
                WeightedReward(new ZeroSumReward(new DiagonalFlickReward(), 0.5f), 25.0f),   // 2026-07-29 re-enabled: was never active in any phase, likely contributor to bot not flicking past defenders in 2v2/3v3
                WeightedReward(new ZeroSumReward(new SideFlickReward(), 0.5f), 15.0f),      // 2026-07-29 re-enabled: was never active in any phase, likely contributor to bot not flicking past defenders in 2v2/3v3
                //WeightedReward(new ZeroSumReward(new BallSteerReward(), 0.4f), 0.4f),                                // 2026-06-19 ball-steering through dribble turns

                // NEW: Basic Shots
                WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 20.0f),
                WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 30.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f), // updated reward on 01/30/26
                // ShotExplorationReward: wide-angle, height variety, and field-position shot bonuses.
                //WeightedReward(new ShotExplorationReward(), 12.0f),
                // ShotCreativityReward: inverted/sideways car, redirect geometry, first-touch shots.
                //WeightedReward(new ShotCreativityReward(), 12.0f),

                // NEW: Minor possession shaping and defense
                //WeightedReward(new DefensivePositionReward(), 1.0f),
                //WeightedReward(new ZeroSumReward(new ChallengeAngleQualityReward(), 0.3f), 0.5f),  // Angle of challenge approach vs threat vector
                //WeightedReward(new BackboardDefenseReward(), 4.5f),
                //WeightedReward(new ZeroSumReward(new BackboardInterceptionReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 0.6f),  // P4: 2.5 → 2.0
                //WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 8.0f),   // P4: 10.0 → 1.0 (light intro)
                WeightedReward(new ZeroSumReward(new PossessionControlRewardv2(), 1.0f), 2.0f),   // P4: [v2: threat-gated]
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 0.5f), 1.0f),
                //WeightedReward(new ZeroSumReward(new ShadowDefenseReward(), 0.5f), 1.3f),
                WeightedReward(new ZeroSumReward(new SingleJumpLow5050Reward(), 0.5f), 2.5f),

                // Third-man tactics (SHADOW, M2 validation). Stall MUST precede CounterLaunch:
                // StallReward.PreStep calls ctx.Update(); CounterLaunch reads from it.
                // Self-gates to 0 in 1v1 (livingCount < 2). No policy impact while shadowMode=true.
                //WeightedReward(new ThirdManStallReward(*thirdManCtx,
                    //[] { ThirdManStallReward::Config c; c.shadowMode = false; return c; }()), 30.0f),

                // Fake plays (Rev 2, 2026-07-07): offensive fake shot (strict convincing-sell
                // trigger) + defensive fake challenge (zone-gated away from own net).
                // W&B: FakeChallenge/Off/* and FakeChallenge/Def/*
                //WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 1.5f),
                //WeightedReward(new ZeroSumReward(new FakeChallengeDefenseReward(), 1.0f), 5.0f),
                WeightedReward(new DefensivePositioningChallengeReward(), 0.9f),
                //WeightedReward(new LastManOvercommitPenalty(), 10.0f),
                //WeightedReward(new UncoveredNetPenalty(), 4.0f),
                //WeightedReward(new OpponentInterceptAwarePenalty(), 10.0f),
                //WeightedReward(new ZeroSumReward(new ImpendingShotDefenseReward(), 0.5f), 30.0f),
                //WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 8.0f),
                WeightedReward(new ZeroSumReward(new BigClearReward(), 0.5f), 14.0f),
                WeightedReward(new ZeroSumReward(new CornerClearReward(), 0.5f), 10.0f),
                //WeightedReward(new PoorWallReboundPenalty(), 80.0f),
                WeightedReward(new ZeroSumReward(new OpponentPassInterceptionReward(), 1.0f), 25.0f),
                //WeightedReward(new ZeroSumReward(new BallLandingPositionReward(), 1.0f), 30.0f),
                //WeightedReward(new GoalpostPositioningReward(), 12.0f),
                //WeightedReward(new EarlyDangerDetectionReward(), 1.0f),
                WeightedReward(new EmergencySaveCommitReward(), 3.0f),
                //WeightedReward(new SimpleOvercommitmentPenalty(), 15.0f),  // Phase 1: Resource management & positioning discipline. 35.0f->26.0f 2026-07-21: Gate 3/5 tax uncontested 1v1 forward commitment, see Reward_Change_Log.md
                WeightedReward(new DoubleCommitPenalty(), 100.0f),
                //WeightedReward(new ActiveSupportingPositionReward(), 10.0f),
                //WeightedReward(new LowBoostOppHalfFiftyPunish(), 100.0f),
                //WeightedReward(new ClosestPlayerSpeedReward(), 5.0f),
                WeightedReward(new OffBallAerialPenalty(), 15.0f),
                //WeightedReward(new SecondaryAerialPenalty(), 15.0f),
                //WeightedReward(new NonPrimaryAirPenalty(), 15.0f),

                // Ground readiness suite (2026-06-26)
                // FlipDuringContestPenalty: penalizes mid-flip when another player is near ball
                //WeightedReward(new FlipDuringContestPenalty(), 0.2f),
                // GroundedAtContactReward: rewards being grounded when any other player touches ball
                WeightedReward(new GroundedAtContactReward(), 0.08f),
                // AirborneOnPossessionLostPenalty: penalizes being airborne when opponent gains ball
                WeightedReward(new AirborneOnPossessionLostPenalty(), 0.8f),

                // NEW: Passing rewards
                //WeightedReward(new ZeroSumReward(new BackPassReward(), 1.0f), 4.0f),
                //WeightedReward(new ZeroSumReward(new PassingLaneNetworkReward(), 1.0f), 9.0f),
                //WeightedReward(new ZeroSumReward(new SpatialFakeReward(), 1.0f), 9.0f),
                WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 24.0f),
                WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 12.0f),
                WeightedReward(new InfieldPassReward(), 25.0f),
                WeightedReward(new DownfieldPassReward(), 25.0f),
                //WeightedReward(new PassToOpponentPenalty(), 60.0f),

                // Rotation handled entirely by ProgressiveRotationReward (includes spacing + retreat internally)
                //WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 2.0f),
                //WeightedReward(new InstantSpacingReward(RotationPhase::CHAMPION_PLUS), 4.0f),  // UPGRADED
                //WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 2.0f),  // UPGRADED
                //WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 10.0f),
                //WeightedReward(new BoostAwareRotationReward(RotationPhase::CHAMPION_PLUS), 3.0f),

                // Positioning — 2v2/3v3 zone discipline (promoted from Champion-only to Diamond,
                // 2026-07-07): these 5 were fully implemented but only wired in at Phase 6+, so
                // they were dormant during Diamond training. Weights copied unchanged from the
                // Champion block (the only validated setting we have) rather than guessed fresh —
                // see Reward_Change_Log.md.
                //WeightedReward(new AntiLayeringPenalty(), 10.0f),
                //WeightedReward(new DefensiveZoneOverlapPenalty(), 10.0f),
                //WeightedReward(new OverflipPenalty(), 10.0f),
                //WeightedReward(new DiagonalZoneCoverageReward(), 2.0f),
                //WeightedReward(new NoMansLandPenalty(), 6.0f),

                // NEW: Numerical advantage attacking (2v1+ situations)
                //WeightedReward(new ZeroSumReward(new NumericalAdvantageAttackingReward(), 1.0f), 50.0f),
                //WeightedReward(new ZeroSumReward(new BallSideNumericalAdvantageReward(), 1.0f), 2.0f),  // Press when opponent over-advances past ball
                //WeightedReward(new ZeroSumReward(new CounterAttackReward(), 1.0f), 10.0f),  // Fast-break bonus: capitalize on turnovers when opponents caught upfield
                WeightedReward(new GoalsideReward(), 0.05f),  // Goalside fundamental: +1 between ball and own net, -0.4 offside (smooth 800uu ramp)

                // ShotLaneCoverageReward (Rev 1.0, 2026-07-24): general, outcome-based
                // alternative to the hardcoded zone-discipline set above (AntiLayeringPenalty
                // etc, still left commented). Rewards whichever teammate best occludes the
                // actual ball->goal shot lane, not a fixed zone or slot. One variable this
                // iteration per Reward_Change_Log.md. W&B: shot_lane_coverage_frac,
                // shot_lane_uncovered_frac via ShotLaneCoverageReward::GetAndResetMetrics().
                //WeightedReward(new ZeroSumReward(new ShotLaneCoverageReward(), 0.3f), 8.0f),

                //WeightedReward(new DemoAwarenessReward(), 2.0f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 1.0f), 400.0f),
                //WeightedReward(new BumpedPenalty(), 80.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 1.0f), 600.0f),
                WeightedReward(new ZeroSumReward(new RolvDemoReward(1.0f, 1.0f, 1.0f), 0.5f), 1200.0f),
                //WeightedReward(new DemoLeadApproachReward(), 60.0f),
                //WeightedReward(new ZeroSumReward(new DemoInterceptLeadReward(), 1.0f), 60.0f),
                //WeightedReward(new ZeroSumReward(new DemoOpportunityReward(), 1.0f), 100.0f),
                //WeightedReward(new ThirdManDemoReward(), 240.0f),
                //WeightedReward(new ZeroSumReward(new DemoAdvantageReward(), 1.0f), 60.0f),
                // Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/demo_reward.py

                    //WeightedReward(new ZeroSumReward(new PlayerVelocityToOpponentReward(), 1.0f), 5.0f),
                WeightedReward(new ZeroSumReward(new ShotPlacementReward(), 0.5f), 200.0f),
                WeightedReward(new ZeroSumReward(new SpeedScaledShotReward(), 0.5f), 200.0f),
                WeightedReward(new ZeroSumReward(new ShotReward(), 1.0f), 200.0f),
                // FreeShotConversionReward (Rev 1.0, 2026-08-02): rewards converting a shot
                // quickly and cleanly when no living opponent can reach the shot lane in time
                // to block it (all overcommitted/out of position), including give-and-go setups
                // (shooter-only credit — see Mechanic Spec / ClaudeFiles for scenario coverage).
                // W&B: FreeShot/WindowsOpened, FreeShot/WindowsConverted,
                // FreeShot/AvgTimeToConvert, FreeShot/RewardSum via GetAndResetMetrics().
                WeightedReward(new ZeroSumReward(new FreeShotConversionReward(), 0.5f), 80.0f),
                //WeightedReward(new ZeroSumReward(new ShotPassReward(), 1.0f), 100.0f),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 100.0f),
                WeightedReward(new GoalReward(-0.8f), 1900.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 100.0f)
        };
    }
    case TrainingPhase::PHASE_6_CHAMPION:
    {
        return std::vector<WeightedReward>{
                WeightedReward(new ZeroSumReward(new StrongTouchReward(100, 170), 0.3f), 80.0f),  // Stricter
                WeightedReward(new ZeroSumReward(new FlipIntoContactReward(), 0.3f), 20.0f),
                WeightedReward(new ZeroSumReward(new HalfVolleyReward(), 0.3f), 20.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 120.0f),
                WeightedReward(new PowerSlideControlReward(), 0.50f),
                WeightedReward(new GoalReward(-0.8f), 2000.0f),
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 20.0f),  // 35→25→20: near-dead signal, migrating to enhanced
                WeightedReward(new ZeroSumReward(new EnhancedVelocityBallToGoalReward(), 1.0f), 40.0f),  // New enhanced variant (20% of original weight)
                // GoalViewReward: potential-based shaping via goal-mouth solid angle (Rolv / Ng et al. 1999).
                // NOT wrapped in ZeroSumReward — the class already returns +/- by team internally.
                // Next iteration: reduce VelocityBallToGoalReward 20→10 once this is confirmed healthy in W&B.
                WeightedReward(new GoalViewReward(0.99f), 5.0f),
                // CornerToLaneReward: rewards moving ball toward center lane when in opponent corner.
                WeightedReward(new ZeroSumReward(new CornerToLaneReward(), 0.5f), 5.0f),
                //WeightedReward(new EnhancedVelocityBallToGoalReward(), 2.0f),
                // GoalMultiplierReward is intentionally separate from GoalReward rather than
                // wrapped in MathOp::MULTIPLY. Using MULTIPLY would zero-out the concede penalty
                // (GoalReward returns -0.75 for conceder, GoalMultiplierReward returns 0 for
                // non-scorers, so -0.75 * 0 = 0). Keeping them separate preserves the full
                // concede penalty while still paying the scorer a 1.0-3.0x style bonus.
                // At weight 650 (half of 1300), a plain goal adds +650, a max-style goal adds +1950.
                //WeightedReward(new ZeroSumReward(new GoalMultiplierReward(), 1.0f), 300.0f),
                // OwnGoalPenalty: fires only when the conceding team last touched the ball.
                // No ZeroSum needed — it already returns 0 to the opposing team.
                // Weight matches GoalReward so an own goal stings as much as conceding a regular goal.
                WeightedReward(new OwnGoalPenalty(-1.0f), 1200.0f),
                // PoorWallReboundPenalty: fires when a side-wall touch rebounds to opponent possession.
                WeightedReward(new PoorWallReboundPenalty(), 30.0f),

                // Aerial focus
                WeightedReward(new ZeroSumReward(new PitchControlAerialReward(), 0.4f), 2.0f),
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 1.0f), 60.0f),
                WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.5f), 20.0f),  // Rev 2.1
                //WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.5f), 70.0f),
                //WeightedReward(new AirDribbleSetupReward(), 8.0f),
                //WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new AirDribbleBumpReward(), 1.0f), 18.0f),  // Reward bumping opponent during air dribble toward goal
                WeightedReward(new ZeroSumReward(new AirDribbleBumpConversionReward(), 0.5f), 60.0f),
                WeightedReward(new FlipResetApproachReward(), 60.0f),
                //WeightedReward(new SimpleFlipResetReward(), 400.0f),
                WeightedReward(new UnifiedAirDribbleReward(), 30.0f),
				WeightedReward(new FlipResetReward(), 50.0f), // new reward for achieving flip reset state (car regains jump after touching ball in air)
                // SimpleFlipResetReward retired 2026-08-01: replaced by RolvFlipResetReward in
                // PHASE_5_DIAMOND (facing-gated touch trigger + productive-use validation is a
                // stricter superset of this flat trigger). See Reward_Change_Log.md.
                //WeightedReward(new SimpleFlipResetReward(), 350.0f),
                WeightedReward(new ZeroSumReward(new UnifiedDoubleTapReward(), 0.3f), 150.0f),
                //WeightedReward(new AirReward(), 0.25f),   // or whatever weight fits your phase
                WeightedReward(new AirRollReward(), 5.0f),   // or whatever weight fits your phase
                WeightedReward(new InvertedAerialReward(), 5.0f),  // inverted aerial (wheels-up flying)
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 5.0f),
                WeightedReward(new ReverseAerialReward(), 4.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new AerialOutplayReward(), 0.3f), 60.0f),  // Tactical awareness during aerials

                // PredictiveBounceReadReward (Rev 1.1) — rewards pre-bounce commitment to the
                // post-bounce intercept position. NOT ZeroSummed: individual spatial prediction skill.
                WeightedReward(new PredictiveBounceReadReward(), 20.0f),

                // NEW: Dribbling
                WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 0.25f),
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.3f), 1.0f),
                // NOTE: DribbleMasteryReward is now wrapped with option control above
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.4f), 0.5f),
                WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 0.5f),
                WeightedReward(new ZeroSumReward(new CompositeFlickReward(), 0.5f), 10.0f),
                // FlickOutplayReward: dribble flick executed while opponent is actively
                // challenging (within 1200 UU, moving toward ball > 500 UU/s or already jumped).
                // Quality: on-goal=0.9, pop-up=0.35, toward-goal+away=0.35, behind=up-to+0.15.
                // Max payout = 10 + 10*quality (internal). ZeroSum: opponent loses what scorer gains.
                WeightedReward(new ZeroSumReward(new FlickOutplayReward(), 1.0f), 10.0f),
                // OutplayExplorationReward: aerial outplay, wall-position outplay, sharp redirect geometry.
                // Fills gaps FlickOutplayReward/CutOutplayReward miss (both require ground/specific context).
                WeightedReward(new OutplayExplorationReward(), 5.0f),
                // OutplayCreativityReward: inverted/sideways car or low-boost (<30) during outplay touch.
                WeightedReward(new OutplayCreativityReward(), 2.0f),
                // FlickVarietyReward: bonuses for side/behind/underside/inverted flick varieties.
                // Complements CompositeFlick (covers front/diagonal/musty). No bonus for standard front flicks.
                WeightedReward(new FlickVarietyReward(), 8.0f),
                // AirDribbleExplorationReward: orientation diversity + touch rhythm + power air touches.
                // Three additive channels rewarding airborne ball contact in non-standard patterns.
                WeightedReward(new AirDribbleExplorationReward(), 3.0f),

                // Recovery
                WeightedReward(new HalfFlipReward(), 1.25f),
                WeightedReward(new WavedashBasicReward(), 0.5f),
                //WeightedReward(new WaveDashHeightReward(), 2.0f),
                //WeightedReward(new WallDashReward(), 15.0f),
                WeightedReward(new WallDashInputReward(), 10.0f),
                WeightedReward(new EnhancedSpeedFlipReward(), 12.0f),
                WeightedReward(new ProperLandingReward(), 1.5f),
                WeightedReward(new AdvancedRecoveryReward(), 0.5f),
                WeightedReward(new BumpedFlipRecoveryReward(), 0.3f),
                // RecoveryExplorationReward: ceiling wavedash, wall-to-wall wavedash,
                // squishy save momentum, boost-to-surface. Complements IntelligentWavedash + WavedashBasic.
                WeightedReward(new RecoveryExplorationReward(), 0.5f),
                // 2026-04-19 | NEW: Penalize sustained airborne time far from ball.
                // Counterpressure against wall-flip approaches that commit the bot to
                // multi-second aerials when the ball isn't in a viable position.
                // Grace period = 0.5s; kicks in when ball > 3000 UU away.
                // Not ZeroSummed: per-player behavioral penalty only.
                WeightedReward(new OffBallAerialPenalty(), 10.0f),
                WeightedReward(new SecondaryAerialPenalty(), 8.0f),
                WeightedReward(new NonPrimaryAirPenalty(), 6.0f),
                WeightedReward(new SupersonicReward(), 8.0f),

                // Kickoff
                WeightedReward(new ClosestPlayerSpeedReward(), 7.0f),
                WeightedReward(new KickoffSpeedReward(), 15.0f),
                WeightedReward(new ClosestPlayerKickoffSideReward(), 30.0f),
                // Counter the opponent's kickoff: opp touches first -> redirect back at their goal
                WeightedReward(new ZeroSumReward(new KickoffCounterHitReward(), 0.3f), 12.0f),
                // Pre-shade laterally when opp speedflip detected; bonus for counter touch
                WeightedReward(new KickoffCounterpositionReward(), 8.0f),
                WeightedReward(new KickoffDoubleCommitPenalty(), 45.0f),
                //WeightedReward(new SpeedFlipKickoffReward(), 2.0f),
                //WeightedReward(new DiagonalBoostKickoffReward(), 4.0f),
                //WeightedReward(new DelayedKickoffReward(), 2.0f),
                //WeightedReward(new FakeKickoffReward(), 2.0f),
                //WeightedReward(new ReactiveKickoffReward(), 2.0f),
                //WeightedReward(new PredictiveSecondManReward(), 2.0f),
                //WeightedReward(new SpanishDemoKickoffReward(), 2.0f),
                // Kickoff - Penalize team if nobody commits to the ball
                //WeightedReward(new KickoffNoChaseTeamPenalty(), 30.0f),
                // Kickoff - Support players grab corner/mid boost during kickoff (2v2+)
                WeightedReward(new KickoffSupportBoostReward(), 7.5f),

                // Boost management
                WeightedReward(new PickupBoostReward(), 75.0f),  // INCREASED
                WeightedReward(new SaveBoostReward(), 0.65f),
                WeightedReward(new SmallPadCollectionReward(), 12.0f),
                WeightedReward(new BoostPathReward(), 30.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 140.0f),   // 100→50: 0.86% activation rate, effective contribution near-zero
                WeightedReward(new ZeroSumReward(new RotationPathBoostDenialReward(), 0.5f), 60.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 3.0f),
                WeightedReward(new BoostWastePenaltyReward(), 20.0f),
                WeightedReward(new SupersonicBoostPenalty(), 25.0f),
                WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 12.0f),
                WeightedReward(new PadCenterAccuracyReward(), 25.0f),   // 20→10: very low effective yield (0.005)
                WeightedReward(new BigBoostApproachReward(), 8.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 20.0f),  // 20→14: low effective yield (0.026)

                // Basic Shots and Outplays
                //WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 30.0f),
                //WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 30.0f),
                //WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f), // updated reward on 01/30/26. Added to Option Control above
                // 2026-04-18 | 100→150 (+50%): shift weight toward outcome-quality placement
                // (defender-coverage ballistic scoring) and away from shot-event proxies.
                WeightedReward(new ShotPlacementReward(), 200.0f),
                // ShotExplorationReward: wide-angle, height variety, and field-position shot bonuses.
                WeightedReward(new ShotExplorationReward(), 15.0f),
                // ShotCreativityReward: inverted/sideways car, redirect geometry, first-touch shots.
                WeightedReward(new ShotCreativityReward(), 15.0f),
                WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 1.5f), // Added to Option Control above
                WeightedReward(new ZeroSumReward(new FakeChallengeDefenseReward(), 1.0f), 3.0f), // Rev 2 zone-gated defensive fake (2026-07-07)
                WeightedReward(new ZeroSumReward(new AirRollShotReward(), 0.3f), 10.0f),
                WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.3f), 70.0f),
                WeightedReward(new ZeroSumReward(new DoomseeDisshShotReward(), 0.3f), 25.0f),
                WeightedReward(new InvertedAirDribbleReward([]{ InvertedAirDribbleReward::Config c; c.shadowMode = false; return c; }()), 5.0f),
                WeightedReward(new TornadoSpinTakeoffReward([]{ TornadoSpinTakeoffReward::Config c; c.shadowMode = false; return c; }()), 2.0f),
                WeightedReward(new ChallengeCommitmentReward(), 10.0f),
                WeightedReward(new AerialCommitmentReward(), 15.0f),
                //WeightedReward(new ZeroSumReward(new WallPinchReward(), 0.2f), 3.0f),

                // Positioning — 2v2/3v3 anti-layering (same Y-band penalty)
                WeightedReward(new AntiLayeringPenalty(), 15.0f),
                WeightedReward(new DefensiveZoneOverlapPenalty(), 10.0f),
                WeightedReward(new OverflipPenalty(), 15.0f),
                WeightedReward(new DiagonalZoneCoverageReward(), 10.0f),
                WeightedReward(new NoMansLandPenalty(), 10.0f),

                // Solo play — multi-touch combo + dual threat
                WeightedReward(new ZeroSumReward(new SoloPlayComboReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new DualThreatReward(),    1.0f), 10.0f),

                // NEW: Minor possession shaping and defense
                WeightedReward(new DefensivePositionReward(), 2.5f),
                //WeightedReward(new DefensiveReadinessReward(), 0.5f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f),  //  teaches the bot to get to the ball before the opponent.
                WeightedReward(new ZeroSumReward(new PossessionControlRewardv2(), 1.0f), 5.0f),   // teaches it what to do with the ball once it has it. [v2: threat-gated]
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 1.0f), 2.5f),
                WeightedReward(new ZeroSumReward(new BigClearReward(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new CornerClearReward(), 0.5f), 3.0f),
                WeightedReward(new DefensivePositioningChallengeReward(), 2.0f),
                WeightedReward(new LastManOvercommitPenalty(), 6.0f),
                WeightedReward(new UncoveredNetPenalty(), 4.0f),
                WeightedReward(new OpponentInterceptAwarePenalty(), 10.0f),
                WeightedReward(new ImpendingShotDefenseReward(), 10.0f),
                WeightedReward(new ZeroSumReward(new ChallengeAngleQualityReward(), 1.0f), 1.5f),  // Angle of challenge approach vs threat vector
                WeightedReward(new BackboardDefenseReward(), 5.0f),
                WeightedReward(new ZeroSumReward(new BackboardInterceptionReward(), 0.5f), 8.0f),
                WeightedReward(new EarlyDangerDetectionReward(), 0.9f),
                WeightedReward(new LowBoostOppHalfFiftyPunish(), 20.0f),
                WeightedReward(new DoubleCommitPenalty(), 50.0f),
                WeightedReward(new SimpleOvercommitmentPenalty(), 10.0f),  // Phase 1: Resource management & positioning discipline
                //WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new OpponentPassInterceptionReward(), 1.0f), 25.0f),
                //WeightedReward(new GoalpostPositioningReward(), 10.0f),
                WeightedReward(new OwnNetCenteringPenalty(), 50.0f),
                WeightedReward(new EmergencySaveCommitReward(), 2.5f),
                WeightedReward(new CornerPlayPenalty(), 50.0f),

                // Passing rewards
                WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 40.0f),
                WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 20.0f),  
                WeightedReward(new PassToOpponentPenalty(), 40.0f),

                // Spatial lane control (team modes only — returns 0 in 1v1)
                WeightedReward(new PassingLaneNetworkReward(PassingLaneNetworkReward::Config::Full()), 6.0f),
                WeightedReward(new SpatialFakeReward(), 6.0f),
                // Infield (lateral switch) and downfield (long ball) passing.
                // Not ZeroSummed: individual pass-quality signals, not contested outcomes.
                // Both skip 1v1 internally (players.size() < 4).
                WeightedReward(new PressureEscapeBackPassReward(), 2.0f),
                WeightedReward(new InfieldPassReward(), 15.0f),
                WeightedReward(new DownfieldPassReward(), 15.0f),

                // NEW: Retreat after touch (prepare for rotation), rotation rewards
                WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 2.0f),
                WeightedReward(new InstantSpacingReward(RotationPhase::CHAMPION_PLUS), 5.0f),  // UPGRADED
                WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 12.0f),
                WeightedReward(new BoostAwareRotationReward(RotationPhase::CHAMPION_PLUS), 2.0f),

                // NEW: Numerical advantage attacking (2v1+ situations)
                // Weight reduced 70 -> 35: was driving excessive ball-chasing via per-frame
                // aggression bonus for any player in the attacking zone.
                WeightedReward(new ZeroSumReward(new NumericalAdvantageAttackingReward(), 1.0f), 100.0f),  // 30→18: 0.032 effective, tiny mean vs high activation — 30 was wasted signal mass
                WeightedReward(new ZeroSumReward(new BallSideNumericalAdvantageReward(), 1.0f), 0.5f),  // Press when opponent over-advances past ball
                WeightedReward(new ZeroSumReward(new CounterAttackReward(), 1.0f), 5.0f),  // Fast-break bonus: capitalize on turnovers when opponents caught upfield
                WeightedReward(new GoalsideReward(), 2.0f),  // Goalside fundamental: +1 between ball and own net, -0.4 offside (smooth 800uu ramp)
                // 2nd-man upfield holding: fires only for MIDDLE-role player when teammate has
                // possession. Rewards staying 1000-5000 UU upfield (closer to opp goal than ball).
                // Counteracts redundant ball-chasing when a teammate is already the primary.
                WeightedReward(new ActiveSupportingPositionReward(), 24.0f),
                //WeightedReward(new DeepBackPostCoverageReward(), 0.5f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 500.0f),
                WeightedReward(new BumpReward(), 200.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 750.0f),
                WeightedReward(new SecondManOffensiveDemoReward(), 200.0f),
                WeightedReward(new ThirdManDemoReward(), 150.0f),
                WeightedReward(new ZeroSumReward(new PossessionSideDemoReward(), 1.0f), 250.0f),
                WeightedReward(new DemoReward(), 250.0f),
                // Demo opponent who is predicted to intercept a goal-bound ball.
                // Fires only when BallPred shows ball heading to opp goal AND target is on trajectory.
                WeightedReward(new ZeroSumReward(new InterceptClearingDemoReward(), 0.5f), 200.0f),
                WeightedReward(new DemoedPenalty(), 60.0f),
                WeightedReward(new ZeroSumReward(new ContextualDemoReward(), 0.5f), 40.0f),  // 80→40: 0.09% activation rate, near-dead signal
                WeightedReward(new ZeroSumReward(new DemoOpportunityReward(), 1.0f), 60.0f),
                WeightedReward(new ZeroSumReward(new SpeedScaledShotReward(), 0.5f), 200.0f),
                WeightedReward(new ZeroSumReward(new BallRedirectReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 50.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 50.0f)
        };
    }

    case TrainingPhase::PHASE_7_GC:
    case TrainingPhase::PHASE_8_SSL:
    case TrainingPhase::PHASE_9_PRO:
    {
        // ====================================================================
        // PHASES 7-9: GRAND CHAMPION+ - Full Phase 2 Emphasis
        // ====================================================================
        // Minimize Phase 1 (reactive), maximize Phase 2 (predictive)
        // Higher complexity rewards for elite-level play
        // ====================================================================

        // Use Phase 6 as base with adjusted overcommitment weights
        auto baseRewards = std::vector<WeightedReward>{
            // (Same core rewards as Phase 6, with overcommitment weight adjustments)
            WeightedReward(new ZeroSumReward(new StrongTouchReward(80, 140), 0.3f), 25.0f),
            WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 30.0f),
            WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 12.0f),  // 15→12: reducing for migration
            WeightedReward(new ZeroSumReward(new EnhancedVelocityBallToGoalReward(), 1.0f), 3.0f),  // New enhanced variant (20% of original weight)
            // GoalViewReward: potential-based shaping via goal-mouth solid angle (Rolv / Ng et al. 1999).
            // NOT wrapped in ZeroSumReward — the class already returns +/- by team internally.
            WeightedReward(new GoalViewReward(0.99f), 5.0f),
            // CornerToLaneReward: rewards moving ball toward center lane when in opponent corner.
            WeightedReward(new ZeroSumReward(new CornerToLaneReward(), 0.5f), 5.0f),
            WeightedReward(new PowerSlideControlReward(), 0.55f),
            WeightedReward(new GoalReward(-1.0f), 1000.0f),
            // PoorWallReboundPenalty: fires when a side-wall touch rebounds to opponent possession.
            WeightedReward(new PoorWallReboundPenalty(), 30.0f),

            WeightedReward(new ZeroSumReward(new PitchControlAerialReward(), 0.4f), 10.0f),
            WeightedReward(new ZeroSumReward(new FirstToBallReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new EarlyAerialReward(), 0.5f), 5.0f),
            WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.5f), 20.0f),  // Rev 2.1
            WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.5f), 60.0f),
            WeightedReward(new ZeroSumReward(new AirDribbleBumpReward(), 1.0f), 20.0f),  // Reward bumping opponent during air dribble toward goal
            WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 5.0f), // restored taper (was 25 — contradicted "covered by AerialFirstTouchReward" comment elsewhere; AerialFirstTouchReward is active here too)
            WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 0.3f), 12.0f),
            WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 20.0f),
            WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 20.0f),

            WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 0.5f),
            WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.3f), 2.0f),
            WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.4f), 1.0f),
            WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 1.0f),
            WeightedReward(new ZeroSumReward(new CompositeFlickReward(), 0.3f), 11.0f),
            // NOTE: CompositeFlickReward combines AdvancedFlickReward (6.0), BackFlipFlickReward (1.0),
            //       MustyFlickReward (2.0), and FlickExplorationReward (2.0)

            WeightedReward(new HalfFlipReward(), 1.5f),
            WeightedReward(new WavedashBasicReward(), 1.0f),
            WeightedReward(new EnhancedSpeedFlipReward(), 10.0f),
            WeightedReward(new ProperLandingReward(), 0.5f),
            WeightedReward(new AdvancedRecoveryReward(), 0.3f),
            WeightedReward(new BumpedFlipRecoveryReward(), 0.1f),
            WeightedReward(new SecondaryAerialPenalty(), 8.0f),
            WeightedReward(new NonPrimaryAirPenalty(), 6.0f),
            WeightedReward(new SupersonicReward(), 3.0f),

            WeightedReward(new PickupBoostReward(), 33.0f),
            WeightedReward(new SaveBoostReward(), 0.55f),
            WeightedReward(new SmallPadCollectionReward(), 2.0f),
            WeightedReward(new BoostPathReward(), 6.0f),
            WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 50.0f),
            WeightedReward(new ZeroSumReward(new RotationPathBoostDenialReward(), 0.5f), 40.0f),
            WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 4.0f),
            WeightedReward(new BoostWastePenaltyReward(), 8.0f),
            WeightedReward(new SupersonicBoostPenalty(), 9.0f),
            // Counter the opponent's kickoff: opp touches first -> redirect back at their goal
            WeightedReward(new ZeroSumReward(new KickoffCounterHitReward(), 0.3f), 12.0f),
            WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
            WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 6.0f),
            WeightedReward(new PadCenterAccuracyReward(), 20.0f),
            WeightedReward(new BigBoostApproachReward(), 8.0f),
            WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

            WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 10.0f),
            WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f),
            WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 1.5f),
            WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.3f), 35.0f),
            WeightedReward(new ZeroSumReward(new DoomseeDisshShotReward(), 0.3f), 35.0f),
            WeightedReward(new InvertedAirDribbleReward([]{ InvertedAirDribbleReward::Config c; c.shadowMode = false; return c; }()), 5.0f),
            WeightedReward(new TornadoSpinTakeoffReward([]{ TornadoSpinTakeoffReward::Config c; c.shadowMode = false; return c; }()), 2.0f),
            WeightedReward(new ZeroSumReward(new ChallengeCommitmentReward(), 0.5f), 10.0f),
            WeightedReward(new ZeroSumReward(new AerialCommitmentReward(), 0.5f), 15.0f),

            WeightedReward(new DefensivePositionReward(), 2.5f),
            WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.0f),  // P4: 2.5 → 2.0
            WeightedReward(new ZeroSumReward(new PossessionControlRewardv2(), 1.0f), 6.0f),   // P4: 15.0 → 6.0 [v2: threat-gated]
            WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 1.0f), 1.9f),
            WeightedReward(new ZeroSumReward(new BigClearReward(), 0.5f), 10.0f),
            WeightedReward(new ZeroSumReward(new CornerClearReward(), 0.5f), 3.0f),
            WeightedReward(new DefensivePositioningChallengeReward(), 2.5f),
            WeightedReward(new LastManOvercommitPenalty(), 5.0f),
            WeightedReward(new UncoveredNetPenalty(), 4.0f),
            WeightedReward(new BackboardDefenseReward(), 5.0f),
            WeightedReward(new ZeroSumReward(new BackboardInterceptionReward(), 0.5f), 8.0f),
            WeightedReward(new EarlyDangerDetectionReward(), 1.2f),
            WeightedReward(new DoubleCommitPenalty(), 5.0f),

            // ====================================================================
            // PHASE 2 OVERCOMMITMENT (FULL EMPHASIS for Phases 7-9)
            // ====================================================================
            // Minimal Phase 1 (5.0f), maximum Phase 2 (20.0f + 18.0f)
            WeightedReward(new SimpleOvercommitmentPenalty(), 5.0f),  // Minimal Phase 1
            WeightedReward(new AdvancedOvercommitmentPenalty(), 20.0f),  // Full Phase 2 emphasis
            WeightedReward(new AdvancedOpponentOvercommitmentReward(), 18.0f),  // Full opponent exploit

            WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 15.0f),
            WeightedReward(new ZeroSumReward(new OpponentPassInterceptionReward(), 1.0f), 15.0f),
            WeightedReward(new GoalpostPositioningReward(), 10.0f),
            WeightedReward(new ActiveSupportingPositionReward(), 8.0f),
            WeightedReward(new DeepBackPostCoverageReward(), 2.5f),
            WeightedReward(new OwnNetCenteringPenalty(), 15.0f),
            WeightedReward(new EmergencySaveCommitReward(), 4.0f),

            WeightedReward(new ZeroSumReward(new HighPassReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new ForwardPassReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 40.0f),
            WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 40.0f),
            WeightedReward(new ZeroSumReward(new PressureEscapeBackPassReward(), 0.5f), 8.0f),
            WeightedReward(new PassToOpponentPenalty(), 25.0f),

            // Spatial lane control (team modes only — returns 0 in 1v1)
            WeightedReward(new PassingLaneNetworkReward(PassingLaneNetworkReward::Config::Full()), 7.0f),
            WeightedReward(new SpatialFakeReward(), 5.0f),

            WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 5.0f),
            WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 20.0f),
            WeightedReward(new BoostAwareRotationReward(RotationPhase::CHAMPION_PLUS), 5.0f),

            WeightedReward(new ZeroSumReward(new NumericalAdvantageAttackingReward(), 1.0f), 30.0f),
            WeightedReward(new GoalsideReward(), 2.0f),  // Goalside fundamental: +1 between ball and own net, -0.4 offside (smooth 800uu ramp)
            WeightedReward(new ZeroSumReward(new CounterAttackReward(), 1.0f), 5.0f),  // Fast-break bonus: capitalize on turnovers when opponents caught upfield
            WeightedReward(new ZeroSumReward(new DribbleBumpReward(), 0.5f), 60.0f),

            WeightedReward(new DemoAwarenessReward(), 3.0f),
            WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 40.0f),
            WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 60.0f),
            WeightedReward(new ZeroSumReward(new ContextualDemoReward(), 0.5f), 150.0f),
            WeightedReward(new ZeroSumReward(new SoloDefenderBumpReward(), 0.5f), 60.0f),
            WeightedReward(new ZeroSumReward(new PossessionSideDemoReward(), 1.0f), 15.0f),
            WeightedReward(new ZeroSumReward(new DemoOpportunityReward(), 1.0f), 15.0f),
            WeightedReward(new ThirdManDemoReward(), 150.0f),

            WeightedReward(
                new ZeroSumReward(
                    new MathRewardWrapper(
                        {
                            new SpeedScaledShotReward(),
                            new ShotAccuracyInBoundsReward()
                        },
                        MathOp::MULTIPLY
                    ),
                    0.3f
                ),
                40.0f
            ),
            WeightedReward(
                new ZeroSumReward(
                    new MathRewardWrapper(
                        {
                            new CompositeFlickReward(),
                            new ShotAccuracyInBoundsReward()
                        },
                        MathOp::MULTIPLY
                    ),
                    0.3f
                ),
                30.0f
            ),
            WeightedReward(
                new ZeroSumReward(
                    new MathRewardWrapper(
                        {
                            new BallRedirectReward(),
                            new ShotAccuracyInBoundsReward()
                        },
                        MathOp::MULTIPLY
                    ),
                    0.3f
                ),
                40.0f
            ),
            WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 120.0f),
            WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 80.0f)
        };

        return baseRewards;
    }

    default:
        // Fallback to Bronze
        return std::vector<WeightedReward>{
            WeightedReward(new VelocityReward(), 0.4f),
            WeightedReward(new FaceBallReward(), 0.3f),
            WeightedReward(new VelocityPlayerToBallReward(), 3.0f),
            WeightedReward(new ZeroSumReward(new TouchBallReward(), 1.0f), 20.0f),
            WeightedReward(new PickupBoostReward(), 8.0f),
            WeightedReward(new GoalReward(-1.0f), 150.0f)
        };
    }
}

/// <summary>
/// Generate rewards from JSON configuration file
/// This is the new data-driven approach that avoids hardcoding 1000+ lines
/// </summary>
std::vector<WeightedReward> GeneratePhasedRewardsFromJSON() {
    // Note: This is the data-driven approach for loading rewards from JSON.
    // The infrastructure (RewardFactory, ConfigLoader) is in place and ready.
    // However, due to complex constructor parameters in many reward classes,
    // we temporarily fall back to the hardcoded approach while the factory
    // is being enhanced to handle parameterized constructors.

    // TODO: Enhance RewardFactory to support rewards with parameters
    // TODO: Create wrapper rewards for parameterized rewards in the JSON config

    return GetPhasedRewards();  // Fallback to hardcoded configuration

    /*
    // Original JSON-based code (currently disabled):
    // Initialize factory if not already done
    static bool initialized = false;
    if (!initialized) {
        RewardFactory::Initialize();
        ConfigLoader::LoadDefaultConfig();
        ConfigLoader::Validate();
        initialized = true;
    }

    // Convert current phase enum to string
    std::string phaseName = [&]() {
        switch (CURRENT_PHASE) {
            case TrainingPhase::PHASE_1_BRONZE: return "PHASE_1_BRONZE";
            case TrainingPhase::PHASE_2_SILVER: return "PHASE_2_SILVER";
            case TrainingPhase::PHASE_3_GOLD: return "PHASE_3_GOLD";
            case TrainingPhase::PHASE_4_PLATINUM: return "PHASE_4_PLATINUM";
            case TrainingPhase::PHASE_5_DIAMOND: return "PHASE_5_DIAMOND";
            case TrainingPhase::PHASE_6_CHAMPION: return "PHASE_6_CHAMPION";
            case TrainingPhase::PHASE_7_GC: return "PHASE_7_GC";
            case TrainingPhase::PHASE_8_SSL: return "PHASE_8_SSL";
            case TrainingPhase::PHASE_9_PRO: return "PHASE_9_PRO";
            default: return "PHASE_1_BRONZE";  // Fallback
        }
    }();

    try {
        // Get rewards for current phase from JSON configuration
        const auto& phaseRewards = ConfigLoader::GetPhaseRewards(phaseName);

        std::vector<WeightedReward> result;
        result.reserve(phaseRewards.size());

        // Create reward instances from the configuration
        for (const auto& [rewardName, weight] : phaseRewards) {
            try {
                Reward* reward = RewardFactory::Create(rewardName);
                if (reward) {
                    result.push_back(WeightedReward(reward, weight));
                }
            } catch (const std::exception& e) {
                std::cerr << "[RewardConfig] Error creating reward '" << rewardName << "': " << e.what() << std::endl;
                // Continue with next reward instead of failing completely
            }
        }

        if (result.empty()) {
            throw std::runtime_error("No valid rewards loaded for phase " + phaseName);
        }

        return result;
    } catch (const std::exception& e) {
        std::cerr << "[RewardConfig] Error loading from JSON for phase " << phaseName << ": " << e.what() << std::endl;
        std::cerr << "[RewardConfig] Falling back to hardcoded configuration" << std::endl;
        // Fall back to the hardcoded GetPhasedRewards
        return GetPhasedRewards();
    }
    */
}