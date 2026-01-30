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
                // Reduce basics heavily
                WeightedReward(new ZeroSumReward(new StrongTouchReward(40, 130), 0.3f), 20.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 0.3f), 50.0f),
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 20.0f),
                WeightedReward(new InstantSpacingReward(RotationPhase::DIAMOND), 6.0f),  // UPGRADED
                WeightedReward(new RetreatAfterTouchReward(RotationPhase::DIAMOND), 6.0f),
                WeightedReward(new PowerSlideControlReward(), 3.0f),
                WeightedReward(new DoubleCommitPenalty(), 20.0f),
                WeightedReward(new ProperLandingReward(), 0.4f),
                WeightedReward(new AdvancedRecoveryReward(), 0.25f),
                WeightedReward(new ReverseRecoveryReward(), 0.2f),
                WeightedReward(new GoalPostRecoveryReward(), 0.5f),
                WeightedReward(new AirReward(), 0.45f),

                // Aerial/wall progression
                WeightedReward(new ZeroSumReward(new AdvancedDoubleTapReward(), 0.25f), 20.0f),
                WeightedReward(new ZeroSumReward(new WallToAerialReward(), 0.25f), 2.0f),
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 0.3f), 45.0f),
                WeightedReward(new ZeroSumReward(new WallShotReward(), 0.3f), 4.0f),
                //WeightedReward(new ZeroSumReward(new WallOutplayReward(), 0.3f), 3.0f),
                //WeightedReward(new ZeroSumReward(new WallClearReward(), 0.5f), 1.0f),
                //WeightedReward(new ZeroSumReward(new WallTeamPassReward(), 0.5f), 3.0f),
                WeightedReward(new ZeroSumReward(new CornerClearReward(), 0.5f), 5.0f),
                WeightedReward(new WallPossessionGiveawayPenalty(), 2.5f),
                WeightedReward(new WallPlayReward(), 1.0f),
                WeightedReward(new FlipResetDetectionReward(), 40.0f),
                WeightedReward(new ZeroSumReward(new TouchHeightWallScalarReward(), 0.2f), 20.0f),
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 0.3f), 25.0f),
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 0.3f), 10.0f),

                // NEW: Dribbling (MAIN FOCUS)
                WeightedReward(new ZeroSumReward(new DribbleReward(), 0.2f), 1.0f),
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 0.2f), 4.0f),
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 0.2f), 3.0f),
                WeightedReward(new ZeroSumReward(new CatchBallReward(), 0.2f), 3.0f),
                WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 0.2f), 2.0f),

                // NEW: Flicks and Shots (emerge from dribbling)
                //WeightedReward(new ZeroSumReward(new FlickReward(), 0.3f), 5.0f),
                WeightedReward(new ZeroSumReward(new FlickExplorationReward(), 0.3f), 5.0f),
                WeightedReward(new ZeroSumReward(new AdvancedFlickReward(), 0.3f), 10.0f),
                WeightedReward(new ZeroSumReward(new FlickOnGoalReward(), 0.3f), 20.0f),
                WeightedReward(new ZeroSumReward(new FlickOutplayReward(), 0.4f), 20.0f),
                WeightedReward(new ZeroSumReward(new BallControlAngleReward(), 0.3f), 0.1f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 0.4f), 25.0f),
                WeightedReward(new ZeroSumReward(new DemoBumpOutplayReward(), 0.5f), 30.0f),
                WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 0.4f), 1.2f),
                WeightedReward(new ZeroSumReward(new FakeChallengeReward(), 0.4f), 1.0f),
                WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 0.4f), 30.0f),
                WeightedReward(new ZeroSumReward(new AirRollShotReward(), 0.3f), 10.0f),
                WeightedReward(new ZeroSumReward(new BallRedirectReward(), 0.3f), 30.0f),

                // NEW: PASSING REWARDS (Progressive system)
                // Back passes: Safe, possession-focused
                WeightedReward(new ZeroSumReward(new BackPassReward(), 0.5f), 4.0f),
                // Forward passes: Requires teammate positioning awareness
                //WeightedReward(new ZeroSumReward(new ForwardPassReward(), 0.5f), 12.0f),
                // High passes: Lobs, aerials, advanced setups
                //WeightedReward(new ZeroSumReward(new HighPassReward(), 0.5f), 15.0f),
                // Offensive plays: Direct scoring setup passes
                //WeightedReward(new ZeroSumReward(new OffensivePlayReward(), 0.5f), 18.0f),

                // NEW: Air dribbling (combines aerials + dribbling)
                WeightedReward(new ZeroSumReward(new AirDribbleAccuracyReward(), 0.3f), 10.0f), // initially farmed this + AirDribbleReward at 200.0f and 120.0f respectively.
                WeightedReward(new ZeroSumReward(new AirDribbleClearingReward(), 0.3f), 6.0f),  // Defensive clearing
                WeightedReward(new ZeroSumReward(new AirDribbleBumpReward(), 0.3f), 40.0f),  // Defensive clearing

                // NEW: Possession Reward
                WeightedReward(new ZeroSumReward(new PossessionControlReward(), 1.0f), 10.0f),
                //WeightedReward(new CornerPlayPenalty(), 25.0f),
                //WeightedReward(new CenterPlayReward(), 12.0f),

                // NEW: Advanced movement (emerge naturally)
                WeightedReward(new WavedashBasicReward(), 35.0f),
                WeightedReward(new HalfFlipReward(), 2.5f),
                WeightedReward(new EnhancedSpeedFlipReward(), 8.0f),

                // NEW: PASSING REWARDS
                WeightedReward(new ZeroSumReward(new PassingSetupReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new ReceivingPassReward(), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new PassingAccuracyReward(), 1.0f), 30.0f),

                // NEW: Kickoffs
                //WeightedReward(new KickoffReward(), 4.0f),
                //WeightedReward(new KickoffCompositeReward(), 5.0f),
                //WeightedReward(new KickoffFormReward(), 2.0f),
                WeightedReward(new KickoffSpeedReward(), 15.0f),
                WeightedReward(new KickoffBackPlayerBoostReward(), 30.0f),
                WeightedReward(new KickoffFlipReward(), 15.0f),

                // NEW: Rotation (CRITICAL FOR 3v3)
                WeightedReward(new ProgressiveRotationReward(RotationPhase::DIAMOND), 25.0f),

                // Advanced boost
                WeightedReward(new PickupBoostReward(), 50.0f),
                WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
                WeightedReward(new BigBoostPadCenterReward(), 10.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 20.0f),
                WeightedReward(new SaveBoostReward(), 0.4f),
                WeightedReward(new BoostPathReward(), 10.0f),
                WeightedReward(new SmallPadCollectionReward(), 3.0f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 0.5f), 70.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 0.7f), 4.0f),
                WeightedReward(new DribbleBoostInefficencyPunish(), 4.0f),
                //WeightedReward(new SupersonicBoostPenalty(), 10.0f),
                WeightedReward(new BoostWastePenaltyReward(), 12.0f),
                WeightedReward(new FrontFlipBoostWasteReward(), 5.0f),
                WeightedReward(new KickoffBoostRetentionReward(), 15.0f),
                WeightedReward(new SupersonicReward(), 15.0f),
                // Supporting player boost management
                WeightedReward(new SupportingPlayerBoostMaintenanceReward(), 5.0f),

                // Confidence and Hesitation Rewards/punishments
                WeightedReward(new ZeroSumReward(new ChallengeCommitmentReward(1200.0f, 2000.0f), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new FirstToBallReward(), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new ExcessiveShadowPunishment(), 0.5f), 2.0f),
                WeightedReward(new ZeroSumReward(new AerialCommitmentReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new FiftyAggressionReward(), 0.5f), 10.0f),
                WeightedReward(new ZeroSumReward(new ImmediateShotReward(1.0f), 0.3f), 50.0f),

                // Combat Rewards. Made the weights super high so we can try to farm them.
                WeightedReward(new DemoAwarenessReward(), 3.0f),
                WeightedReward(new ZeroSumReward(new MasterDemoTacticsReward(), 0.5f), 60.0f),
                WeightedReward(new TeammteBumpPenalty(), 15.0f),
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 60.0f),
                WeightedReward(new ZeroSumReward(new DemoOffenseReward(), 0.5f), 100.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 100.0f),

                // Shots
                WeightedReward(new ZeroSumReward(new OpenNetShotReward(), 0.3f), 45.0f),
                WeightedReward(new ZeroSumReward(new LongShotReward(), 0.3f), 30.0f),
                WeightedReward(
                    new ZeroSumReward(
                        new MathRewardWrapper(
                            {
                                new SpeedScaledShotReward(),
                                new ShotAccuracyReward()
                            },
                            MathOp::MULTIPLY
                        ),
                        0.3f
                    ),
                    200.0f
                ),

                // NEW: SHOOTING REWARDS (Hard shots & outplay focus)
                // Power shots: Fast, hard shots toward goal
                WeightedReward(new ZeroSumReward(new PowerShotReward(), 0.4f), 40.0f),
                // Accurate shots: Post-side aiming and placement
                WeightedReward(new ZeroSumReward(new AccurateShotReward(), 0.3f), 30.0f),
                // Contested shots: Shooting under defender pressure
                WeightedReward(new ZeroSumReward(new ContestedShotReward(), 0.3f), 36.0f),
                // Basic outplay: 1v1 beats and cutting past defenders
                WeightedReward(new ZeroSumReward(new BasicOutplayReward(), 0.3f), 40.0f),
                // Open goal shots: High reward for undefended opportunities
                WeightedReward(new ZeroSumReward(new OpenGoalShotReward(), 0.4f), 50.0f),

                // Defense and Positional Rewards
                WeightedReward(new LowBoostOppHalfFiftyPunish(), 12.0f),
                WeightedReward(new ZeroSumReward(new SpeedScaledSaveReward(), 0.5f), 70.0f),
                //WeightedReward(new ZeroSumReward(new SaveReward(), 0.5f), 50.0f),
                WeightedReward(new BackboardDefenseReward(), 5.0f),
                WeightedReward(new DefensivePositionReward(), 3.0f),
                WeightedReward(new EarlyDangerDetectionReward(), 5.0f),
                WeightedReward(new GoalpostPositioningReward(), 6.5f),
                WeightedReward(new ZeroSumReward(new GoalLineClearanceReward(), 0.5f), 12.0f),
                WeightedReward(new ZeroSumReward(new BallTrajectoryInterceptionReward(), 0.5f), 3.0f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 3.0f),
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 0.5f), 2.7f),
                WeightedReward(new ZeroSumReward(new FiftyFiftyReward(), 0.3f), 8.0f),
                WeightedReward(new ZeroSumReward(new SingleJumpLow5050Reward(), 0.3f), 6.0f),
                //WeightedReward(new EmergencySaveCommitReward(), 2.0f),
                // Defensive challenge positioning
                WeightedReward(new DefensivePositioningChallengeReward(), 1.0f),
                // Active supporting (2nd man) positioning for team play
                WeightedReward(new ActiveSupportingPositionReward(), 5.0f),


                // Goals
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 100.0f),
                WeightedReward(new SpeedScaledGoalReward(-0.5f), 1200.0f)
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

                // Active supporting (2nd man) positioning for team play
                WeightedReward(new ActiveSupportingPositionReward(), 30.0f),

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
                WeightedReward(new KickoffFlipReward(), 15.0f),
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
