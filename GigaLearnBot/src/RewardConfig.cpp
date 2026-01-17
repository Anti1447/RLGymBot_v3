#pragma once
#include "RewardConfig.h"

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
                WeightedReward(new FaceBallReward(), 0.25f),
                WeightedReward(new VelocityPlayerToBallReward(), 5.0f),
                WeightedReward(new AdvancedRecoveryReward(), 0.3f),
                WeightedReward(new ProperLandingReward(), 0.4f),
                WeightedReward(new AirReward(), 0.25f), // don't forget how to jump
                WeightedReward(new WavedashBasicReward(), 6.0f),
                WeightedReward(new EnhancedSpeedFlipReward(), 10.0f),
                WeightedReward(new HalfFlipReward(), 2.0f),
                WeightedReward(new TurtledPenaltyReward(), 6.0f),

                WeightedReward(new StrongTouchReward(20, 100), 20.0f),
                WeightedReward(new TouchAccelReward(), 20.0f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 0.4f),

                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 3.0f),

                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 80.0f),

                WeightedReward(new PickupBoostReward(), 10.0f),
                WeightedReward(new SaveBoostReward(), 0.2f),

                WeightedReward(new GoalReward(-1.f), 150.0f)
        };

        // ========================================================================
        // PHASE 2: SILVER (1-3B steps)
        // Focus: Ground control, simple aerials, basic recoveries
        // ========================================================================
    case TrainingPhase::PHASE_2_SILVER:
        return std::vector<WeightedReward>{
			    // Basic Movement and Recovery Rewards
                //WeightedReward(new VelocityReward(), 0.6f),
                WeightedReward(new VelocityPlayerToBallReward(), 5.0f),
                //WeightedReward(new FaceBallReward(), 0.3f),
                WeightedReward(new AirReward(), 0.15f),
                //WeightedReward(new AdvancedRecoveryReward(), 0.3f),
                //WeightedReward(new ProperLandingReward(), 0.4f),
                WeightedReward(new WavedashBasicReward(), 10.0f),
                WeightedReward(new EnhancedSpeedFlipReward(), 10.0f),
                WeightedReward(new HalfFlipReward(), 4.0f),
                WeightedReward(new TurtledPenaltyReward(), 8.0f),
                WeightedReward(new GoodRecoveryReward(1000.0f, 10.0f), 4.0f),
                WeightedReward(new PowerSlideControlReward(), 1.0f),
                
				// Teamplay and Positioning Rewards
                WeightedReward(new DoubleCommitPenalty(1000.0f, 0.1f, 300.0f), 2.0f),
                WeightedReward(new FieldSpacingReward(), 1.0f),
                WeightedReward(new MiddleRotationReward(16.0f, 3000.0f, 0.0f, 0.15f, 0.5f), 1.0f),
                

                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 1.0f), 10.0f), // replaces wall touch & air touch rewards
                WeightedReward(new ZeroSumReward(new AirTouchReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 1.0f), 5.0f),
                WeightedReward(new ZeroSumReward(new JumpTouchReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new StrongTouchReward(25, 110), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 0.8f),
                WeightedReward(new ZeroSumReward(new CatchBallReward(), 1.0f), 5.0f),

				// Kickoff Rewards
                WeightedReward(new ZeroSumReward(new KickoffFirstTouchReward(), 1.0f), 10.0f),
                WeightedReward(new SideFlipKickoffReward(), 15.0f),
                WeightedReward(new FlipDuringKickoffReward(), 15.0f),

                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 20.0f),

                // Boost Rewards
                WeightedReward(new PickupBoostReward(), 15.0f),
                WeightedReward(new SaveBoostReward(), 0.3f),
                WeightedReward(new BoostPathReward(), 5.0f),
                WeightedReward(new SmallPadCollectionReward(), 1.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 1.0f), 25.0f),

                // Combat Rewards
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 80.0f),
                WeightedReward(new ZeroSumReward(new BumpedPenalty(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new DemoedPenalty(), 0.5f), 40.0f),
                WeightedReward(new TeammteBumpPenalty(0.5f), 20.0f),

                // Basic Game Event Rewards
                WeightedReward(new SpeedScaledGoalReward(-1.0f), 250.0f),
                WeightedReward(
                    new ZeroSumReward(
                        new MathRewardWrapper(
                            {
                                new SpeedScaledShotReward(),
                                new ShotAccuracyReward()
                            },
                            MathOp::MULTIPLY
                        ),
                        1.0f
                    ),
                    40.0f
                ),
                WeightedReward(new ZeroSumReward(new ShotOnGoalReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new SpeedScaledSaveReward(), 1.0f), 40.0f),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 40.0f)
        };

        // ========================================================================
        // PHASE 3: GOLD (3-5B steps)
        // Focus: Wall play, power hits, basic defense
        // ========================================================================
    case TrainingPhase::PHASE_3_GOLD:
        return std::vector<WeightedReward>{
                WeightedReward(new VelocityReward(), 0.3f),
                WeightedReward(new VelocityPlayerToBallReward(), 1.0f),
                WeightedReward(new AirReward(), 0.3f),
                WeightedReward(new ZeroSumReward(new LiuPlayerBallDistanceReward(), 1.0f), 0.5f),
                WeightedReward(new ZeroSumReward(new LiuDistanceBallToGoalReward(), 1.0f), 2.0f),

                WeightedReward(new ZeroSumReward(new AirTouchReward(), 1.0f), 24.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new JumpTouchReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new StrongTouchReward(20, 110), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.0f),
                WeightedReward(new ZeroSumReward(new DribbleControlReward(), 1.0f), 6.0f),
                WeightedReward(new ZeroSumReward(new FlickReward(), 1.0f), 8.0f),
                WeightedReward(new CornerClearReward(), 5.0f),
                WeightedReward(new ZeroSumReward(new AirRollShotReward(), 1.0f), 1.0f),
                WeightedReward(new DemoAwarenessReward(), 5.0f),

                WeightedReward(new HalfFlipReward(), 6.0f),
                WeightedReward(new PowerSlideControlReward(), 1.5f),
                WeightedReward(new WavedashReward(), 4.0f),

                WeightedReward(new ZeroSumReward(new OffensivePossessionPositionReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new DefensivePositionReward(), 1.0f), 3.0f),
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 20.0f),

                WeightedReward(new PickupBoostReward(), 6.0f),
                WeightedReward(new SmallPadCollectionReward(), 1.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 1.0f), 10.0f),
                WeightedReward(new SaveBoostReward(), 0.25f),


                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 40.0f),

                // Reduced penalty for conceding to encourage aggression
                WeightedReward(new GoalReward(-0.75f), 300.0f),
                WeightedReward(new ZeroSumReward(new ShotReward(), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 20.0f)
        };

        // ========================================================================
        // PHASE 4: PLATINUM (5-8B steps)
        // Focus: Air control, 50/50s, boost management, dribbling intro
        // ========================================================================
    case TrainingPhase::PHASE_4_PLATINUM:
        return std::vector<WeightedReward>{
                // Basic Mechanics
                WeightedReward(new DemoAwarenessReward(), 3.4f),
                WeightedReward(new AdvancedRecoveryReward(), 0.7f),
                WeightedReward(new ProperLandingReward(), 0.9f),
                WeightedReward(new PowerSlideControlReward(), 3.0f),
                WeightedReward(new WavedashBasicReward(), 24.0f),
                WeightedReward(new WavedashReward(), 3.0f),
                WeightedReward(new EnhancedSpeedFlipReward(), 18.0f),
                WeightedReward(new EmergencySaveCommitReward(), 2.0f),


                // Aerial Rewards
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new TouchHeightWallScalarReward(), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new WallPopReward(), 1.0f), 7.0f),
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new UpsideDownAerialReward(), 1.0f), 30.0f),

                // Ball Touch Rewards (boosted for aggression)
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 80.0f),
                WeightedReward(new ZeroSumReward(new OpenNetShotReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new ShotQualityReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new LongShotReward(), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 1.0f), 35.0f),
                WeightedReward(new ZeroSumReward(new BackboardDefenseReward(), 1.0f), 4.0f),
                WeightedReward(new ZeroSumReward(new SingleJumpLow5050Reward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new BallRedirectReward(), 1.0f), 25.0f),
                WeightedReward(new LowBoostOppHalfFiftyPunish(), 5.0f),

                // Outplay Rewards (boosted for aggressive playmaking)
                WeightedReward(new ZeroSumReward(new AdvancedFlickReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new FlickShotReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new FlickOutplayReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new FlickOnGoalReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 1.0f), 1.8f),
                WeightedReward(new ZeroSumReward(new DemoBumpOutplayReward(), 0.5f), 40.0f),

                // Possession and Dribbling (offensive focus)
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 1.0f), 13.0f),
                WeightedReward(new ZeroSumReward(new BallControlAngleReward(), 1.0f), 0.15f),
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 1.0f), 9.0f),
                WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 1.0f), 7.5f),
                WeightedReward(new ZeroSumReward(new AirDribbleAccuracyReward(), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new LowBoostDribblePunishment(), 1.0f), 3.0f),
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 1.0f), 3.3f),
                WeightedReward(new ZeroSumReward(new OrbitBallPunishment(), 1.0f), 2.0f),

                // Boosted ball-to-goal velocity for offensive pressure
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 25.0f),

                // Confidence and Hesitation Rewards/punishments
                WeightedReward(new ZeroSumReward(new ChallengeCommitmentReward(1200.0f, 2000.0f), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new FirstToBallReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new ImmediateShotReward(0.5f), 1.0f), 35.0f),

                // Kickoff Rewards
                WeightedReward(new ZeroSumReward(new KickoffSpeedReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new KickoffFormReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new KickoffReward(), 1.0f), 22.0f),
                WeightedReward(new ZeroSumReward(new KickoffFirstTouchReward(), 1.0f), 10.0f),
                WeightedReward(new SideFlipKickoffReward(), 15.0f),
                WeightedReward(new FlipDuringKickoffReward(), 15.0f),

                // Boost Management Rewards
                WeightedReward(new PostKickoffMidBoostReward(), 20.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 15.0f),
                WeightedReward(new PickupBoostReward(), 30.0f),
                WeightedReward(new BigBoostPadCenterReward(), 30.0f),
                WeightedReward(new BoostDuringFlipReward(), 7.0f),
                WeightedReward(new BoostPathReward(), 20.0f),
                WeightedReward(new SmallPadCollectionReward(), 2.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 1.0f), 35.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 3.0f),
                WeightedReward(new DribbleBoostInefficencyPunish(), 3.5f),
                WeightedReward(new SaveBoostReward(), 0.55f),
                WeightedReward(new SupersonicBoostPenalty(), 11.0f),
                WeightedReward(new SupersonicReward(), 12.0f),

                //Combat Rewards
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new DemoOffenseReward(), 0.5f), 55.0f),
                WeightedReward(new ZeroSumReward(new AirBumpDemoReward(), 0.5f), 85.0f),
                WeightedReward(new ZeroSumReward(new DemoChasingReward(), 0.5f), 0.06f),

                //General Event Rewards
                WeightedReward(new SpeedScaledGoalReward(-1.0f), 1000.0f),
                WeightedReward(
                    new ZeroSumReward(
                        new MathRewardWrapper(
                            {
                                new SpeedScaledShotReward(),
                                new ShotAccuracyReward()
                            },
                            MathOp::MULTIPLY
                        ),
                        1.0f
                    ),
                    60.0f
                ),
                WeightedReward(new ZeroSumReward(new ShotOnGoalReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new SpeedScaledSaveReward(), 1.0f), 60.0f)
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
