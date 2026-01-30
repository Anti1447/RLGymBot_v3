#pragma once

#include <GigaLearnCPP/Learner.h>
#include "RewardConfig.h"

using namespace GGL;

/// <summary>
/// Phase configuration for training parameters
/// Contains learning rates, exploration settings, and state setter configuration
/// </summary>
struct PhaseConfig {
    float entropyScale;
    float policyLR;
    float criticLR;
    float noTouchTimeout;
    bool useRandomState;
    float randomStateChance;
    bool useAerialTraining;
    float aerialTrainingChance;
    bool enableProfiling = false;  // Enable environment profiling
};

/// <summary>
/// Get phase-specific training configuration
/// Contains learning rates, entropy, and curriculum settings
/// </summary>
PhaseConfig GetPhaseConfig();

/// <summary>
/// Configure the LearnerConfig object with all training settings
/// This includes checkpoints, MuZero, PPO, and device configuration
/// </summary>
void ConfigureLearnerConfig(LearnerConfig& cfg, const PhaseConfig& phaseCfg);
