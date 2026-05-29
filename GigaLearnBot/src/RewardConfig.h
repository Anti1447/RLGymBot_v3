#pragma once

#include <GigaLearnCPP/Learner.h>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include "ConfigLoader.h"
#include "RewardFactory.h"
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/Rewards/AdvancedRewards.h>
#include <RLGymCPP/Rewards/ConfidenceRewards.h>
#include <RLGymCPP/Rewards/KickoffRewards.h>
#include <RLGymCPP/Rewards/BoostManagementRewards.h>
#include <RLGymCPP/Rewards/BoostCooperationRewards.h>
#include <RLGymCPP/Rewards/MultiModalRewards.h>
#include <RLGymCPP/Rewards/CompleteMechanicsRewards.h>
#include <RLGymCPP/Rewards/OutplayRewards.h>
#include <RLGymCPP/Rewards/FlipResetReward.h>
#include <RLGymCPP/Rewards/FlipResetApproachReward.h>
#include <RLGymCPP/Rewards/GodTierMovementRewards_Enhanced.h>
#include <RLGymCPP/Rewards/PossessionRewards.h>
#include <RLGymCPP/Rewards/MovementAndKickoffRewards_Enhanced.h>
#include <RLGymCPP/Rewards/RotationRewards.h>
#include <RLGymCPP/Rewards/PreemptiveSaveRewards.h>
#include <RLGymCPP/Rewards/AdvancedDemoRewards.h>
#include <RLGymCPP/Rewards/RecoveryRewards.h>
#include <RLGymCPP/Rewards/AdvancedPlayRewards.h>
#include <RLGymCPP/Rewards/PositioningRewards.h>
#include <RLGymCPP/Rewards/SmartWallPlayRewards.h>
#include <RLGymCPP/Rewards/HookshotReward.h>
#include <RLGymCPP/Rewards/WallDashReward.h>
#include <RLGymCPP/Rewards/OptionControlReward.h>
#include <RLGymCPP/Rewards/OptionGatingWrappers.h>
#include <RLGymCPP/Rewards/AngleFlickRewards.h>
#include <RLGymCPP/Rewards/DoomseeDisshShotReward.h>
#include <RLGymCPP/Rewards/DefensiveClearingRewards.h>
#include <RLGymCPP/Rewards/SpecializedFlickRewards.h>
#include <RLGymCPP/Rewards/AerialRewards.h>
#include <RLGymCPP/Rewards/AerialOutplayReward.h>
#include <RLGymCPP/Rewards/IntelligentWavedashReward.h>
#include <RLGymCPP/Rewards/KickoffStrategyReward.h>
#include <RLGymCPP/Rewards/KickoffRandomizationReward.h>
#include <RLGymCPP/Rewards/KickoffNoChaseTeamPenalty.h>
#include <RLGymCPP/Rewards/AirDribbleUtilities.h>
#include <RLGymCPP/Rewards/AirDribbleBumpReward.h>
#include <RLGymCPP/Rewards/AirDribbleBumpConversionReward.h>
#include <RLGymCPP/Rewards/CompositeFlickReward.h>
#include <RLGymCPP/Rewards/GoalMultiplierReward.h>
#include <RLGymCPP/Rewards/ShootingRewards.h>
#include <RLGymCPP/Rewards/SpatialPassingLaneRewards.h>
#include <RLGymCPP/Rewards/PossessionSideDemoReward.h>
#include <RLGymCPP/Rewards/AirDribbleExplorationReward.h>
#include <RLGymCPP/Rewards/FlickExplorationReward.h>
#include <RLGymCPP/Rewards/RecoveryExplorationReward.h>
#include <RLGymCPP/Rewards/ShotExplorationRewards.h>
#include <RLGymCPP/Rewards/OutplayExplorationReward.h>
#include <RLGymCPP/Rewards/PredictiveDemoRewards.h>
#include <RLGymCPP/Rewards/AdvancedKickoffRewards.h>
#include <RLGymCPP/Rewards/SoloPlayRewards.h>
#include <RLGymCPP/Rewards/RolvVelocityPlayerToBallRewards.h>
#include <RLGymCPP/Rewards/PowerslideTurnRewards.h>
#include <RLGymCPP/Rewards/PowerShotRewards.h>
using namespace RLGC;

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
constexpr TrainingPhase CURRENT_PHASE = TrainingPhase::PHASE_3_GOLD;

/// <summary>
/// Get the reward configuration for the current training phase
/// </summary>
std::vector<WeightedReward> GetPhasedRewards();

/// <summary>
/// Get the reward configuration from the JSON-based configuration file
/// Loads from reward_phases.json for the current training phase
/// This is the new data-driven approach that avoids the massive switch statement
/// </summary>
std::vector<WeightedReward> GeneratePhasedRewardsFromJSON();
