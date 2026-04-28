#include "RewardFactory.h"
#include "RewardConfig.h"

// All reward headers are already included via RewardConfig.h
// which includes all necessary reward definitions

namespace RLGC {

std::map<std::string, RewardFactory::RewardConstructor>& RewardFactory::GetRegistry() {
    static std::map<std::string, RewardConstructor> registry;
    return registry;
}

bool& RewardFactory::IsInitialized() {
    static bool initialized = false;
    return initialized;
}

void RewardFactory::Initialize() {
    // Temporarily disabled while implementing support for parameterized rewards
    // TODO: Implement constructors for all reward types, including those with parameters
    if (IsInitialized()) {
        return;  // Already initialized
    }

    // Note: The following reward registrations are commented out because they
    // require constructor parameters that aren't easily represented in JSON.
    // This will be fixed once we enhance the factory to handle parameterized constructors.

    IsInitialized() = true;
    return;

    /*

    // Register all reward types
    // Common Rewards
    Register("VelocityReward", []() { return new VelocityReward(); });
    Register("FaceBallReward", []() { return new FaceBallReward(); });
    Register("AirReward", []() { return new AirReward(); });
    Register("VelocityPlayerToBallReward", []() { return new VelocityPlayerToBallReward(); });
    Register("TouchBallReward", []() { return new TouchBallReward(); });
    Register("VelocityBallToGoalReward", []() { return new VelocityBallToGoalReward(); });
    Register("PickupBoostReward", []() { return new PickupBoostReward(); });
    Register("GoalReward", []() { return new GoalReward(); });
    Register("GoalReward_withPenalty", []() { return new GoalReward(-1.0f); });

    // Strong Touch Rewards - Note: these take parameters, handled specially
    // We'll create a basic version; parameterized versions need special handling
    Register("StrongTouchReward", []() { return new StrongTouchReward(20, 100); });
    Register("TouchAccelReward", []() { return new TouchAccelReward(); });
    Register("JumpTouchReward", []() { return new JumpTouchReward(); });

    // Spacing
    Register("InstantSpacingReward", []() { return new InstantSpacingReward(RotationPhase::GOLD); });

    // Aerial Rewards
    Register("AirTouchReward", []() { return new AirTouchReward(); });
    Register("AerialFirstTouchReward", []() { return new AerialFirstTouchReward(); });
    Register("HighBallTouchReward", []() { return new HighBallTouchReward(); });
    Register("ReverseAerialReward", []() { return new ReverseAerialReward(); });
    Register("WallTouchReward", []() { return new WallTouchReward(); });
    Register("WallShotReward", []() { return new WallShotReward(); });
    Register("AirRollReward", []() { return new AirRollReward(); });
    Register("AirRollShotReward", []() { return new AirRollShotReward(); });
    Register("InvertedAerialReward", []() { return new InvertedAerialReward(); });
    Register("EarlyAerialReward", []() { return new EarlyAerialReward(); });
    Register("MultiAirTouchReward", []() { return new MultiAirTouchReward(); });
    Register("AerialCommitmentReward", []() { return new AerialCommitmentReward(); });
    Register("AerialOutplayReward", []() { return new AerialOutplayReward(); });
    Register("PitchControlAerialReward", []() { return new PitchControlAerialReward(); });

    // Recovery Rewards
    Register("HalfFlipReward", []() { return new HalfFlipReward(); });
    Register("WavedashBasicReward", []() { return new WavedashBasicReward(); });
    Register("WavedashReward", []() { return new WavedashReward(); });
    Register("WaveDashHeightReward", []() { return new WaveDashHeightReward(); });
    Register("ProperLandingReward", []() { return new ProperLandingReward(); });
    Register("AdvancedRecoveryReward", []() { return new AdvancedRecoveryReward(); });
    Register("BumpedFlipRecoveryReward", []() { return new BumpedFlipRecoveryReward(); });

    // Boost Management
    Register("SaveBoostReward", []() { return new SaveBoostReward(); });
    Register("SmallPadCollectionReward", []() { return new SmallPadCollectionReward(); });
    Register("BoostStealReward", []() { return new BoostStealReward(); });
    Register("BoostStarvationReward", []() { return new BoostStarvationReward(); });
    Register("PostKickoffMidBoostReward", []() { return new PostKickoffMidBoostReward(); });
    Register("BigBoostPadCenterReward", []() { return new BigBoostPadCenterReward(); });
    Register("DefensiveRotationBoostReward", []() { return new DefensiveRotationBoostReward(); });
    // BoostAwareRotationReward requires constructor parameters - skipping
    // Register("BoostAwareRotationReward", []() { return new BoostAwareRotationReward(); });
    // BoostPathReward requires constructor parameters - skipping
    // Register("BoostPathReward", []() { return new BoostPathReward(); });
    Register("BoostWastePenaltyReward", []() { return new BoostWastePenaltyReward(); });
    Register("LowBoostOffenseReward", []() { return new LowBoostOffenseReward(); });
    Register("SupersonicBoostPenalty", []() { return new SupersonicBoostPenalty(); });
    Register("SupportingPlayerBoostMaintenanceReward", []() { return new SupportingPlayerBoostMaintenanceReward(); });

    // Shooting & Shooting Rewards
    Register("HookShotReward", []() { return new HookShotReward(); });
    Register("DirectionChangeReward", []() { return new DirectionChangeReward(); });
    Register("CutOutplayReward", []() { return new CutOutplayReward(); });
    Register("ShotAccuracyInBoundsReward", []() { return new ShotAccuracyInBoundsReward(); });
    Register("ShotPlacementReward", []() { return new ShotPlacementReward(); });
    Register("ShotReward", []() { return new ShotReward(); });
    Register("SpeedScaledShotReward", []() { return new SpeedScaledShotReward(); });
    Register("CeilingShotReward", []() { return new CeilingShotReward(); });
    Register("DoomseeDisshShotReward", []() { return new DoomseeDisshShotReward(); });
    Register("FlickOutplayReward", []() { return new FlickOutplayReward(); });

    // Positioning & Possession
    Register("DefensivePositionReward", []() { return new DefensivePositionReward(); });
    Register("TimeToBalPossessionReward", []() { return new TimeToBalPossessionReward(); });
    Register("RetreatAfterTouchReward", []() { return new RetreatAfterTouchReward(RotationPhase::GOLD); });
    Register("DefensivePositioningChallengeReward", []() { return new DefensivePositioningChallengeReward(); });
    Register("PossessionControlReward", []() { return new PossessionControlReward(); });
    Register("GoalpostPositioningReward", []() { return new GoalpostPositioningReward(); });
    Register("OwnNetCenteringPenalty", []() { return new OwnNetCenteringPenalty(); });
    Register("ActiveSupportingPositionReward", []() { return new ActiveSupportingPositionReward(); });
    Register("ShadowDefenseVsPossessionReward", []() { return new ShadowDefenseVsPossessionReward(); });

    // Demo Rewards
    Register("DemoAwarenessReward", []() { return new DemoAwarenessReward(); });
    Register("BumpReward", []() { return new BumpReward(); });
    Register("DemoReward", []() { return new DemoReward(); });
    Register("ContextualDemoReward", []() { return new ContextualDemoReward(); });
    Register("SecondManOffensiveDemoReward", []() { return new SecondManOffensiveDemoReward(); });
    Register("SoloDefenderBumpReward", []() { return new SoloDefenderBumpReward(); });
    Register("DemoedPenalty", []() { return new DemoedPenalty(); });
    Register("BumpedPenalty", []() { return new BumpedPenalty(); });

    // Movement & Control
    Register("PowerSlideControlReward", []() { return new PowerSlideControlReward(); });
    Register("SupersonicReward", []() { return new SupersonicReward(); });
    Register("EnhancedSpeedFlipReward", []() { return new EnhancedSpeedFlipReward(); });
    Register("SpeedFlipKickoffReward", []() { return new SpeedFlipKickoffReward(); });
    Register("EnhancedVelocityBallToGoalReward", []() { return new EnhancedVelocityBallToGoalReward(); });
    Register("PlayerVelocityToOpponentReward", []() { return new PlayerVelocityToOpponentReward(); });
    Register("ClosestPlayerSpeedReward", []() { return new ClosestPlayerSpeedReward(); });

    // Flip Reset & Advanced Mechanics
    Register("FlipResetReward", []() { return new FlipResetReward(); });
    Register("SimpleFlipResetReward", []() { return new SimpleFlipResetReward(); });
    Register("FlipResetApproachReward", []() { return new FlipResetApproachReward(); });

    // Dribble & Ball Control
    Register("DribbleReward", []() { return new DribbleReward(); });
    Register("DribbleBumpReward", []() { return new DribbleBumpReward(); });
    Register("DribbleSetupReward", []() { return new DribbleSetupReward(); });
    Register("DribbleMasteryReward", []() { return new DribbleMasteryReward(); });
    Register("CatchBallOnCarReward", []() { return new CatchBallOnCarReward(); });

    // Air Dribble
    Register("AirDribbleSetupReward", []() { return new AirDribbleSetupReward(); });
    Register("UnifiedAirDribbleReward", []() { return new UnifiedAirDribbleReward(); });
    Register("AirDribbleBumpReward", []() { return new AirDribbleBumpReward(); });

    // Flick Rewards
    Register("AdvancedFlickReward", []() { return new AdvancedFlickReward(); });
    Register("CompositeFlickReward", []() { return new CompositeFlickReward(); });

    // Double Tap & Special Shots
    Register("UnifiedDoubleTapReward", []() { return new UnifiedDoubleTapReward(); });
    Register("BackboardReboundReward", []() { return new BackboardReboundReward(); });
    Register("BackboardDefenseReward", []() { return new BackboardDefenseReward(); });

    // Wall Play
    Register("WallDashReward", []() { return new WallDashReward(); });
    Register("WallToAerialReward", []() { return new WallToAerialReward(); });
    Register("WallPinchReward", []() { return new WallPinchReward(); });

    // Defense & Saves
    Register("SaveReward", []() { return new SaveReward(); });
    Register("EmergencySaveCommitReward", []() { return new EmergencySaveCommitReward(); });
    Register("BigClearReward", []() { return new BigClearReward(); });
    Register("CornerClearReward", []() { return new CornerClearReward(); });
    Register("GoalLineClearanceReward", []() { return new GoalLineClearanceReward(); });
    Register("EarlyDangerDetectionReward", []() { return new EarlyDangerDetectionReward(); });

    // Overcommitment Penalties
    // SimpleOvercommitmentPenalty requires constructor parameters - skipping
    // Register("SimpleOvercommitmentPenalty", []() { return new SimpleOvercommitmentPenalty(); });
    // AdvancedOvercommitmentPenalty requires constructor parameters - skipping
    // Register("AdvancedOvercommitmentPenalty", []() { return new AdvancedOvercommitmentPenalty(); });
    Register("AdvancedOpponentOvercommitmentReward", []() { return new AdvancedOpponentOvercommitmentReward(); });
    Register("DoubleCommitPenalty", []() { return new DoubleCommitPenalty(); });
    Register("KickoffDoubleCommitPenalty", []() { return new KickoffDoubleCommitPenalty(); });

    // Passing & Teamwork
    Register("AssistReward", []() { return new AssistReward(); });
    Register("ReceivingPassReward", []() { return new ReceivingPassReward(); });
    Register("PassingSetupReward", []() { return new PassingSetupReward(); });
    Register("PassingAccuracyReward", []() { return new PassingAccuracyReward(); });
    Register("PassingLaneNetworkReward", []() { return new PassingLaneNetworkReward(); });
    Register("HighPassReward", []() { return new HighPassReward(); });
    Register("BackPassReward", []() { return new BackPassReward(); });
    Register("ForwardPassReward", []() { return new ForwardPassReward(); });
    Register("InfieldPassReward", []() { return new InfieldPassReward(); });
    Register("DownfieldPassReward", []() { return new DownfieldPassReward(); });
    Register("OpponentPassInterceptionReward", []() { return new OpponentPassInterceptionReward(); });
    Register("PassToOpponentPenalty", []() { return new PassToOpponentPenalty(); });

    // Ball Redirect & Outplay
    Register("BallRedirectReward", []() { return new BallRedirectReward(); });
    Register("FirstToBallReward", []() { return new FirstToBallReward(); });
    Register("ChallengeCommitmentReward", []() { return new ChallengeCommitmentReward(); });
    Register("FakeChallengeOffenseReward", []() { return new FakeChallengeOffenseReward(); });
    Register("SpatialFakeReward", []() { return new SpatialFakeReward(); });

    // Defensive Readiness & Rotation
    Register("DefensiveReadinessReward", []() { return new DefensiveReadinessReward(); });
    Register("ProgressiveRotationReward", []() { return new ProgressiveRotationReward(); });

    // Kickoff
    Register("KickoffSpeedReward", []() { return new KickoffSpeedReward(); });
    Register("KickoffFlipReward", []() { return new KickoffFlipReward(); });
    Register("KickoffStrategyReward", []() { return new KickoffStrategyReward(); });
    Register("KickoffRandomizationReward", []() { return new KickoffRandomizationReward(); });
    Register("KickoffNoChaseTeamPenalty", []() { return new KickoffNoChaseTeamPenalty(); });
    Register("KickoffSupportBoostReward", []() { return new KickoffSupportBoostReward(); });
    Register("ClosestPlayerKickoffSideReward", []() { return new ClosestPlayerKickoffSideReward(); });

    // Option Control & Wrappers (special handling)
    Register("OptionControlReward", []() { return new OptionControlReward(); });
    Register("MathRewardWrapper", []() { return new MathRewardWrapper(); });
    Register("PassingRewardWrapper", []() { return new PassingRewardWrapper(); });
    Register("ScoringRewardWrapper", []() { return new ScoringRewardWrapper(); });

    // Goal Multiplier (special reward)
    Register("GoalMultiplierReward", []() { return new GoalMultiplierReward(); });

    // Misc
    Register("LowBoostOppHalfFiftyPunish", []() { return new LowBoostOppHalfFiftyPunish(); });
    Register("CornerPlayPenalty", []() { return new CornerPlayPenalty(); });
    Register("OwnGoalPenalty", []() { return new OwnGoalPenalty(); });
    Register("NumericalAdvantageAttackingReward", []() { return new NumericalAdvantageAttackingReward(); });
    Register("IntelligentWavedashReward", []() { return new IntelligentWavedashReward(); });
    Register("AirRollMultiplierReward", []() { return new AirRollMultiplierReward(); });

    // Note: ZeroSumReward is a wrapper and is handled specially in JSON configuration
    // (it wraps other rewards, so it's created dynamically in the JSON loader)
    // Therefore, we don't register it as a standalone reward

    IsInitialized() = true;
    */
}

Reward* RewardFactory::Create(const std::string& name) {
    if (!IsInitialized()) {
        throw std::runtime_error("RewardFactory not initialized. Call RewardFactory::Initialize() first.");
    }

    auto& registry = GetRegistry();
    auto it = registry.find(name);

    if (it == registry.end()) {
        throw std::runtime_error("Unknown reward type: '" + name + "'. Check reward_phases.json and RewardFactory registration.");
    }

    return it->second();
}

bool RewardFactory::Has(const std::string& name) {
    if (!IsInitialized()) {
        return false;
    }
    return GetRegistry().find(name) != GetRegistry().end();
}

std::vector<std::string> RewardFactory::GetRegisteredRewards() {
    std::vector<std::string> names;
    for (const auto& [name, _] : GetRegistry()) {
        names.push_back(name);
    }
    return names;
}

void RewardFactory::Register(const std::string& name, RewardConstructor constructor) {
    GetRegistry()[name] = constructor;
}

}  // namespace RLGC
