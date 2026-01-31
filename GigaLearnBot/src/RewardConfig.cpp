#pragma once
#include "RewardConfig.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/PassingRewards.h"
#include "../GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/ShootingRewards.h"

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

    case TrainingPhase::PHASE_5_DIAMOND:
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
