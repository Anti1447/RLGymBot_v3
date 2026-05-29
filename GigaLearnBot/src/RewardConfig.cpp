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
                WeightedReward(new BigBoostPadCenterReward(), 10.0f),
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
                //WeightedReward(new VelocityPlayerToBallReward(), 0.5f),  // VERY REDUCED
			    WeightedReward(new TrajectoryComparisonVPBReward(), 1.2f),  // replaces VPTB with more nuanced velocity shaping. dont use at the same time as other VPTB to avoid gradient confusion
                WeightedReward(new ZeroSumReward(new StrongTouchReward(25, 110), 0.3f), 40.0f),  // Stricter
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 50.0f),
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 8.0f),
                WeightedReward(new PowerSlideControlReward(), 0.8f),
                WeightedReward(new GoalReward(-1.0f), 600.0f),

                // NEW: Aerial focus
                WeightedReward(new ZeroSumReward(new AirTouchReward(), 0.3f), 25.0f),  // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 0.3f), 35.0f),
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 15.0f), // begin decreasing so we can focus on other rewards. covered by AerialFirstTouchReward
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new JumpTouchReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 15.0f),

                // NEW: Recovery (needed for aerials)
                WeightedReward(new HalfFlipReward(), 3.5f),
                WeightedReward(new WavedashBasicReward(), 10.0f),
                WeightedReward(new EnhancedSpeedFlipReward(), 8.0f),
                WeightedReward(new ReverseRecoveryReward(), 0.5f),
                WeightedReward(new DirectionalFlipRecoveryReward(), 0.5f),
                WeightedReward(new ProperLandingReward(), 1.0f),
                WeightedReward(new AdvancedRecoveryReward(), 0.7f),

                // Powerslide turning mechanics (Phase 3 intro — conservative weights)
                WeightedReward(new PowerslideSharpTurnReward(), 1.5f),
                WeightedReward(new Powerslide180ForwardReward(), 1.5f),
                WeightedReward(new Powerslide180ReverseReward(), 1.5f),
                //WeightedReward(new Powerslide360Reward(), 2.0f),

                //Kickoffs
                WeightedReward(new KickoffSpeedReward(), 15.0f),

                // NEW: Boost management (critical for aerials)
                WeightedReward(new PickupBoostReward(), 40.0f),  // INCREASED
                WeightedReward(new SaveBoostReward(), 0.6f),
                WeightedReward(new SmallPadCollectionReward(), 4.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 50.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 3.0f),
                WeightedReward(new BoostWastePenaltyReward(), 6.0f),
                //WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
                WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 2.0f),
                //WeightedReward(new BigBoostPadCenterReward(), 10.0f),
                //WeightedReward(new DefensiveRotationBoostReward(), 20.0f),

                // NEW: Dribbling
                WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 2.0f),
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.3f), 8.0f),
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.4f), 1.5f),
                WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 1.0f),
                WeightedReward(new ZeroSumReward(new CompositeFlickReward(), 0.3f), 40.0f),
                WeightedReward(new ZeroSumReward(new FlickOutplayReward(), 0.5f), 40.0f),

                // NEW: Basic Shots
                WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f), // updated reward on 01/30/26

                // Power-shot physics (Phase 3 intro — conservative weights)
                // Core: impact angle × mass alignment via MULTIPLY. A hit needs
                // BOTH correct angle AND correct contact geometry to score, so
                // neither signal can be farmed independently.
                // Ground shots
                WeightedReward(new ZeroSumReward(
                    new PowerShotContextGate(
                        new MathRewardWrapper({ new PowerShotImpactAngleReward(),
                                               new PowerShotMassAlignmentReward() },
                                             MathOp::MULTIPLY),
                        PowerShotContextGate::GROUND), 1.0f), 5.0f),
                // Half-volley (airborne, low rising ball)
                WeightedReward(new ZeroSumReward(
                    new PowerShotContextGate(
                        new MathRewardWrapper({ new PowerShotImpactAngleReward(),
                                               new PowerShotMassAlignmentReward() },
                                             MathOp::MULTIPLY),
                        PowerShotContextGate::HALF_VOLLEY), 1.0f), 5.0f),
                // Volley (airborne, ball high)
                WeightedReward(new ZeroSumReward(
                    new PowerShotContextGate(
                        new MathRewardWrapper({ new PowerShotImpactAngleReward(),
                                               new PowerShotMassAlignmentReward() },
                                             MathOp::MULTIPLY),
                        PowerShotContextGate::VOLLEY), 1.0f), 5.0f),
                // Aerial no-dodge (hardest — flip already spent; no dodge-timing)
                WeightedReward(new ZeroSumReward(
                    new PowerShotContextGate(
                        new MathRewardWrapper({ new PowerShotImpactAngleReward(),
                                               new PowerShotMassAlignmentReward() },
                                             MathOp::MULTIPLY),
                        PowerShotContextGate::AERIAL_NODODGE), 1.0f), 5.0f),
                // Dodge-timing (suppress for AERIAL_NODODGE — no dodge to time)
                WeightedReward(new ZeroSumReward(
                    new PowerShotContextGate(new PowerShotDodgeTimingReward(),
                        PowerShotContextGate::GROUND), 1.0f), 2.0f),
                WeightedReward(new ZeroSumReward(
                    new PowerShotContextGate(new PowerShotDodgeTimingReward(),
                        PowerShotContextGate::HALF_VOLLEY), 1.0f), 2.0f),
                WeightedReward(new ZeroSumReward(
                    new PowerShotContextGate(new PowerShotDodgeTimingReward(),
                        PowerShotContextGate::VOLLEY), 1.0f), 2.0f),

                // NEW: Minor possession shaping and defense
                WeightedReward(new DefensivePositionReward(), 1.5f),
                WeightedReward(new BackboardDefenseReward(), 1.5f),
                WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 6.0f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f),  // P4: 2.5 → 2.0
                WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 1.5f),   // P4: 10.0 → 1.0 (light intro)
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 0.3f), 2.0f),
                WeightedReward(new SimpleOvercommitmentPenalty(), 7.0f),  // Phase 1: Resource management & positioning discipline

                // NEW: Passing rewards
                WeightedReward(new ZeroSumReward(new BackPassReward(), 0.5f), 2.0f),

                // Rotation handled entirely by ProgressiveRotationReward (includes spacing + retreat internally)
                WeightedReward(new ProgressiveRotationReward(RotationPhase::PLATINUM), 4.0f),

                // NEW: Numerical advantage attacking (2v1+ situations)
                WeightedReward(new NumericalAdvantageAttackingReward(), 10.0f),

                // NEW: Dribble bump reward (aggressive ground dribbles with bumps)
                WeightedReward(new DribbleBumpReward(), 10.0f),

                WeightedReward(new DemoAwarenessReward(), 1.0f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 120.0f),

                // NEW: Basic shots
                WeightedReward(new ZeroSumReward(new ShotReward(), 0.3f), 80.0f),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 60.0f)
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
			    //WeightedReward(new VelocityPlayerToBallReward(), 0.25f),  // VERY REDUCED
                WeightedReward(new VelocityReward(), 0.1f),
                WeightedReward(new ZeroSumReward(new StrongTouchReward(70, 140), 0.3f), 25.0f),  // Stricter
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 35.0f),
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

                // NEW: Team boost cooperation (role-based preference)
                WeightedReward(new RoleBasedBoostPreferenceReward(), 0.3f),

                // Basic Shots and Outplays
                WeightedReward(new ZeroSumReward(new HookShotReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new DirectionChangeReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.3f), 30.0f), // updated reward on 01/30/26. Added to Option Control above
                WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 1.4f), // Added to Option Control above
                WeightedReward(new ZeroSumReward(new AirRollShotReward(), 0.3f), 8.0f),

                // NEW: Minor possession shaping and defense
                WeightedReward(new DefensivePositionReward(), 2.5f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.0f),  // P4: 2.5 → 2.0
                WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 1.0f),   // P4: 10.0 → 1.0 (light intro)
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

                // Spatial lane control (team modes only — returns 0 in 1v1)
                WeightedReward(new PassingLaneNetworkReward(PassingLaneNetworkReward::Config::Warm()), 3.0f),

                // Rotation handled entirely by ProgressiveRotationReward (includes spacing + retreat internally)
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
                WeightedReward(new PowerSlideControlReward(), 0.55f),
                WeightedReward(new GoalReward(-1.0f), 900.0f),
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 30.0f),  // Gradually reducing (was 35)
                WeightedReward(new ZeroSumReward(new EnhancedVelocityBallToGoalReward(), 1.0f), 5.0f),  // New enhanced variant (30% of original weight)
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
                WeightedReward(new KickoffStrategyReward(), 25.0f),  // Reward diverse kickoff strategies
                //WeightedReward(new KickoffRandomizationReward(), 15.0f),  // Prevent repetitive patterns
                // Kickoff - Encourage closest player to rush ball
                WeightedReward(new ClosestPlayerSpeedReward(), 12.0f),
                // Kickoff - Penalize team if nobody commits to the ball
                WeightedReward(new KickoffNoChaseTeamPenalty(), 15.0f),

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
                WeightedReward(new ZeroSumReward(new ShotPlacementReward(), 0.5f), 25.0f),
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
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f),  // P4: 3.0 → 2.5
                WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 6.0f),   // P4: 15.0 → 6.0 (reduce dominance)
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

                // Spatial lane control (team modes only — returns 0 in 1v1)
                WeightedReward(new PassingLaneNetworkReward(PassingLaneNetworkReward::Config::Balanced()), 4.0f),
                WeightedReward(new SpatialFakeReward(), 2.5f),

                // Rotation handled entirely by ProgressiveRotationReward (includes spacing + retreat internally)
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
                WeightedReward(new DeepBackPostCoverageReward(), 3.0f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 30.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new ContextualDemoReward(), 0.5f), 80.0f),
                // 2nd-man offensive demo: approach + event, MIDDLE role only, not ZeroSummed.
                WeightedReward(new SecondManOffensiveDemoReward(), 35.0f),
                WeightedReward(new ZeroSumReward(new PossessionSideDemoReward(), 1.0f), 5.0f),
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
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 40.0f),
                WeightedReward(new AerialOutplayReward(), 5.0f)  // Tactical awareness during aerials
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

                // Aerial focus
                WeightedReward(new ZeroSumReward(new PitchControlAerialReward(), 0.4f), 2.0f),
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 1.0f), 60.0f),
                WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.5f), 6.0f),
                //WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.5f), 70.0f),
                //WeightedReward(new AirDribbleSetupReward(), 8.0f),
                //WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new AirDribbleBumpReward(), 1.0f), 18.0f),  // Reward bumping opponent during air dribble toward goal
                WeightedReward(new ZeroSumReward(new AirDribbleBumpConversionReward(), 0.5f), 60.0f),
                WeightedReward(new FlipResetApproachReward(), 60.0f),
                //WeightedReward(new SimpleFlipResetReward(), 400.0f),
                WeightedReward(new UnifiedAirDribbleReward(), 30.0f),
				WeightedReward(new FlipResetReward(), 50.0f), // new reward for achieving flip reset state (car regains jump after touching ball in air)
                WeightedReward(new SimpleFlipResetReward(), 350.0f),
                WeightedReward(new ZeroSumReward(new UnifiedDoubleTapReward(), 0.3f), 150.0f),
                //WeightedReward(new AirReward(), 0.25f),   // or whatever weight fits your phase
                WeightedReward(new AirRollReward(), 5.0f),   // or whatever weight fits your phase
                WeightedReward(new InvertedAerialReward(), 5.0f),  // inverted aerial (wheels-up flying)
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 5.0f),
                WeightedReward(new ReverseAerialReward(), 4.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 0.3f), 15.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new AerialOutplayReward(), 0.3f), 60.0f),  // Tactical awareness during aerials

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
                WeightedReward(new WavedashBasicReward(), 1.0f),
                //WeightedReward(new WaveDashHeightReward(), 2.0f),
                //WeightedReward(new WallDashReward(), 15.0f),
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
                WeightedReward(new SupersonicReward(), 8.0f),

                // Kickoff
                WeightedReward(new ClosestPlayerSpeedReward(), 7.0f),
                WeightedReward(new KickoffSpeedReward(), 15.0f),
                WeightedReward(new ClosestPlayerKickoffSideReward(), 30.0f),
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
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 3.0f),
                WeightedReward(new BoostWastePenaltyReward(), 20.0f),
                WeightedReward(new SupersonicBoostPenalty(), 25.0f),
                WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 12.0f),
                WeightedReward(new BigBoostPadCenterReward(), 25.0f),   // 20→10: very low effective yield (0.005)
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
                WeightedReward(new ZeroSumReward(new AirRollShotReward(), 0.3f), 10.0f),
                WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.3f), 70.0f),
                WeightedReward(new ZeroSumReward(new DoomseeDisshShotReward(), 0.3f), 25.0f),
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
                WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 5.0f),   // teaches it what to do with the ball once it has it.
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 1.0f), 2.5f),
                WeightedReward(new ZeroSumReward(new BigClearReward(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new CornerClearReward(), 0.5f), 3.0f),
                WeightedReward(new DefensivePositioningChallengeReward(), 2.0f),
                WeightedReward(new BackboardDefenseReward(), 5.0f),
                //WeightedReward(new EarlyDangerDetectionReward(), 0.9f),
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
                // 2nd-man upfield holding: fires only for MIDDLE-role player when teammate has
                // possession. Rewards staying 1000-5000 UU upfield (closer to opp goal than ball).
                // Counteracts redundant ball-chasing when a teammate is already the primary.
                WeightedReward(new ActiveSupportingPositionReward(), 24.0f),
                //WeightedReward(new DeepBackPostCoverageReward(), 0.5f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 500.0f),
                WeightedReward(new BumpReward(), 200.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 750.0f),
                WeightedReward(new SecondManOffensiveDemoReward(), 200.0f),
                WeightedReward(new ZeroSumReward(new PossessionSideDemoReward(), 1.0f), 250.0f),
                WeightedReward(new DemoReward(), 250.0f),
                // Demo opponent who is predicted to intercept a goal-bound ball.
                // Fires only when BallPred shows ball heading to opp goal AND target is on trajectory.
                WeightedReward(new ZeroSumReward(new InterceptClearingDemoReward(), 0.5f), 200.0f),
                WeightedReward(new DemoedPenalty(), 60.0f),
                WeightedReward(new ZeroSumReward(new ContextualDemoReward(), 0.5f), 40.0f),  // 80→40: 0.09% activation rate, near-dead signal
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
            WeightedReward(new PowerSlideControlReward(), 0.55f),
            WeightedReward(new GoalReward(-1.0f), 1000.0f),

            WeightedReward(new ZeroSumReward(new PitchControlAerialReward(), 0.4f), 10.0f),
            WeightedReward(new ZeroSumReward(new FirstToBallReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 1.0f), 10.0f),
            WeightedReward(new ZeroSumReward(new EarlyAerialReward(), 0.5f), 5.0f),
            WeightedReward(new ZeroSumReward(new AirDribbleSetupReward(), 0.5f), 20.0f),
            WeightedReward(new ZeroSumReward(new UnifiedAirDribbleReward(), 0.5f), 60.0f),
            WeightedReward(new ZeroSumReward(new AirDribbleBumpReward(), 1.0f), 20.0f),  // Reward bumping opponent during air dribble toward goal
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
            WeightedReward(new SecondaryAerialPenalty(), 8.0f),
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
            WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.0f),  // P4: 2.5 → 2.0
            WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 6.0f),   // P4: 15.0 → 6.0
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
            WeightedReward(new ZeroSumReward(new DribbleBumpReward(), 0.5f), 60.0f),

            WeightedReward(new DemoAwarenessReward(), 3.0f),
            WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 40.0f),
            WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 60.0f),
            WeightedReward(new ZeroSumReward(new ContextualDemoReward(), 0.5f), 150.0f),
            WeightedReward(new ZeroSumReward(new SoloDefenderBumpReward(), 0.5f), 60.0f),
            WeightedReward(new ZeroSumReward(new PossessionSideDemoReward(), 1.0f), 15.0f),

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