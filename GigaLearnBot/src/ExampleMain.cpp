#include <GigaLearnCPP/Learner.h>

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/Rewards/AdvancedRewards.h>
#include <RLGymCPP/Rewards/ConfidenceRewards.h>
#include <RLGymCPP/Rewards/KickoffRewards.h>
#include <RLGymCPP/Rewards/BoostManagementRewards.h>
#include <RLGymCPP/Rewards/CompleteMechanicsRewards.h>
#include <RLGymCPP/Rewards/OutplayRewards.h>
#include <RLGymCPP/Rewards/FlipResetReward.h>
#include <RLGymCPP/Rewards/GodTierMovementRewards_Enhanced.h>
#include <RLGymCPP/Rewards/PossessionRewards.h>
#include <RLGymCPP/Rewards/MovementAndKickoffRewards_Enhanced.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/OBSBuilders/DefaultObs.h>
#include <RLGymCPP/OBSBuilders/AdvancedObs.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/StateSetters/RandomState.h>
#include <RLGymCPP/StateSetters/CombinedState.h>
#include <RLGymCPP/StateSetters/AerialTrainingState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/Gamestates/GameState.h>  // Full definition
#include "DribbleFlickMetrics.h" //same directory as this file

using namespace GGL; // GigaLearn
using namespace RLGC; // RLGymCPP

// ============================================================================
// FULL TRAINING PHASE CONFIGURATION - BRONZE TO PRO ESPORTS
// ============================================================================
// Logarithmic scale - early phases are faster, later phases need more time
// 
// Phase Timeline:
//   Phase 1 (Bronze):     0-1B    steps  (~3 hours at 8B/day)
//   Phase 2 (Silver):     1-3B    steps  (~6 hours)
//   Phase 3 (Gold):       3-5B    steps  (~6 hours)
//   Phase 4 (Platinum):   5-8B    steps  (~9 hours)
//   Phase 5 (Diamond):    8-12B   steps  (~12 hours)
//   Phase 6 (Champion):   12-20B  steps  (~1 day)
//   Phase 7 (GC):         20-40B  steps  (~2.5 days)
//   Phase 8 (SSL):        40-80B  steps  (~5 days)
//   Phase 9 (PRO):        80B+    steps  (ongoing)
//
// Total to SSL: ~10 days at 8B steps/day
// ============================================================================

enum class TrainingPhase {
    PHASE_1_BRONZE,         // 0-1B:    Basic ball chasing, boost collection
    PHASE_2_SILVER,         // 1-3B:    Ground control, simple aerials, recoveries
    PHASE_3_GOLD,           // 3-5B:    Wall play, power hits, basic defense
    PHASE_4_PLATINUM,       // 5-8B:    Air control, 50/50s, boost management
    PHASE_5_DIAMOND,        // 8-12B:   Flicks, dribbles, advanced aerials
    PHASE_6_CHAMPION,       // 12-20B:  Double taps, flip resets, advanced mechanics
    PHASE_7_GC,             // 20-40B:  Speed, consistency, pressure play
    PHASE_8_SSL,            // 40-80B:  Polish, optimization, full game mastery
    PHASE_9_PRO             // 80B+:    Pro esports level refinement
};

// >>> SET YOUR CURRENT PHASE HERE <<<
constexpr TrainingPhase CURRENT_PHASE = TrainingPhase::PHASE_4_PLATINUM;

// ============================================================================
// PHASE CONFIGURATIONS
// ============================================================================

struct PhaseConfig {
    float entropyScale;
    float policyLR;
    float criticLR;
    float noTouchTimeout;
    bool useRandomState;
    float randomStateChance;
    bool useAerialTraining;
    float aerialTrainingChance;
};

PhaseConfig GetPhaseConfig() {
    PhaseConfig cfg;

    // Network size is CONSTANT - defined in main()
    // Only these parameters change per phase

    switch (CURRENT_PHASE) {
    case TrainingPhase::PHASE_1_BRONZE: // OVERFITS AFTER 200M STEPS!!!
        cfg.entropyScale = 0.025f;          // Highest exploration
        cfg.policyLR = 2.5e-4f;
        cfg.criticLR = 2.5e-4f;
        cfg.noTouchTimeout = 15.0f;         // Very lenient
        cfg.useRandomState = false;
        cfg.randomStateChance = 0.0f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;
        break;

    case TrainingPhase::PHASE_2_SILVER:
        cfg.entropyScale = 0.03f;
        cfg.policyLR = 2e-4f;
        cfg.criticLR = 2e-4f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = false;
        cfg.randomStateChance = 0.0f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;
        break;

    case TrainingPhase::PHASE_3_GOLD:
        cfg.entropyScale = 0.028f;
        cfg.policyLR = 1.9e-4f;
        cfg.criticLR = 1.9e-4f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.3f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;
        break;

    case TrainingPhase::PHASE_4_PLATINUM:
        cfg.entropyScale = 0.02f;
        cfg.policyLR = 1.7e-4f;
        cfg.criticLR = 1.7e-4f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.6f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;  // 0% aerial training Not working well yet
        break;

    case TrainingPhase::PHASE_5_DIAMOND:
        cfg.entropyScale = 0.025f;
        cfg.policyLR = 1.6e-4f;
        cfg.criticLR = 1.6e-4f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.5f;
        cfg.useAerialTraining = false;
		cfg.aerialTrainingChance = 0.0f;  // 0% aerial training Not working well yet
        break;

    case TrainingPhase::PHASE_6_CHAMPION:
        cfg.entropyScale = 0.01f;
        cfg.policyLR = 1e-4f;
        cfg.criticLR = 1e-4f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.5f;
        cfg.useAerialTraining = true;
        cfg.aerialTrainingChance = 0.15f;  // 15% aerial training
        break;

    case TrainingPhase::PHASE_7_GC:
        cfg.entropyScale = 0.009f;
        cfg.policyLR = 8e-5f;
        cfg.criticLR = 8e-5f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.55f;
        cfg.useAerialTraining = true;
        cfg.aerialTrainingChance = 0.1f;  // 10% aerial training
        break;

    case TrainingPhase::PHASE_8_SSL:
        cfg.entropyScale = 0.008f;
        cfg.policyLR = 5e-5f;
        cfg.criticLR = 5e-5f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.6f;
        cfg.useAerialTraining = true;
        cfg.aerialTrainingChance = 0.05f;  // 5% aerial training
        break;

    case TrainingPhase::PHASE_9_PRO:
        cfg.entropyScale = 0.008f;          // Maintain some exploration
        cfg.policyLR = 3e-5f;               // Very fine tuning
        cfg.criticLR = 3e-5f;
        cfg.noTouchTimeout = 12.0f;          // Strictest
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.65f;
        cfg.useAerialTraining = true;
        cfg.aerialTrainingChance = 0.05f;  // 5% aerial training
        break;

    default:
        // Fallback to Bronze settings
        cfg.entropyScale = 0.025f;
        cfg.policyLR = 2.5e-4f;
        cfg.criticLR = 2.5e-4f;
        cfg.noTouchTimeout = 15.0f;
        cfg.useRandomState = false;
        cfg.randomStateChance = 0.0f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;
        break;
    }

    return cfg;
}

// ============================================================================
// REWARD CONFIGURATIONS BY PHASE
// ============================================================================

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
                WeightedReward(new VelocityPlayerToBallReward(), 3.0f),
                WeightedReward(new AirReward(), 0.25f), // don't forget how to jump

                WeightedReward(new ZeroSumReward(new StrongTouchReward(10, 70), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 0.4f),

                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 12.0f),

                WeightedReward(new PickupBoostReward(), 6.0f),

                // Reduced penalty for conceding to encourage aggression
                WeightedReward(new GoalReward(-0.7f), 250.0f)
        };

        // ========================================================================
        // PHASE 2: SILVER (1-3B steps)
        // Focus: Ground control, simple aerials, basic recoveries
        // ========================================================================
    case TrainingPhase::PHASE_2_SILVER:
        return std::vector<WeightedReward>{
            WeightedReward(new VelocityReward(), 0.6f),
                WeightedReward(new VelocityPlayerToBallReward(), 4.0f),
                WeightedReward(new FaceBallReward(), 0.3f),
                WeightedReward(new AirReward(), 0.25f),

                WeightedReward(new ZeroSumReward(new AirTouchReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new WallTouchReward(), 1.0f), 5.0f),
                WeightedReward(new ZeroSumReward(new JumpTouchReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new StrongTouchReward(15, 90), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 0.8f),

                //WeightedReward(new HalfFlipReward(), 3.0f),
                WeightedReward(new PowerSlideControlReward(), 0.1f),
                WeightedReward(new KickoffSpeedReward(), 6.0f),
                //WeightedReward(new WavedashReward(), 3.0f),

                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 20.0f),

                WeightedReward(new PickupBoostReward(), 6.0f),
                WeightedReward(new SaveBoostReward(), 0.15f),

                // Reduced penalty for conceding to encourage aggression
                WeightedReward(new GoalReward(-0.7f), 250.0f),
                WeightedReward(new ZeroSumReward(new ShotReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 10.0f)
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
                WeightedReward(new WavedashBasicReward(), 18.0f),
                //WeightedReward(new WallDashReward(), 15.0f),
                WeightedReward(new WavedashReward(), 3.0f),
                WeightedReward(new EnhancedSpeedFlipReward(), 12.0f),
                WeightedReward(new EmergencySaveCommitReward(), 2.0f),


                // Aerial Rewards
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 1.0f), 15.0f),
                //WeightedReward(new ZeroSumReward(new AirTouchReward(), 1.0f), 10.0f),
                //WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 1.0f), 4.0f), // replaces wall touch & air touch rewards
                WeightedReward(new ZeroSumReward(new TouchHeightWallScalarReward(), 1.0f), 10.0f), // replaces wall touch & air touch rewards
                WeightedReward(new ZeroSumReward(new WallShotReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new WallPopReward(), 1.0f), 5.0f),
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new UpsideDownAerialReward(), 1.0f), 30.0f), // new for helping with flip resets
                //WeightedReward(new ZeroSumReward(new CeilingShotTrainingReward(), 1.0f), 20.0f),
                //WeightedReward(new ZeroSumReward(new CeilingShotOnGoalReward(), 1.0f), 15.0f), //this reward sucks

                // Ball Touch Rewards (boosted for aggression)
                //WeightedReward(new ZeroSumReward(new StrongTouchReward(30, 120), 1.0f), 50.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 80.0f),
                WeightedReward(new ZeroSumReward(new OpenNetShotReward(), 1.0f), 30.0f),
                WeightedReward(new ZeroSumReward(new ShotQualityReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new LongShotReward(), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 1.0f), 35.0f),
                //WeightedReward(new ZeroSumReward(new WallPinchReward(), 1.0f), 10.0f),
                //WeightedReward(new ZeroSumReward(new GroundPinchReward(), 1.0f), 10.0f),
                //WeightedReward(new ZeroSumReward(new CeilingPinchReward(), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new BackboardDefenseReward(), 1.0f), 4.0f),
                //WeightedReward(new ZeroSumReward(new HookShotReward(), 1.0f), 10.0f),
                //WeightedReward(new ZeroSumReward(new CornerClearReward(), 1.0f), 5.0f),
                WeightedReward(new ZeroSumReward(new FiftyFiftyReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new BallRedirectReward(), 1.0f), 25.0f),
                WeightedReward(new LowBoostOppHalfFiftyPunish(), 8.0f), //punishment for losing 50/50s in opponent half with low boost
                //WeightedReward(new ZeroSumReward(new EnhancedDoubleTapReward(), 1.0f), 25.0f), //messing up air dribbles

                // Outplay Rewards (boosted for aggressive playmaking)
                WeightedReward(new ZeroSumReward(new FlickReward(), 1.0f), 4.0f),
                WeightedReward(new ZeroSumReward(new AdvancedFlickReward(), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new FlickShotReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new FlickOnGoalReward(), 1.0f), 40.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 1.0f), 40.0f),
                WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 1.0f), 1.5f),
                WeightedReward(new ZeroSumReward(new FakeChallengeDefenseReward(), 1.0f), 2.0f),  // Reduced defense fake
                WeightedReward(new ZeroSumReward(new DemoBumpOutplayReward(), 0.5f), 40.0f),
                //WeightedReward(new ZeroSumReward(new FlipResetReward(), 0.5f), 100.0f),
                //WeightedReward(new ZeroSumReward(new FlipResetDetectionReward(), 0.5f), 75.0f),

                // Possession and Dribbling (offensive focus)
                WeightedReward(new ZeroSumReward(new DribbleReward(), 1.0f), 3.0f),
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new BallControlAngleReward(), 1.0f), 0.1f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.5f),  // Reduced possession reward
                WeightedReward(new ZeroSumReward(new DefensivePositionReward(), 1.0f), 2.5f),  // Reduced defensive positioning
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 1.0f), 6.0f),
                WeightedReward(new ZeroSumReward(new CatchBallReward(), 1.0f), 6.0f),
                WeightedReward(new ZeroSumReward(new DribbleSetupReward(), 1.0f), 7.5f),
                WeightedReward(new ZeroSumReward(new AirDribbleAccuracyReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new AirDribbleBumpReward(), 1.0f), 40.0f),
                WeightedReward(new ZeroSumReward(new LowBoostDribblePunishment(), 1.0f), 5.0f),
				//WeightedReward(new ZeroSumReward(new AirDribbleReward(), 1.0f), 5.0f), //using this reward with HighBallTouchReward is excellent at teaching good air drivble basics, but causes too much overfitting
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 1.0f), 2.0f),
                WeightedReward(new ZeroSumReward(new OrbitBallPunishment(), 1.0f), 5.0f),

                // Boosted ball-to-goal velocity for offensive pressure
                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 20.0f),

                // Confidence and Hesitation Rewards/punishments
                WeightedReward(new HesitationPunishment(800.0f, 1500.0f, 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new ChallengeCommitmentReward(1200.0f, 2000.0f), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new FirstToBallReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new ImmediateShotReward(0.5f), 1.0f), 30.0f),
                WeightedReward(new ExcessiveShadowPunishment(3.0f, 2500.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new AerialCommitmentReward(400.0f, 1.0f), 1.0f), 15.0f),
                WeightedReward(new MomentumMaintenanceReward(1700.0f, 0.4f), 10.0f),
                WeightedReward(new ZeroSumReward(new FiftyAggressionReward(), 1.0f), 20.0f),

                // Kickoff Rewards
                WeightedReward(new ZeroSumReward(new KickoffSpeedReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new KickoffFormReward(), 1.0f), 24.0f),
                WeightedReward(new ZeroSumReward(new KickoffReward(), 1.0f), 22.0f),
                WeightedReward(new ZeroSumReward(new KickoffFirstTouchReward(), 1.0f), 10.0f),
                //WeightedReward(new FlipDuringKickoffReward(), 5.0f),
				//WeightedReward(new ZeroSumReward(new KickoffContactPunishmentReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new KickoffBallPositionReward(), 1.0f), 5.0f),
                WeightedReward(new ZeroSumReward(new EnhancedKickoffReward(), 1.0f), 10.0f),

                // Boost Management Rewards
                //WeightedReward(new AdvancedBoostManagementReward(), 10.0f),
                WeightedReward(new PostKickoffMidBoostReward(), 15.0f),
                WeightedReward(new DefensiveRotationBoostReward(), 11.0f),
                WeightedReward(new PickupBoostReward(), 25.0f),
                WeightedReward(new BigBoostPadCenterReward(), 25.0f),
                WeightedReward(new BoostDuringFlipReward(), 7.0f),
                WeightedReward(new BoostPathReward(), 12.0f),
                WeightedReward(new SmallPadCollectionReward(), 1.5f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 1.0f), 35.0f),
                WeightedReward(new ZeroSumReward(new BoostStarvationReward(), 1.0f), 3.0f),
                //WeightedReward(new ZeroSumReward(new BoostWastePenaltyReward(), 1.0f), 10.0f),
                WeightedReward(new DribbleBoostInefficencyPunish(), 3.5f),
                WeightedReward(new SaveBoostReward(), 0.55f),
                WeightedReward(new SupersonicBoostPenalty(), 11.0f),
                WeightedReward(new SupersonicReward(), 12.0f),

                //Combat Rewards
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 30.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 30.0f),
                WeightedReward(new ZeroSumReward(new DemoOffenseReward(), 0.5f), 45.0f),
                WeightedReward(new ZeroSumReward(new AirBumpDemoReward(), 0.5f), 75.0f),
                WeightedReward(new ZeroSumReward(new DemoChasingReward(), 0.5f), 0.03f),

                //General Event Rewards
                // Reduced penalty for conceding to encourage aggression (Phase 4 - Platinum)
                WeightedReward(new SpeedScaledGoalReward(-1.0f), 1000.0f),
                WeightedReward( // Composite shot reward focusing on speed and accuracy. we get 1.5x the reward for a perfect shot at high speed
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
                //WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 40.0f),
                WeightedReward(new ZeroSumReward(new SpeedScaledSaveReward(), 1.0f), 60.0f)
        };

        // ========================================================================
        // PHASE 5: DIAMOND (8-12B steps)
        // Focus: Flicks, dribbles, advanced aerials, shot accuracy
        // ========================================================================
    case TrainingPhase::PHASE_5_DIAMOND:
        return std::vector<WeightedReward>{
                // Basic Mechanics & Movement
                WeightedReward(new AirReward(), 0.1f),
                WeightedReward(new DemoAwarenessReward(), 3.5f),
                WeightedReward(new HalfFlipReward(), 2.5f),
                WeightedReward(new PowerSlideControlReward(), 3.0f),
                WeightedReward(new AdvancedRecoveryReward(), 0.05f),
                WeightedReward(new WavedashReward(), 15.0f),
                WeightedReward(new EnhancedSpeedFlipReward(), 4.0f),
                WeightedReward(new LandingQualityReward(), 0.2f),
                WeightedReward(new SupersonicBoostPenalty(), 3.0f),
                WeightedReward(new SupersonicReward(), 1.0f),

                // Aerial Rewards
                WeightedReward(new ZeroSumReward(new ReverseAerialReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new AerialFirstTouchReward(), 1.0f), 16.0f),
                WeightedReward(new ZeroSumReward(new AirTouchReward(), 1.0f), 16.0f),
                WeightedReward(new ZeroSumReward(new HighBallTouchReward(), 1.0f), 21.0f), // replaces wall touch & air touch rewards
                WeightedReward(new ZeroSumReward(new TouchHeightWallScalarReward(), 1.0f), 21.0f), // replaces wall touch & air touch rewards
                WeightedReward(new ZeroSumReward(new WallShotReward(), 1.0f), 18.0f),
                WeightedReward(new ZeroSumReward(new WallPopReward(), 1.0f), 8.0f),
                WeightedReward(new ZeroSumReward(new CeilingShotTrainingReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new CeilingShotOnGoalReward(), 1.0f), 15.0f),

                // Ball Touch Rewards  
                WeightedReward(new ZeroSumReward(new StrongTouchReward(40, 140), 1.0f), 25.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 40.0f),
                WeightedReward(new ZeroSumReward(new BackboardReboundReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new BackboardDefenseReward(), 1.0f), 5.5f),
                WeightedReward(new ZeroSumReward(new OpenNetShotReward(), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new HookShotReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new ChipShotReward(), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new ShotQualityReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new CornerClearReward(), 1.0f), 5.0f),
                WeightedReward(new ZeroSumReward(new FiftyFiftyReward(), 1.0f), 7.0f),
                WeightedReward(new LowBoostOppHalfFiftyPunish(), 10.0f), //punishment for losing 50/50s in opponent half with low boost
                WeightedReward(new ZeroSumReward(new ShadowDefenseVsPossessionReward(), 1.0f), 3.0f),

                // Outplay Rewards
                WeightedReward(new ZeroSumReward(new FlickOutplayReward(), 1.0f), 1.2f),
                WeightedReward(new ZeroSumReward(new FlickReward(), 1.0f), 2.5f),
                //WeightedReward(new ZeroSumReward(new FlickExplorationReward(), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new FlickOnGoalReward(), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new CutOutplayReward(), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new FakeChallengeOffenseReward(), 1.0f), 2.0f),
                WeightedReward(new ZeroSumReward(new FakeChallengeDefenseReward(), 1.0f), 5.0f),

                // Possession and Dribbling
                WeightedReward(new ZeroSumReward(new DribbleReward(), 1.0f), 1.0f),
                WeightedReward(new ZeroSumReward(new DribbleMasteryReward(), 1.0f), 4.5f),
                WeightedReward(new ZeroSumReward(new TimeToBalPossessionReward(), 1.0f), 2.00f),
                WeightedReward(new ZeroSumReward(new DefensivePositionReward(), 1.0f), 3.0f),
                WeightedReward(new ZeroSumReward(new CatchBallOnCarReward(), 1.0f), 3.0f),
                //WeightedReward(new ZeroSumReward(new AirDribbleReward(), 1.0f), 18.0f),
                WeightedReward(new ZeroSumReward(new AirDribbleOpportunityAwarenessReward(), 1.0f), 0.8f),
                WeightedReward(new ZeroSumReward(new AntiDribblingReward(), 1.0f), 2.0f),
                WeightedReward(new ZeroSumReward(new OrbitBallPunishment(), 1.0f), 3.0f),
                WeightedReward(new ZeroSumReward(new LowBoostDribblePunishment(), 1.0f), 1.0f),

                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 18.0f),

                // Kickoff Rewards
                WeightedReward(new ZeroSumReward(new KickoffFormReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new KickoffSpeedReward(), 1.0f), 10.0f),
                WeightedReward(new ZeroSumReward(new KickoffFirstTouchReward(), 1.0f), 5.0f),

                // Boost Rewards
                WeightedReward(new PickupBoostReward(), 8.0f),
                WeightedReward(new BoostPathReward(), 2.0f),
                WeightedReward(new SmallPadCollectionReward(), 0.5f),
                WeightedReward(new ZeroSumReward(new BoostStealReward(), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new BoostOnOffenseReward(), 1.0f), 5.0f),
                WeightedReward(new SaveBoostReward(), 0.8f),
                WeightedReward(new BoostEfficiencyReward(), 2.0f),

                //Combat Rewards
                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 40.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new DemoOffenseReward(), 0.5f), 65.0f),
                WeightedReward(new ZeroSumReward(new AirBumpDemoReward(), 0.5f), 60.0f),


                //General Event Rewards
                // Reduced penalty for conceding to encourage aggression (Phase 5 - Diamond)
                WeightedReward(new SpeedScaledGoalReward(-0.6f), 950.0f),
				WeightedReward( // Composite shot reward focusing on speed and accuracy. we get 1.5x the reward for a perfect shot at high speed
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
                    43.0f
                ),
                WeightedReward(new ZeroSumReward(new ShotOnGoalReward(), 1.0f), 21.0f),
                WeightedReward(new ZeroSumReward(new AssistReward(), 1.0f), 40.0f),
                WeightedReward(new ZeroSumReward(new SpeedScaledSaveReward(), 1.0f), 63.0f)
        };

        // ========================================================================
        // PHASE 6: CHAMPION (12-20B steps)
        // Focus: Double taps, flip resets, advanced mechanics
        // ========================================================================
    case TrainingPhase::PHASE_6_CHAMPION:
        return std::vector<WeightedReward>{
            WeightedReward(new ZeroSumReward(new AirTouchReward(400.f), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new StrongTouchReward(35, 130), 1.0f), 22.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 18.0f),

                WeightedReward(new FlickExplorationReward(), 20.0f),
                WeightedReward(new ChipShotReward(), 12.0f),
                WeightedReward(new EnhancedDoubleTapReward(), 30.0f),
                WeightedReward(new DribbleMasteryReward(), 10.0f),
                WeightedReward(new GroundToAirDribbleReward(), 18.0f),
                WeightedReward(new FlipResetDetectionReward(), 35.0f),
                WeightedReward(new PassingPlayReward(), 15.0f),
                WeightedReward(new RedirectReward(), 20.0f),
                WeightedReward(new WallPinchReward(), 25.0f),

                WeightedReward(new CeilingShotTrainingReward(), 15.0f),
                WeightedReward(new FlickReward(), 12.0f),
                //WeightedReward(new AirDribbleReward(), 15.0f),
                WeightedReward(new BackboardDefenseReward(), 12.0f),

                WeightedReward(new EnhancedSpeedFlipReward(), 20.0f),
                WeightedReward(new EnhancedMenaFlipReward(), 15.0f),
                WeightedReward(new EnhancedWallDashReward(), 12.0f),
                WeightedReward(new EnhancedZapDashReward(), 10.0f),

                WeightedReward(new BoostStealReward(), 8.0f),
                WeightedReward(new ReverseAerialReward(), 15.0f),

                WeightedReward(new TimeToBalPossessionReward(), 4.0f),

                WeightedReward(new ZeroSumReward(new LiuDistanceReward(), 1.0f), 3.5f),
                WeightedReward(new ZeroSumReward(new DefensivePositionReward(), 1.0f), 3.5f),
                WeightedReward(new FieldSpacingReward(), 4.0f),

                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 9.0f),

                WeightedReward(new PickupBoostReward(), 3.0f),
                WeightedReward(new SaveBoostReward(), 0.35f),

                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 15.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 40.0f),

                // Reduced penalty for conceding to encourage aggression (Phase 6 - Champion)
                WeightedReward(new GoalReward(-0.55f), 450.0f),
                WeightedReward(new ZeroSumReward(new ShotReward(), 1.0f), 40.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 50.0f)
        };

        // ========================================================================
        // PHASE 7: GRAND CHAMPION (20-40B steps)
        // Focus: Speed, consistency, pressure play
        // ========================================================================
    case TrainingPhase::PHASE_7_GC:
        return std::vector<WeightedReward>{
            WeightedReward(new ZeroSumReward(new AirTouchReward(500.f), 1.0f), 18.0f),
                WeightedReward(new ZeroSumReward(new StrongTouchReward(40, 140), 1.0f), 20.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 16.0f),

                WeightedReward(new BounceDribbleReward(), 15.0f),
                //WeightedReward(new AirDribbleReward(), 25.0f),
                WeightedReward(new AirBumpDemoReward(), 60.0f),
                WeightedReward(new CeilingGroundPinchReward(), 30.0f),
                WeightedReward(new MustyVariationReward(), 35.0f),
                WeightedReward(new ConsistencyReward(), 8.0f),

                WeightedReward(new EnhancedDoubleTapReward(), 25.0f),
                WeightedReward(new FlipResetDetectionReward(), 30.0f),
                WeightedReward(new FlickExplorationReward(), 18.0f),
                WeightedReward(new GroundToAirDribbleReward(), 15.0f),
                WeightedReward(new RedirectReward(), 18.0f),

                WeightedReward(new EnhancedSpeedFlipReward(), 22.0f),
                WeightedReward(new EnhancedMenaFlipReward(), 18.0f),
                WeightedReward(new EnhancedWallDashReward(), 15.0f),
                WeightedReward(new EnhancedZapDashReward(), 12.0f),

                WeightedReward(new BoostStealReward(), 10.0f),
                WeightedReward(new ReverseAerialReward(), 18.0f),

                WeightedReward(new TimeToBalPossessionReward(), 5.0f),
                WeightedReward(new MultiAgentPossessionReward(), 4.0f),

                WeightedReward(new ZeroSumReward(new LiuDistanceReward(), 1.0f), 4.0f),
                WeightedReward(new ZeroSumReward(new DefensivePositionReward(), 1.0f), 4.0f),
                WeightedReward(new FieldSpacingReward(), 5.0f),

                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 10.0f),

                WeightedReward(new PickupBoostReward(), 2.5f),
                WeightedReward(new SaveBoostReward(), 0.4f),

                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 18.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 50.0f),

                // Reduced penalty for conceding to encourage aggression (Phase 7 - GC)
                WeightedReward(new GoalReward(-0.5f), 500.0f),
                WeightedReward(new ZeroSumReward(new ShotReward(), 1.0f), 45.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 55.0f)
        };

        // ========================================================================
        // PHASE 8: SSL (40-80B steps)
        // Focus: Polish, optimization, full game mastery
        // ========================================================================
    case TrainingPhase::PHASE_8_SSL:
        return std::vector<WeightedReward>{
            WeightedReward(new ZeroSumReward(new AirTouchReward(600.f), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new StrongTouchReward(45, 150), 1.0f), 18.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 14.0f),

                WeightedReward(new BounceDribbleReward(), 12.0f),
                //WeightedReward(new AirDribbleReward(), 20.0f),
                WeightedReward(new AirBumpDemoReward(), 60.0f),
                WeightedReward(new CeilingGroundPinchReward(), 25.0f),
                WeightedReward(new MustyVariationReward(), 30.0f),
                WeightedReward(new ConsistencyReward(), 10.0f),
                WeightedReward(new EnhancedDoubleTapReward(), 22.0f),
                WeightedReward(new FlipResetDetectionReward(), 28.0f),
                WeightedReward(new FlickExplorationReward(), 16.0f),
                WeightedReward(new GroundToAirDribbleReward(), 14.0f),
                WeightedReward(new RedirectReward(), 16.0f),
                WeightedReward(new WallPinchReward(), 20.0f),

                WeightedReward(new EnhancedSpeedFlipReward(), 25.0f),
                WeightedReward(new EnhancedMenaFlipReward(), 20.0f),
                WeightedReward(new EnhancedWallDashReward(), 16.0f),
                WeightedReward(new EnhancedZapDashReward(), 14.0f),

                WeightedReward(new BoostStealReward(), 12.0f),
                WeightedReward(new ReverseAerialReward(), 20.0f),

                WeightedReward(new TimeToBalPossessionReward(), 6.0f),
                WeightedReward(new MultiAgentPossessionReward(), 5.0f),

                WeightedReward(new ZeroSumReward(new LiuDistanceReward(), 1.0f), 5.0f),
                WeightedReward(new ZeroSumReward(new DefensivePositionReward(), 1.0f), 5.0f),
                WeightedReward(new FieldSpacingReward(), 6.0f),

                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 12.0f),

                WeightedReward(new PickupBoostReward(), 2.0f),
                WeightedReward(new SaveBoostReward(), 0.45f),

                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 20.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 60.0f),

                // Reduced penalty for conceding to encourage aggression (Phase 8 - SSL)
                WeightedReward(new GoalReward(-0.45f), 600.0f),
                WeightedReward(new ZeroSumReward(new ShotReward(), 1.0f), 50.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 60.0f)
        };

        // ========================================================================
        // PHASE 9: PRO ESPORTS (80B+ steps)
        // Focus: Maximum optimization, pro-level refinement
        // ========================================================================
    case TrainingPhase::PHASE_9_PRO:
        return std::vector<WeightedReward>{
            WeightedReward(new ZeroSumReward(new AirTouchReward(700.f), 1.0f), 12.0f),
                WeightedReward(new ZeroSumReward(new StrongTouchReward(50, 160), 1.0f), 15.0f),
                WeightedReward(new ZeroSumReward(new TouchAccelReward(), 1.0f), 12.0f),

                WeightedReward(new BounceDribbleReward(), 10.0f),
                //WeightedReward(new AirDribbleReward(), 18.0f),
                WeightedReward(new AirBumpDemoReward(), 60.0f),
                WeightedReward(new CeilingGroundPinchReward(), 22.0f),
                WeightedReward(new MustyVariationReward(), 28.0f),
                WeightedReward(new ConsistencyReward(), 12.0f),
                WeightedReward(new EnhancedDoubleTapReward(), 20.0f),
                WeightedReward(new FlipResetDetectionReward(), 25.0f),
                WeightedReward(new FlickExplorationReward(), 14.0f),
                WeightedReward(new GroundToAirDribbleReward(), 12.0f),
                WeightedReward(new RedirectReward(), 14.0f),
                WeightedReward(new WallPinchReward(), 18.0f),

                WeightedReward(new EnhancedSpeedFlipReward(), 28.0f),
                WeightedReward(new EnhancedMenaFlipReward(), 22.0f),
                WeightedReward(new EnhancedWallDashReward(), 18.0f),
                WeightedReward(new EnhancedZapDashReward(), 16.0f),

                WeightedReward(new BoostStealReward(), 15.0f),
                WeightedReward(new ReverseAerialReward(), 22.0f),

                WeightedReward(new TimeToBalPossessionReward(), 8.0f),
                WeightedReward(new MultiAgentPossessionReward(), 6.0f),

                WeightedReward(new ZeroSumReward(new LiuDistanceReward(), 1.0f), 6.0f),
                WeightedReward(new ZeroSumReward(new DefensivePositionReward(), 1.0f), 6.0f),
                WeightedReward(new FieldSpacingReward(), 8.0f),

                WeightedReward(new ZeroSumReward(new VelocityBallToGoalReward(), 1.0f), 14.0f),

                WeightedReward(new PickupBoostReward(), 1.5f),
                WeightedReward(new SaveBoostReward(), 0.5f),

                WeightedReward(new ZeroSumReward(new BumpReward(), 0.5f), 25.0f),
                WeightedReward(new ZeroSumReward(new DemoReward(), 0.5f), 70.0f),

                // Reduced penalty for conceding to encourage aggression (Phase 9 - PRO)
                WeightedReward(new GoalReward(-0.4f), 700.0f),
                WeightedReward(new ZeroSumReward(new ShotReward(), 1.0f), 55.0f),
                WeightedReward(new ZeroSumReward(new SaveReward(), 1.0f), 65.0f)
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

// ============================================================================
// ENVIRONMENT CREATION
// ============================================================================

EnvCreateResult EnvCreateFunc(int index) {
    std::vector<WeightedReward> rewards = GetPhasedRewards();
    PhaseConfig phaseCfg = GetPhaseConfig();

    std::vector<TerminalCondition*> terminalConditions = {
        new NoTouchCondition(phaseCfg.noTouchTimeout),
        new GoalScoreCondition()
    };

    int playersPerTeam = 1;
    auto arena = Arena::Create(GameMode::SOCCAR);
    for (int i = 0; i < playersPerTeam; i++) {
        arena->AddCar(Team::BLUE);
        arena->AddCar(Team::ORANGE);
    }

    EnvCreateResult result = {};
    result.actionParser = new DefaultAction();
    result.obsBuilder = new AdvancedObs();

    // Create state setters based on phase configuration
    std::vector<std::pair<StateSetter*, float>> stateSetters;

    // Always include kickoff state as the base
    float kickoffWeight = 1.0f;

    // Calculate weights based on phase configuration
    if (phaseCfg.useRandomState) {
        kickoffWeight *= (1.0f - phaseCfg.randomStateChance);
    }
    if (phaseCfg.useAerialTraining) {
        kickoffWeight *= (1.0f - phaseCfg.aerialTrainingChance);
    }

    stateSetters.push_back({ new KickoffState(), kickoffWeight });

    // Add random state if enabled
    if (phaseCfg.useRandomState) {
        float randomWeight = phaseCfg.randomStateChance;
        if (phaseCfg.useAerialTraining) {
            randomWeight *= (1.0f - phaseCfg.aerialTrainingChance);
        }
        stateSetters.push_back({ new RandomState(), randomWeight });
    }

    // Add aerial training state if enabled
    if (phaseCfg.useAerialTraining) {
        stateSetters.push_back({ new AerialTrainingState(), phaseCfg.aerialTrainingChance });
    }

    // Use CombinedState to randomly choose between the state setters
    result.stateSetter = new CombinedState(stateSetters);

    result.terminalConditions = terminalConditions;
    result.rewards = rewards;
    result.arena = arena;

    return result;
}

// ============================================================================
// METRICS CALLBACK
// ============================================================================

// 2025-12-16 | Rev 1.1 | NJH
// - Fix: pull/reset reward metrics ONCE per StepCallback (not per-state) to avoid under/over counting.
// - Keep expensive per-player metrics sampled as before.
void StepCallback(Learner* learner, const std::vector<GameState>& states, Report& report) {
    bool doExpensiveMetrics = (rand() % 16) == 0;

    //static RLGC::DribbleFlickEpisodeMetrics dfMetrics;

    // Call once per step for all arenas
    //dfMetrics.ProcessBatch(states, report, doExpensiveMetrics);

    // Update aerial training gravity (if any arenas are using it)
    AerialTrainingState::UpdateAll(states);

    // -------------------------
    // Reward-owned metrics (pull ONCE per callback)
    // -------------------------
    {
        auto adm = RLGC::AirDribbleReward::GetAndResetAirDribbleMetrics();
        if (adm.attempts > 0) {
            float onTargetRate = float(adm.onTarget) / float(RS_MAX(adm.attempts, 1));
            float avgReward = float(adm.rewardSum / double(adm.attempts));
            report.AddAvg("AirDribble/Attempts", (float)adm.attempts);
            report.AddAvg("AirDribble/OnTargetRate", onTargetRate);
            report.AddAvg("AirDribble/AvgReward", avgReward);
        }
    }

    {
        auto m = KickoffFirstTouchReward::GetAndResetKickoffFirstTouchMetrics();
        report.AddAvg("KO/FirstTouch/Kickoffs", (float)m.kickoffs);
        report.AddAvg("KO/FirstTouch/Wins", (float)m.firstWins);
        report.AddAvg("KO/FirstTouch/LossTouched", (float)m.firstLoss);
        report.AddAvg("KO/FirstTouch/NoTouch", (float)m.noTouch);
        report.AddAvg("KO/FirstTouch/RewardSum", (float)m.rewardSum);
    }

    {
        auto m = KickoffFormReward::GetAndResetKickoffFormMetrics();
        report.AddAvg("KO/Form/Kickoffs", (float)m.kickoffs);
        report.AddAvg("KO/Form/earlyFlips", (float)m.earlyFlips);
        report.AddAvg("KO/Form/centerHits", (float)m.centerHits);
        report.AddAvg("KO/Form/powerfulHits", (float)m.powerfulHits);
        report.AddAvg("KO/Form/followThroughs", (float)m.followThroughs);
        report.AddAvg("KO/Form/RewardSum", (float)m.rewardSum);
    }

    {
        auto m = WavedashReward::GetAndResetWavedashMetrics();
        report.AddAvg("Mech/Wavedash/LandFlip", (float)m.landFlip);
        report.AddAvg("Mech/Wavedash/GoodGeom", (float)m.goodGeom);
        report.AddAvg("Mech/Wavedash/Events", (float)m.events);
        report.AddAvg("Mech/Wavedash/RewardSum", (float)m.rewardSum);
    }

    // -------------------------
    // Per-arena/per-player metrics
    // -------------------------
    for (auto& state : states) {
        if (doExpensiveMetrics) {
            for (auto& player : state.players) {
                report.AddAvg("Player/In Air Ratio", !player.isOnGround);
                report.AddAvg("Player/Ball Touch Ratio", player.ballTouchedStep);
                report.AddAvg("Player/Demoed Ratio", player.isDemoed);
                report.AddAvg("Player/Speed", player.vel.Length());

                Vec dirToBall = (state.ball.pos - player.pos).Normalized();
                report.AddAvg("Player/Speed Towards Ball", RS_MAX(0, player.vel.Dot(dirToBall)));
                report.AddAvg("Player/Boost", player.boost);

                if (player.ballTouchedStep) {
                    report.AddAvg("Player/Touch Height", state.ball.pos.z);
                }

                bool supersonic = player.vel.Length() > 2200.0f;
                report.AddAvg("Player/Supersonic Ratio", supersonic);

                report.AddAvg("Player/Flipping Ratio", player.isFlipping);
            }
        }

        if (state.goalScored) {
            report.AddAvg("Game/Goal Speed", state.ball.vel.Length());
        }
    }
}



// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    RocketSim::Init("C:\\Projects\\RLGymBot_v3\\collision_meshes");

    // Parse command-line arguments for render mode
    bool renderMode = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--render") {
            renderMode = true;
            std::cout << "[CONFIG] Render mode enabled\n";
            break;
        }
    }

    // Toggle here: change to KickoffMode::STOCHASTIC to disable hardcoded kickoffs, KickoffMode::HARDCODED to enable them
    const RLGC::KickoffMode KICKOFF_MODE = RLGC::KickoffMode::STOCHASTIC;

    std::cout << "[CONFIG] Kickoff mode: " <<
        (KICKOFF_MODE == RLGC::KickoffMode::HARDCODED ? "HARDCODED" : "STOCHASTIC") << std::endl;

    PhaseConfig phaseCfg = GetPhaseConfig();
    LearnerConfig cfg = {};

    // === CHECKPOINTS ===
    // Use absolute path to ensure it works regardless of working directory
    cfg.checkpointFolder = "C:\\Projects\\RLGymBot_v3\\GigaLearnBot\\build\\checkpoints";
    cfg.tsPerSave = 100'000'000ULL;      // Save every 100M steps
    cfg.checkpointsToKeep = 128;

    // === POLICY VERSIONS ===
    cfg.tsPerVersion = 200'000'000ULL;   // New version every 200M steps
    cfg.maxOldVersions = 64;

    cfg.trainAgainstOldVersions = true;
    cfg.trainAgainstOldChance = 0.3f;

    // === SKILL TRACKER ===
    cfg.skillTracker.enabled = true;
    cfg.skillTracker.numArenas = 16;
    cfg.skillTracker.simTime = 45.f;
    cfg.skillTracker.maxSimTime = 240.f;
    cfg.skillTracker.updateInterval = 16;
    cfg.skillTracker.ratingInc = 5.f;
    cfg.skillTracker.initialRating = 0.f;
    cfg.skillTracker.deterministic = false;

    // === MuZero Implementation ===
    cfg.reanalysis.enabled = true;
    cfg.reanalysis.mctsSimulations = 4;           // Very low for testing
    cfg.reanalysis.reanalysisInterval = 50;        // Every 50 iterations instead of 100
    cfg.reanalysis.samplesPerReanalysis = 100;    // Small batch
    cfg.reanalysis.minBufferSize = 1000;          // Lower threshold


    // === DEVICE ===
    cfg.deviceType = LearnerDeviceType::GPU_CUDA;

    // === TIMING ===
    cfg.tickSkip = 8;
    cfg.actionDelay = cfg.tickSkip - 1;
    cfg.numGames = 310;
    cfg.randomSeed = 123;

    // === PPO CONFIG ===
    int tsPerItr = 300'000;
    cfg.ppo.tsPerItr = tsPerItr;
    cfg.ppo.batchSize = tsPerItr;
    cfg.ppo.miniBatchSize = 25'000;
    cfg.ppo.epochs = 1;

    // Phase-specific learning parameters
    cfg.ppo.entropyScale = phaseCfg.entropyScale;
    cfg.ppo.gaeGamma = 0.9925;
    cfg.ppo.policyLR = phaseCfg.policyLR;
    cfg.ppo.criticLR = phaseCfg.criticLR;

    // === NETWORK SIZE (CONSTANT FOR ALL PHASES) ===
    cfg.ppo.sharedHead.layerSizes = { 512, 512 };
    cfg.ppo.policy.layerSizes = { 512, 512, 512 };
    cfg.ppo.critic.layerSizes = { 512, 512, 512 };

    auto optim = ModelOptimType::ADAM;
    cfg.ppo.policy.optimType = optim;
    cfg.ppo.critic.optimType = optim;
    cfg.ppo.sharedHead.optimType = optim;

    auto activation = ModelActivationType::RELU;
    cfg.ppo.policy.activationType = activation;
    cfg.ppo.critic.activationType = activation;
    cfg.ppo.sharedHead.activationType = activation;

    bool addLayerNorm = true;
    cfg.ppo.policy.addLayerNorm = addLayerNorm;
    cfg.ppo.critic.addLayerNorm = addLayerNorm;
    cfg.ppo.sharedHead.addLayerNorm = addLayerNorm;

    // Configure render mode settings
    if (renderMode) {
        cfg.sendMetrics = false;           // No need to log metrics from render instance
        cfg.renderMode = false;             // Enable visualization
        cfg.ppo.deterministic = false;      // Use best actions (no exploration)
        std::cout << "[CONFIG] Rendering with deterministic policy\n";
    } else {
        cfg.sendMetrics = true;
        cfg.renderMode = false;
    }

    cfg.renderTimeScale = 3.f;
    cfg.kickoffMode = KICKOFF_MODE;  // SET HERE

    // Print current phase info
    const char* phaseNames[] = {    
        "PHASE 1: BRONZE (0-1B)",
        "PHASE 2: SILVER (1-3B)",
        "PHASE 3: GOLD (3-5B)",
        "PHASE 4: PLATINUM (5-40B)",
        "PHASE 5: DIAMOND (40-80B)",
        "PHASE 6: CHAMPION (80-120B)",
        "PHASE 7: GRAND CHAMPION (120-160B)",
        "PHASE 8: SSL (160-200B)",
        "PHASE 9: PRO ESPORTS (200B+)"
    };
    printf("\n=== TRAINING PHASE: %s ===\n", phaseNames[(int)CURRENT_PHASE]);
    printf("Entropy: %.4f, Policy LR: %.2e\n", phaseCfg.entropyScale, phaseCfg.policyLR);
    printf("Network: Shared[512, 512] Policy[512,512,512] Critic[512,512,512]\n\n");

    Learner* learner = new Learner(EnvCreateFunc, cfg, StepCallback);
    learner->Start();

    return EXIT_SUCCESS;
}