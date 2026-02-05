#pragma once

#include <GigaLearnCPP/Learner.h>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/Rewards/AdvancedRewards.h>
#include <RLGymCPP/Rewards/ConfidenceRewards.h>
#include <RLGymCPP/Rewards/KickoffRewards.h>
#include <RLGymCPP/Rewards/BoostManagementRewards.h>
#include <RLGymCPP/Rewards/MultiModalRewards.h>
#include <RLGymCPP/Rewards/CompleteMechanicsRewards.h>
#include <RLGymCPP/Rewards/OutplayRewards.h>
#include <RLGymCPP/Rewards/FlipResetReward.h>
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
#include <RLGymCPP/Rewards/AirDribbleAccuracyReward.h>
#include <RLGymCPP/Rewards/AirDribbleClearingReward.h>
#include <RLGymCPP/Rewards/HookshotReward.h>
#include <RLGymCPP/Rewards/WallDashReward.h>
#include <RLGymCPP/Rewards/OptionControlReward.h>
#include <RLGymCPP/Rewards/OptionGatingWrappers.h>

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
constexpr TrainingPhase CURRENT_PHASE = TrainingPhase::PHASE_5_DIAMOND;

/// <summary>
/// Get the reward configuration for the current training phase
/// </summary>
std::vector<WeightedReward> GetPhasedRewards();
