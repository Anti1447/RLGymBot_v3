#pragma once
#include "RewardConfig.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/PassingRewards.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/ShootingRewards.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/OptionControlReward.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/OptionGatingWrappers.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/SimpleOvercommitmentPenalty.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/AdvancedOvercommitmentPenalty.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/AdvancedOpponentOvercommitmentReward.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/CompositeFlickReward.h"

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

                // NEW: Numerical advantage attacking (2v1+ situations)
                WeightedReward(new NumericalAdvantageAttackingReward(), 4.0f),

                // NEW: Dribble bump reward (aggressive ground dribbles with bumps)
                WeightedReward(new DribbleBumpReward(), 2.0f),

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
                WeightedReward(new ZeroSumReward(new CompositeFlickReward(), 0.3f), 12.0f),
                // NOTE: CompositeFlickReward combines AdvancedFlickReward, FlickExplorationReward,
                //       BackFlipFlickReward, MustyFlickReward, and FlickOnGoalReward

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
                WeightedReward(new PickupBoostReward(), 35.0f),  // INCREASED
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
                WeightedReward(new BoostAwareRotationReward(RotationPhase::CHAMPION_PLUS), 3.5f),

                // NEW: Numerical advantage attacking (2v1+ situations)
                WeightedReward(new NumericalAdvantageAttackingReward(), 5.0f),

                // NEW: Dribble bump reward (aggressive ground dribbles with bumps)
                WeightedReward(new DribbleBumpReward(), 3.0f),

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
                                new CompositeFlickReward(),
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
                WeightedReward(new ZeroSumReward(new StrongTouchReward(110, 160), 0.3f), 20.0f),  // Stricter
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 20.0f),
                WeightedReward(new InstantSpacingReward(RotationPhase::CHAMPION_PLUS), 5.0f),  // UPGRADED
                WeightedReward(new PowerSlideControlReward(), 0.55f),
                WeightedReward(new GoalReward(-1.0f), 900.0f),
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 35.0f),
                // GoalMultiplierReward is intentionally separate from GoalReward rather than
                // wrapped in MathOp::MULTIPLY. Using MULTIPLY would zero-out the concede penalty
                // (GoalReward returns -0.75 for conceder, GoalMultiplierReward returns 0 for
                // non-scorers, so -0.75 * 0 = 0). Keeping them separate preserves the full
                // concede penalty while still paying the scorer a 1.0-3.0x style bonus.
                // At weight 650 (half of 1300), a plain goal adds +650, a max-style goal adds +1950.
                WeightedReward(new ZeroSumReward(new GoalMultiplierReward(), 1.0f), 350.0f),
                // OwnGoalPenalty: fires only when the conceding team last touched the ball.
                // No ZeroSum needed — it already returns 0 to the opposing team.
                // Weight matches GoalReward so an own goal stings as much as conceding a regular goal.
                WeightedReward(new OwnGoalPenalty(-1.0f), 900.0f),

                // Aerial focus
                WeightedReward(new ZeroSumReward(new PitchControlAerialReward(), 0.4f), 10.0f),
                WeightedReward(new ZeroSumReward(new FirstToBallReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 1.0f), 40.0f),
                WeightedReward(new ZeroSumReward(new EarlyAerialReward(), 0.5f), 5.0f),
                WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.5f), 12.0f),
                WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.5f), 50.0f),
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.5f), 15.0f),
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 0.3f), 9.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 20.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 20.0f),

                // NEW: Dribbling
                WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 0.5f),
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.3f), 1.5f),
                // NOTE: DribbleMasteryReward is now wrapped with option control above
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.4f), 0.7f),
                WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 0.8f),
                WeightedReward(new ZeroSumReward(new CompositeFlickReward(), 0.5f), 20.0f),
                // FlickOutplayReward: dribble flick executed while opponent is actively
                // challenging (within 1200 UU, moving toward ball > 500 UU/s or already jumped).
                // Quality: on-goal=0.9, pop-up=0.35, toward-goal+away=0.35, behind=up-to+0.15.
                // Max payout = 10 + 10*quality (internal). ZeroSum: opponent loses what scorer gains.
                WeightedReward(new ZeroSumReward(new FlickOutplayReward(), 1.0f), 25.0f),

                // NEW: Ceiling Shots (advanced wall-to-air-dribble variation)
                WeightedReward(new ZeroSumReward(new CeilingShotReward(), 0.5f), 12.0f),

                // Recovery
                WeightedReward(new HalfFlipReward(), 2.0f),
                WeightedReward(new WavedashBasicReward(), 0.5f),
                WeightedReward(new EnhancedSpeedFlipReward(), 6.0f),
                WeightedReward(new ProperLandingReward(), 0.67f),
                WeightedReward(new AdvancedRecoveryReward(), 0.3f),
                WeightedReward(new BumpedFlipRecoveryReward(), 0.2f),
                WeightedReward(new SupersonicReward(), 3.0f),

                // Kickoff
                //WeightedReward(new KickoffSpeedReward(), 20.0f),
                //WeightedReward(new KickoffFlipReward(), 13.0f),
                //WeightedReward(new KickoffStrategyReward(), 25.0f),  // Reward diverse kickoff strategies
                //WeightedReward(new KickoffRandomizationReward(), 15.0f),  // Prevent repetitive patterns
                // Kickoff - Encourage closest player to rush ball
                WeightedReward(new ClosestPlayerSpeedReward(), 12.0f),

                // Boost management
                WeightedReward(new PickupBoostReward(), 43.0f),  // INCREASED
                WeightedReward(new SaveBoostReward(), 0.60f),
                WeightedReward(new SmallPadCollectionReward(), 4.0f),
                WeightedReward(new BoostPathReward(), 12.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 80.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 2.5f),
                WeightedReward(new BoostWastePenaltyReward(), 12.0f),
                WeightedReward(new SupersonicBoostPenalty(), 15.0f),
                //WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
                WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 4.5f),
                WeightedReward(new BigBoostPadCenterReward(), 20.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

                // Basic Shots and Outplays
                //WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 5.0f),
                WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 15.0f), // updated reward on 01/30/26. Added to Option Control above
                WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 1.5f), // Added to Option Control above
                //WeightedReward(new ZeroSumReward(new AirRollShotReward(), 0.3f), 6.0f),
                WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.3f), 40.0f),
                WeightedReward(new ZeroSumReward(new DoomseeDisshShotReward(), 0.3f), 40.0f),
                WeightedReward(new ZeroSumReward(new ChallengeCommitmentReward(), 0.5f), 15.0f),
                WeightedReward(new ZeroSumReward(new AerialCommitmentReward(), 0.5f), 15.0f),
                WeightedReward(new ZeroSumReward(new WallPinchReward(), 0.2f), 10.0f),

                // NEW: Minor possession shaping and defense
                WeightedReward(new DefensivePositionReward(), 2.25f),
                WeightedReward(new DefensiveReadinessReward(), 0.6f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 3.0f),
                WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 9.0f),
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 1.0f), 3.2f),
                WeightedReward(new ZeroSumReward(new BigClearReward(), 0.5f), 6.0f),
                WeightedReward(new ZeroSumReward(new CornerClearReward(), 0.5f), 3.0f),
                WeightedReward(new DefensivePositioningChallengeReward(), 2.0f),
                WeightedReward(new BackboardDefenseReward(), 6.0f),
                WeightedReward(new EarlyDangerDetectionReward(), 1.0f),
                WeightedReward(new DoubleCommitPenalty(), 25.0f),
                WeightedReward(new SimpleOvercommitmentPenalty(), 9.0f),  // Phase 1: Resource management & positioning discipline
                WeightedReward(new AdvancedOvercommitmentPenalty(), 8.0f),  // Some Phase 2 emphasis
                WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new OpponentPassInterceptionReward(), 1.0f), 10.0f),
                WeightedReward(new GoalpostPositioningReward(), 7.0f),
                WeightedReward(new ActiveSupportingPositionReward(), 5.6f),
                WeightedReward(new OwnNetCenteringPenalty(), 10.0f),
                WeightedReward(new EmergencySaveCommitReward(), 5.0f),

                // NEW: Passing rewards
                // NOTE: PassingAccuracyReward is now wrapped with option control 
                //WeightedReward(new ZeroSumReward(new BackPassReward(), 1.0f), 3.0f),
                //WeightedReward(new ZeroSumReward(new HighPassReward(), 1.0f), 10.0f),
                //WeightedReward(new ZeroSumReward(new ForwardPassReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 15.0f),
                WeightedReward(new PassToOpponentPenalty(), 30.0f),

                // NEW: Retreat after touch (prepare for rotation), rotation rewards
                WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 1.0f),

                WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 5.0f),
                WeightedReward(new BoostAwareRotationReward(RotationPhase::CHAMPION_PLUS), 2.0f),

                // NEW: Numerical advantage attacking (2v1+ situations)
                // Weight reduced 70 -> 35: was driving excessive ball-chasing via per-frame
                // aggression bonus for any player in the attacking zone.
                WeightedReward(new ZeroSumReward(new NumericalAdvantageAttackingReward(), 1.0f), 35.0f),
                // 2nd-man upfield holding: fires only for MIDDLE-role player when teammate has
                // possession. Rewards staying 1000-5000 UU upfield (closer to opp goal than ball).
                // Counteracts redundant ball-chasing when a teammate is already the primary.
                WeightedReward(new ActiveSupportingPositionReward(), 8.0f),

                // NEW: Dribble bump reward (aggressive ground dribbles with bumps)
                //WeightedReward(new ZeroSumReward(new DribbleBumpReward(), 0.5f), 40.0f),

                //WeightedReward(new DemoAwarenessReward(), 3.0f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 30.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new ContextualDemoReward(), 0.5f), 80.0f),
                //WeightedReward(new ZeroSumReward(new SoloDefenderBumpReward(), 0.5f), 60.0f),
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
                        0.5f
                    ),
                    50.0f
                ),
                WeightedReward(
                    new ZeroSumReward(
                        new MathRewardWrapper(
                            {
                                new AirTouchReward(),
                                new ShotAccuracyInBoundsReward()
                            },
                            MathOp::MULTIPLY
                        ),
                        0.5f
                    ),
                    30.0f
                ),
                WeightedReward(
                    new ZeroSumReward(
                        new MathRewardWrapper(
                            {
                                new WallPinchReward(),
                                new ShotAccuracyInBoundsReward()
                            },
                            MathOp::MULTIPLY
                        ),
                        0.5f
                    ),
                    10.0f
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
                        0.5f
                    ),
                    60.0f
                ),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 40.0f)
        };
    }
    case TrainingPhase::PHASE_6_CHAMPION:
    {
        // ====================================================================
        // PHASE 6: CHAMPION - Advanced Motion Prediction (Phase 2 Overcommitment)
        // ====================================================================
        // Transition from Phase 1 (reactive) to Phase 2 (predictive) overcommitment
        // penalties. Both run with reduced Phase 1 weight and new Phase 2 emphasis.
        // Opponent exploitation reward for capitalizing on locked-out opponents.
        // ====================================================================

        // Use Phase 5 Diamond as base and add Phase 2 overcommitment rewards
        auto baseRewards = std::vector<WeightedReward>{
            // ====================================================================
            // DIAMOND PHASE CORE REWARDS (reused)
            // ====================================================================
            WeightedReward(new ZeroSumReward(new StrongTouchReward(80, 140), 0.3f), 25.0f),
            WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 30.0f),
            WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 15.0f),
            WeightedReward(new PowerSlideControlReward(), 0.55f),
            WeightedReward(new GoalReward(-1.0f), 1000.0f),

            // Aerial focus
            WeightedReward(new ZeroSumReward(new PitchControlAerialReward(), 0.4f), 10.0f),
            WeightedReward(new ZeroSumReward(new FirstToBallReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new EarlyAerialReward(), 0.5f), 5.0f),
            WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.5f), 20.0f),
            WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.5f), 60.0f),
            WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.5f), 25.0f),
            WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 0.3f), 12.0f),
            WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 20.0f),
            WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 20.0f),

            // Dribbling
            WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 0.25f),
            WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.3f), 3.0f),
            WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.4f), 1.0f),
            WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 1.0f),
            WeightedReward(new ZeroSumReward(new CompositeFlickReward(), 0.3f), 11.0f),
            // NOTE: CompositeFlickReward combines AdvancedFlickReward (6.0), BackFlipFlickReward (1.0),
            //       MustyFlickReward (2.0), and FlickExplorationReward (2.0)

            // Recovery
            WeightedReward(new HalfFlipReward(), 1.5f),
            WeightedReward(new WavedashBasicReward(), 1.0f),
            WeightedReward(new EnhancedSpeedFlipReward(), 10.0f),
            WeightedReward(new ProperLandingReward(), 0.5f),
            WeightedReward(new AdvancedRecoveryReward(), 0.3f),
            WeightedReward(new BumpedFlipRecoveryReward(), 0.1f),
            WeightedReward(new SupersonicReward(), 3.0f),

            // Boost management
            WeightedReward(new PickupBoostReward(), 33.0f),
            WeightedReward(new SaveBoostReward(), 0.55f),
            WeightedReward(new SmallPadCollectionReward(), 2.0f),
            WeightedReward(new BoostPathReward(), 6.0f),
            WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 50.0f),
            WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 4.0f),
            WeightedReward(new BoostWastePenaltyReward(), 8.0f),
            WeightedReward(new SupersonicBoostPenalty(), 9.0f),
            WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
            WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 6.0f),
            WeightedReward(new BigBoostPadCenterReward(), 20.0f),
            WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

            // Kickoff - Encourage closest player to rush ball
            WeightedReward(new ClosestPlayerSpeedReward(), 5.0f),

            // Shots and Outplays
            WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 10.0f),
            WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f),
            WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 1.5f),
            WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.3f), 35.0f),
            WeightedReward(new ZeroSumReward(new DoomseeDisshShotReward(), 0.3f), 35.0f),
            WeightedReward(new ZeroSumReward(new ChallengeCommitmentReward(), 0.5f), 10.0f),
            WeightedReward(new ZeroSumReward(new AerialCommitmentReward(), 0.5f), 15.0f),

            // Defense and possession
            WeightedReward(new DefensivePositionReward(), 2.5f),
            WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f),
            WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 15.0f),
            WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 1.0f), 1.9f),
            WeightedReward(new ZeroSumReward(new BigClearReward(), 0.5f), 10.0f),
            WeightedReward(new ZeroSumReward(new CornerClearReward(), 0.5f), 3.0f),
            WeightedReward(new DefensivePositioningChallengeReward(), 2.5f),
            WeightedReward(new BackboardDefenseReward(), 5.0f),
            WeightedReward(new EarlyDangerDetectionReward(), 1.2f),
            WeightedReward(new DoubleCommitPenalty(), 6.0f),

            // ====================================================================
            // PHASE 2 OVERCOMMITMENT (NEW for Phase 6+)
            // ====================================================================
            // Reduced Phase 1 (reactive) penalties, new Phase 2 (predictive)
            WeightedReward(new SimpleOvercommitmentPenalty(), 7.0f),  // Reduced from 30.0f
            WeightedReward(new AdvancedOvercommitmentPenalty(), 7.0f),  // New: Motion prediction
            WeightedReward(new AdvancedOpponentOvercommitmentReward(), 10.0f),  // New: Exploit lock-outs

            WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 15.0f),
            WeightedReward(new ZeroSumReward(new OpponentPassInterceptionReward(), 1.0f), 15.0f),
            WeightedReward(new GoalpostPositioningReward(), 10.0f),
            WeightedReward(new ActiveSupportingPositionReward(), 4.0f),
            WeightedReward(new OwnNetCenteringPenalty(), 10.0f),
            WeightedReward(new EmergencySaveCommitReward(), 4.0f),

            // Passing rewards
            WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 20.0f),
            WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 10.0f),
            WeightedReward(new PassToOpponentPenalty(), 20.0f),

            // Rotation
            WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 2.0f),
            WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 5.0f),
            WeightedReward(new BoostAwareRotationReward(RotationPhase::CHAMPION_PLUS), 2.0f),

            // Numerical advantage and dribble attacks
            WeightedReward(new ZeroSumReward(new NumericalAdvantageAttackingReward(), 1.0f), 30.0f),

            // Demos and bumps
            //WeightedReward(new DemoAwarenessReward(), 3.0f),
            WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 40.0f),
            WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 60.0f),
            WeightedReward(new ZeroSumReward(new ContextualDemoReward(), 0.5f), 150.0f),
            WeightedReward(new ZeroSumReward(new SoloDefenderBumpReward(), 0.5f), 60.0f),

            // Shots
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
                30.0f
            ),
            WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 30.0f),
            WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 40.0f)
        };

        return baseRewards;
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
            WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 15.0f),
            WeightedReward(new PowerSlideControlReward(), 0.55f),
            WeightedReward(new GoalReward(-1.0f), 1000.0f),

            WeightedReward(new ZeroSumReward(new PitchControlAerialReward(), 0.4f), 10.0f),
            WeightedReward(new ZeroSumReward(new FirstToBallReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new EarlyAerialReward(), 0.5f), 5.0f),
            WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.5f), 20.0f),
            WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.5f), 60.0f),
            WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.5f), 25.0f),
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
            WeightedReward(new SupersonicReward(), 3.0f),

            WeightedReward(new PickupBoostReward(), 33.0f),
            WeightedReward(new SaveBoostReward(), 0.55f),
            WeightedReward(new SmallPadCollectionReward(), 2.0f),
            WeightedReward(new BoostPathReward(), 6.0f),
            WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 50.0f),
            WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 4.0f),
            WeightedReward(new BoostWastePenaltyReward(), 8.0f),
            WeightedReward(new SupersonicBoostPenalty(), 9.0f),
            WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
            WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 6.0f),
            WeightedReward(new BigBoostPadCenterReward(), 20.0f),
            WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

            WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 10.0f),
            WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f),
            WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 1.5f),
            WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.3f), 35.0f),
            WeightedReward(new ZeroSumReward(new DoomseeDisshShotReward(), 0.3f), 35.0f),
            WeightedReward(new ZeroSumReward(new ChallengeCommitmentReward(), 0.5f), 10.0f),
            WeightedReward(new ZeroSumReward(new AerialCommitmentReward(), 0.5f), 15.0f),

            WeightedReward(new DefensivePositionReward(), 2.5f),
            WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f),
            WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 15.0f),
            WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 1.0f), 1.9f),
            WeightedReward(new ZeroSumReward(new BigClearReward(), 0.5f), 10.0f),
            WeightedReward(new ZeroSumReward(new CornerClearReward(), 0.5f), 3.0f),
            WeightedReward(new DefensivePositioningChallengeReward(), 2.5f),
            WeightedReward(new BackboardDefenseReward(), 5.0f),
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
            WeightedReward(new OwnNetCenteringPenalty(), 15.0f),
            WeightedReward(new EmergencySaveCommitReward(), 4.0f),

            WeightedReward(new ZeroSumReward(new HighPassReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new ForwardPassReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 40.0f),
            WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 40.0f),
            WeightedReward(new PassToOpponentPenalty(), 25.0f),

            WeightedReward(new RetreatAfterTouchReward(RotationPhase::CHAMPION_PLUS), 5.0f),
            WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 20.0f),
            WeightedReward(new BoostAwareRotationReward(RotationPhase::CHAMPION_PLUS), 5.0f),

            WeightedReward(new ZeroSumReward(new NumericalAdvantageAttackingReward(), 1.0f), 30.0f),
            WeightedReward(new ZeroSumReward(new DribbleBumpReward(), 0.5f), 60.0f),

            WeightedReward(new DemoAwarenessReward(), 3.0f),
            WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 40.0f),
            WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 60.0f),
            WeightedReward(new ZeroSumReward(new ContextualDemoReward(), 0.5f), 150.0f),
            WeightedReward(new ZeroSumReward(new SoloDefenderBumpReward(), 0.5f), 60.0f),

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
