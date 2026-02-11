#pragma once
#include "RewardConfig.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/PassingRewards.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/ShootingRewards.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/OptionControlReward.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/OptionGatingWrappers.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/SimpleOvercommitmentPenalty.h"

std::vector<WeightedReward> GetPhasedRewards() {
    switch (CURRENT_PHASE) {

        // ========================================================================
        // PHASE 1: BRONZE (0-1B steps)
        // Focus: Ball chasing, boost collection, basic touches
        // Key: Zero-sum on ALL competitive rewards
        // ========================================================================
    case TrainingPhase::PHASE_1_BRONZE:
        return std::vector<WeightedReward>{
                WeightedReward(new VelocityReward(), 0.4f),
                WeightedReward(new FaceBallReward(), 0.3f),
                WeightedReward(new AirReward(), 0.25f),
                // Keep from Stage 1
                WeightedReward(new VelocityPlayerToBallReward(), 2.0f),  // REDUCED
                WeightedReward(new ZeroSumReward(new TouchBallReward(), 0.3f), 5.0f),  // REDUCED
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 15.0f),
                WeightedReward(new PickupBoostReward(), 5.0f),
                WeightedReward(new GoalReward(-1.0f), 250.0f),

                // NEW: Ground control focus
                WeightedReward(new ZeroSumReward(new StrongTouchReward(20, 100), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new JumpTouchReward(), 0.3f), 10.0f),  // Learn to jump

                // NEW: Spacing (3v3 only)
                //WeightedReward(new InstantSpacingReward(RotationPhase::BRONZE_SILVER), 5.0f),

                // NEW: Basic movement
                WeightedReward(new PowerSlideControlReward(), 0.5f)
        };

        // ========================================================================
        // PHASE 2: SILVER (1-3B steps)
        // Focus: Ground control, simple aerials, basic recoveries
        // ========================================================================
    case TrainingPhase::PHASE_2_SILVER:
        return std::vector<WeightedReward>{
                // Keep important ones from Stage 2
                WeightedReward(new VelocityPlayerToBallReward(), 1.0f),  // VERY REDUCED
                WeightedReward(new ZeroSumReward(new StrongTouchReward(25, 110), 0.3f), 30.0f),  // Stricter
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 20.0f),
                WeightedReward(new InstantSpacingReward(RotationPhase::GOLD), 1.0f),  // UPGRADED
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
                WeightedReward(new WavedashBasicReward(), 10.0f),
                WeightedReward(new AirReward(), 0.3f),
                WeightedReward(new ProperLandingReward(), 0.2f),
                WeightedReward(new AdvancedRecoveryReward(), 0.2f),

                // NEW: Boost management (critical for aerials)
                WeightedReward(new PickupBoostReward(), 20.0f),  // INCREASED
                WeightedReward(new SaveBoostReward(), 0.6f),
                WeightedReward(new SmallPadCollectionReward(), 2.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 4.0f),
                WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
                WeightedReward(new BigBoostPadCenterReward(), 10.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

                // NEW: Basic Shots
                WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f), // updated reward on 01/30/26

                // NEW: Minor possession shaping
                WeightedReward(new DefensivePositionReward(), 2.5f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.0f),
                // NEW: Retreat after touch (prepare for rotation)
                WeightedReward(new RetreatAfterTouchReward(RotationPhase::GOLD), 3.0f),

                WeightedReward(new DemoAwarenessReward(), 1.5f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 60.0f),

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
                WeightedReward(new VelocityPlayerToBallReward(), 0.5f),  // VERY REDUCED
                WeightedReward(new ZeroSumReward(new StrongTouchReward(25, 110), 0.3f), 25.0f),  // Stricter
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 20.0f),
                WeightedReward(new InstantSpacingReward(RotationPhase::PLATINUM), 1.0f),  // UPGRADED
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 15.0f),
                WeightedReward(new PowerSlideControlReward(), 0.55f),
                WeightedReward(new GoalReward(-1.0f), 450.0f),

                // NEW: Aerial focus
                WeightedReward(new ZeroSumReward(new AirTouchReward(), 0.3f), 15.0f),  // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 0.3f), 28.0f),
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 7.5f), // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 7.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new JumpTouchReward(), 0.3f), 10.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 12.0f),

                // NEW: Recovery (needed for aerials)
                WeightedReward(new HalfFlipReward(), 2.75f),
                WeightedReward(new WavedashBasicReward(), 10.0f),
                WeightedReward(new EnhancedSpeedFlipReward(), 3.0f),
                WeightedReward(new AirReward(), 0.3f),
                WeightedReward(new ProperLandingReward(), 0.2f),
                WeightedReward(new AdvancedRecoveryReward(), 0.2f),

                // NEW: Boost management (critical for aerials)
                WeightedReward(new PickupBoostReward(), 24.0f),  // INCREASED
                WeightedReward(new SaveBoostReward(), 0.6f),
                WeightedReward(new SmallPadCollectionReward(), 2.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 45.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 4.0f),
                WeightedReward(new BoostWastePenaltyReward(), 3.0f),
                WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
                WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 2.0f),
                WeightedReward(new BigBoostPadCenterReward(), 10.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

                // NEW: Basic Shots
                WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f), // updated reward on 01/30/26

                // NEW: Minor possession shaping and defense
                WeightedReward(new DefensivePositionReward(), 2.5f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.0f),
                WeightedReward(new BackboardDefenseReward(), 1.0f),
                WeightedReward(new EarlyDangerDetectionReward(), 0.25f),
                WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 5.0f),

                // NEW: Passing rewards
                WeightedReward(new ZeroSumReward(new BackPassReward(), 0.5f), 2.0f),

                // NEW: Retreat after touch (prepare for rotation), rotation rewards
                WeightedReward(new RetreatAfterTouchReward(RotationPhase::PLATINUM), 4.0f),
                
                WeightedReward(new ProgressiveRotationReward(RotationPhase::PLATINUM), 5.0f),

                WeightedReward(new DemoAwarenessReward(), 2.0f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 60.0f),

                // NEW: Basic shots
                WeightedReward(new ZeroSumReward(new ShotReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 30.0f)
        };

        // ========================================================================
        // PHASE 4: PLATINUM (5-8B steps)
        // Focus: Air control, 50/50s, boost management, dribbling intro
        // ========================================================================
    case TrainingPhase::PHASE_4_PLATINUM:
        {
            // Create shared option control instance for gating mechanism
            OptionControlReward* optionControl = new OptionControlReward();

            return std::vector<WeightedReward>{
                // ====================================================================
                // OPTION CONTROL REWARD (Strategic Optionality)
                // ====================================================================
                // Encourages bot to move into positions with many viable plays
                // Very low weight as standalone; mainly used for gating other rewards
                //WeightedReward(optionControl, 0.5f),  // Low weight - primarily for gating

                // ====================================================================
                // EXAMPLE WRAPPER USAGE: Gating scoring rewards with option control
                // ====================================================================
                // This demonstrates how to wrap any scoring reward with option-based gating
                /*WeightedReward(
                    new ZeroSumReward(
                        new ScoringRewardWrapper(
                            new AdvancedFlickReward(),
                            optionControl
                        ),
                        0.3f
                    ),
                    6.0f
                ),
                WeightedReward(
                    new ZeroSumReward(
                        new ScoringRewardWrapper(
                            new DribbleMasteryReward(),
                            optionControl
                        ),
                        0.3f
                    ),
                    1.5f
                ),
                WeightedReward(
                    new ZeroSumReward(
                        new ScoringRewardWrapper(
                            new CutOutplayReward(),
                            optionControl
                        ),
                        0.3f
                    ),
                    30.0f
                ),
                WeightedReward(
                    new ZeroSumReward(
                        new ScoringRewardWrapper(
                            new FakeChallengeOffenseReward(),
                            optionControl
                        ),
                        0.3f
                    ),
                    1.0f
                ),*/

                // ====================================================================
                // EXAMPLE WRAPPER USAGE: Gating passing rewards with option control
                // ====================================================================
                // This demonstrates how to wrap any passing reward with option-based gating
                /*WeightedReward(
                    new ZeroSumReward(
                        new PassingRewardWrapper(
                            new PassingAccuracyReward(),
                            optionControl
                        ),
                        1.0f
                    ),
                    20.0f
                ),*/

                // ====================================================================
                // REMAINING PLATINUM PHASE REWARDS
                // ====================================================================
			    //WeightedReward(new VelocityPlayerToBallReward(), 0.25f),  // VERY REDUCED
                WeightedReward(new VelocityReward(), 0.1f),
                WeightedReward(new ZeroSumReward(new StrongTouchReward(70, 140), 0.3f), 25.0f),  // Stricter
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 35.0f),
                WeightedReward(new InstantSpacingReward(RotationPhase::CHAMPION_PLUS), 1.25f),  // UPGRADED
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 15.0f),
                WeightedReward(new PowerSlideControlReward(), 0.55f),
                WeightedReward(new GoalReward(-1.0f), 600.0f),

                // Aerial focus
                WeightedReward(new ZeroSumReward(new FirstToBallReward(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new EarlyAerialReward(), 0.5f), 5.0f),
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 35.0f), 
                WeightedReward(new ZeroSumReward(new MultiAirTouchReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 7.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 20.0f),
                //WeightedReward(new ZeroSumReward(new WallToAerialReward(), 0.3f), 2.0f),
                WeightedReward(new ZeroSumReward(new JumpTouchReward(), 0.3f), 10.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 20.0f),

                // NEW: Dribbling
                WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 1.0f),
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.3f), 4.0f),
                // NOTE: DribbleMasteryReward is now wrapped with option control above
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.4f), 1.5f),
                WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 1.5f),
                WeightedReward(new ZeroSumReward(new AdvancedFlickReward(), 0.3f), 9.0f),
                WeightedReward(new ZeroSumReward(new FlickExplorationReward(), 0.3f), 3.0f),
                // NOTE: AdvancedFlickReward is now wrapped with option control above

                // Recovery
                WeightedReward(new HalfFlipReward(), 1.5f),
                WeightedReward(new WavedashBasicReward(), 25.0f), 
                //WeightedReward(new WavedashReward(), 12.0f),
                WeightedReward(new EnhancedSpeedFlipReward(), 8.0f),
                //WeightedReward(new AirReward(), 0.1f),
                WeightedReward(new ProperLandingReward(), 0.3f),
                WeightedReward(new AdvancedRecoveryReward(), 0.3f),
                WeightedReward(new BumpedFlipRecoveryReward(), 0.4f),
                WeightedReward(new WallDashReward(), 35.0f),  // Increased weight for better sequence detection

                // Boost management
                WeightedReward(new PickupBoostReward(), 32.0f),  // INCREASED
                WeightedReward(new SaveBoostReward(), 0.6f),
                WeightedReward(new SmallPadCollectionReward(), 2.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 45.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 4.0f),
                WeightedReward(new BoostWastePenaltyReward(), 5.0f),
                WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
                WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 4.0f),
                WeightedReward(new LowBoostOffenseReward(), 5.0f),
                WeightedReward(new BigBoostPadCenterReward(), 15.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

                // Basic Shots and Outplays
                WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f), // updated reward on 01/30/26. Added to Option Control above
                WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 1.4f), // Added to Option Control above
                WeightedReward(new ZeroSumReward(new AirRollShotReward(), 0.3f), 8.0f),

                // NEW: Minor possession shaping and defense
                WeightedReward(new DefensivePositionReward(), 2.5f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f),
                WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 0.3f), 1.5f),
                WeightedReward(new DefensivePositioningChallengeReward(), 2.0f),
                WeightedReward(new BackboardDefenseReward(), 5.0f),
                WeightedReward(new EarlyDangerDetectionReward(), 1.0f),
                WeightedReward(new DoubleCommitPenalty(), 15.0f),
                WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new OpponentPassInterceptionReward(), 1.0f), 4.0f),
                WeightedReward(new GoalpostPositioningReward(), 6.0f),
                WeightedReward(new ActiveSupportingPositionReward(), 4.5f),

                // NEW: Passing rewards
                // NOTE: PassingAccuracyReward is now wrapped with option control above
                WeightedReward(new ZeroSumReward(new BackPassReward(), 0.5f), 3.5f),
                WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 14.0f),
                WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 14.0f),

                // NEW: Retreat after touch (prepare for rotation), rotation rewards
                WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 1.5f),

                WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 20.0f),

                WeightedReward(new DemoAwarenessReward(), 2.5f),
                /*WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 50.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 120.0f),
                WeightedReward(new ZeroSumReward(new ContextualDemoReward(), 0.5f), 150.0f),
                WeightedReward(new ZeroSumReward(new SoloDefenderBumpReward(), 0.5f), 50.0f),*/
                WeightedReward(new ZeroSumReward(new PlayerVelocityToOpponentReward(), 0.4f), 1.0f),
                WeightedReward(new BumpReward(), 20.0f),
                //WeightedReward(new BumpedPenalty(), 15.0f),
                WeightedReward(new DemoReward(), 60.0f),
                //WeightedReward(new DemoedPenalty(), 40.0f),
                WeightedReward(new ContextualDemoReward(), 60.0f),
                WeightedReward(new SoloDefenderBumpReward(), 20.0f),

                // NEW: Basic shots
                //WeightedReward(new ZeroSumReward(new ShotReward(), 0.3f), 15.0f),
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
                    30.0f
                ),
                WeightedReward(
                    new ZeroSumReward(
                        new MathRewardWrapper(
                            {
                                new FlickOnGoalReward(),
                                new ShotAccuracyInBoundsReward()
                            },
                            MathOp::MULTIPLY
                        ),
                        0.3f
                    ),
                    20.0f
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
                    20.0f
                ),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 30.0f)
            };
        }
    case TrainingPhase::PHASE_5_DIAMOND:
    {
        return std::vector<WeightedReward>{
            // ====================================================================
            // REMAINING PLATINUM PHASE REWARDS
            // ====================================================================
                //WeightedReward(new VelocityPlayerToBallReward(), 0.25f),  // VERY REDUCED
                //WeightedReward(new VelocityReward(), 0.1f),
                WeightedReward(new ZeroSumReward(new StrongTouchReward(90, 140), 0.3f), 30.0f),  // Stricter
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 30.0f),
                //WeightedReward(new InstantSpacingReward(RotationPhase::DIAMOND), 1.0f),  // UPGRADED
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 20.0f),
                WeightedReward(new PowerSlideControlReward(), 0.6f),
                WeightedReward(new GoalReward(-0.9f), 1000.0f),
                //WeightedReward(new SimpleOvercommitmentPenalty(), 10.0f),

                // Aerial focus
                //WeightedReward(new DoubleJumpAerialReward(), 5.0f),
                WeightedReward(new PitchControlAerialReward(), 12.0f),
                WeightedReward(new ZeroSumReward(new FirstToBallReward(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 0.3f), 20.0f),
                WeightedReward(new ZeroSumReward(new EarlyAerialReward(), 0.5f), 8.0f),
                WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new AirDribbleReward(), 0.3f), 35.0f),
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 70.0f),
                WeightedReward(new ZeroSumReward(new MultiAirTouchReward(), 0.3f), 80.0f),
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 25.0f),
                //WeightedReward(new ZeroSumReward(new WallToAerialReward(), 0.3f), 2.0f),
                //WeightedReward(new ZeroSumReward(new JumpTouchReward(), 0.3f), 10.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 25.0f),

                // NEW: Dribbling
                WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 1.0f),
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.3f), 4.0f),
                // NOTE: DribbleMasteryReward is now wrapped with option control above
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.4f), 1.5f),
                WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 1.5f),
                WeightedReward(new ZeroSumReward(new AdvancedFlickReward(), 0.3f), 10.0f),
                WeightedReward(new ZeroSumReward(new BackFlipFlickReward(), 0.3f), 1.2f),
                WeightedReward(new ZeroSumReward(new MustyFlickReward(), 0.3f), 2.4f),
                //WeightedReward(new ZeroSumReward(new AngleFlickRewardBase(), 0.3f), 12.0f),
                //WeightedReward(new ZeroSumReward(new FlickExplorationReward(), 0.3f), 3.0f),
                // NOTE: AdvancedFlickReward is now wrapped with option control above

                // Recovery
                WeightedReward(new HalfFlipReward(), 1.5f),
                //WeightedReward(new WavedashBasicReward(), -2.0f),
                //WeightedReward(new IntelligentWavedashReward(), 5.0f),
                //WeightedReward(new WavedashReward(), 12.0f),
                WeightedReward(new EnhancedSpeedFlipReward(), 10.0f),
                WeightedReward(new AirReward(), 0.1f),
                WeightedReward(new ProperLandingReward(), 0.3f),
                WeightedReward(new AdvancedRecoveryReward(), 0.3f),
                WeightedReward(new BumpedFlipRecoveryReward(), 0.4f),
                //WeightedReward(new WallDashReward(), 35.0f),  // Increased weight for better sequence detection
                WeightedReward(new SupersonicReward(), 4.0f),

                // Kickoff
                //WeightedReward(new KickoffSpeedReward(), 20.0f),
                //WeightedReward(new KickoffFlipReward(), 13.0f),
                //WeightedReward(new KickoffStrategyReward(), 25.0f),  // Reward diverse kickoff strategies
                //WeightedReward(new KickoffRandomizationReward(), 15.0f),  // Prevent repetitive patterns

                // Boost management
                WeightedReward(new PickupBoostReward(), 34.0f),  // INCREASED
                WeightedReward(new SaveBoostReward(), 0.57f),
                WeightedReward(new SmallPadCollectionReward(), 2.5f),
                WeightedReward(new BoostPathReward(), 3.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 50.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 4.0f),
                WeightedReward(new BoostWastePenaltyReward(), 6.0f),
                WeightedReward(new SupersonicBoostPenalty(), 8.0f),
                WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
                WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 6.0f),
                WeightedReward(new LowBoostOffenseReward(), 10.0f),
                WeightedReward(new BigBoostPadCenterReward(), 15.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

                // Basic Shots and Outplays
                WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 5.0f),
                WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 5.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 25.0f), // updated reward on 01/30/26. Added to Option Control above
                WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 1.5f), // Added to Option Control above
                //WeightedReward(new ZeroSumReward(new AirRollShotReward(), 0.3f), 6.0f),
                WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.3f), 35.0f),
                WeightedReward(new ZeroSumReward(new DoomseeDisshShotReward(), 0.3f), 30.0f),
                WeightedReward(new ZeroSumReward(new ChallengeCommitmentReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new AerialCommitmentReward(), 0.3f), 15.0f),

                // NEW: Minor possession shaping and defense
                WeightedReward(new DefensivePositionReward(), 2.5f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.0f),
                WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 0.3f), 2.15f),
                WeightedReward(new ZeroSumReward(new BigClearReward(), 0.5f), 12.0f),
                WeightedReward(new ZeroSumReward(new CornerClearReward(), 0.5f), 2.0f),
                WeightedReward(new DefensivePositioningChallengeReward(), 2.0f),
                WeightedReward(new BackboardDefenseReward(), 5.0f),
                WeightedReward(new EarlyDangerDetectionReward(), 1.0f),
                WeightedReward(new DoubleCommitPenalty(), 10.0f),
                WeightedReward(new SimpleOvercommitmentPenalty(), 20.0f),  // Phase 1: Resource management & positioning discipline
                WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 15.0f),
                WeightedReward(new ZeroSumReward(new OpponentPassInterceptionReward(), 1.0f), 6.0f),
                WeightedReward(new GoalpostPositioningReward(), 8.0f),
                WeightedReward(new ActiveSupportingPositionReward(), 6.5f),
                WeightedReward(new OwnNetCenteringPenalty(), 15.0f),

                // NEW: Passing rewards
                // NOTE: PassingAccuracyReward is now wrapped with option control 
                //WeightedReward(new ZeroSumReward(new BackPassReward(), 1.0f), 3.0f),
                WeightedReward(new ZeroSumReward(new HighPassReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new ForwardPassReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 25.0f),
                WeightedReward(new PassToOpponentPenalty(), 12.0f),

                // NEW: Retreat after touch (prepare for rotation), rotation rewards
                WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 3.0f),

                WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 15.0f),

                WeightedReward(new DemoAwarenessReward(), 2.5f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 30.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 60.0f),
                WeightedReward(new ZeroSumReward(new ContextualDemoReward(), 0.5f), 130.0f),
                WeightedReward(new ZeroSumReward(new SoloDefenderBumpReward(), 0.5f), 40.0f),
                //WeightedReward(new ZeroSumReward(new PlayerVelocityToOpponentReward(), 0.4f), 2.0f),
                //WeightedReward(new BumpReward(), 30.0f),
                //WeightedReward(new BumpedPenalty(), 15.0f),
                //WeightedReward(new DemoReward(), 80.0f),
                //WeightedReward(new DemoedPenalty(), 40.0f),
                //WeightedReward(new ContextualDemoReward(), 80.0f),
                //WeightedReward(new SoloDefenderBumpReward(), 30.0f),

                // NEW: Basic shots
                //WeightedReward(new ZeroSumReward(new ShotReward(), 0.3f), 15.0f),
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
                                new FlickOnGoalReward(),
                                new ShotAccuracyInBoundsReward()
                            },
                            MathOp::MULTIPLY
                        ),
                        0.3f
                    ),
                    36.0f
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
                    30.0f
                ),
                WeightedReward(new ZeroSumReward(new AssistReward(), 0.5f), 60.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 60.0f)
        };
    }
    case TrainingPhase::PHASE_6_CHAMPION:
    case TrainingPhase::PHASE_7_GC:
    case TrainingPhase::PHASE_8_SSL:
    case TrainingPhase::PHASE_9_PRO:
        // For now, default to PHASE_4_PLATINUM rewards
        // TODO: Implement phase-specific rewards for phases 5-9
        return std::vector<WeightedReward>{
            WeightedReward(new VelocityReward(), 0.4f),
            WeightedReward(new FaceBallReward(), 0.3f),
            WeightedReward(new VelocityPlayerToBallReward(), 3.0f),
            WeightedReward(new ZeroSumReward(new TouchBallReward(), 1.0f), 20.0f),
            WeightedReward(new PickupBoostReward(), 8.0f),
            WeightedReward(new GoalReward(-1.0f), 150.0f)
        };

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
