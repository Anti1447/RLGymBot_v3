#include "ModelConfig.h"

PhaseConfig GetPhaseConfig() {
    PhaseConfig cfg;

    // Network size is CONSTANT - defined in ConfigureLearnerConfig()
    // Only these parameters change per phase

    switch (CURRENT_PHASE) {
    case TrainingPhase::PHASE_1_BRONZE: // OVERFITS AFTER 200M STEPS!!!
        cfg.entropyScale = 0.045f;          // Highest exploration
        cfg.policyLR = 2.5e-4f;
        cfg.criticLR = 2.5e-4f;
        cfg.noTouchTimeout = 15.0f;         // Very lenient
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.3f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;
        break;

    case TrainingPhase::PHASE_2_SILVER:
        cfg.entropyScale = 0.04f;
        cfg.policyLR = 2e-4f;
        cfg.criticLR = 2e-4f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.4f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;
        break;

    case TrainingPhase::PHASE_3_GOLD:
        cfg.entropyScale = 0.035f;
        cfg.policyLR = 1.8e-4f;
        cfg.criticLR = 1.8e-4f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.4f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;
        cfg.enableProfiling = false;  // CHANGE THIS TO true/false TO ENABLE/DISABLE PROFILING
        break;

    case TrainingPhase::PHASE_4_PLATINUM:
        cfg.entropyScale = 0.032f;
        cfg.policyLR = 1.3e-4f;
        cfg.criticLR = 1.3e-4f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.6f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;  // 0% aerial training Not working well yet
        break;

    case TrainingPhase::PHASE_5_DIAMOND:
        cfg.entropyScale = 0.031f;
        cfg.policyLR = 1.4e-4f;
        cfg.criticLR = 1.4e-4f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.65f;
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
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;  // 15% aerial training
        break;

    case TrainingPhase::PHASE_7_GC:
        cfg.entropyScale = 0.009f;
        cfg.policyLR = 8e-5f;
        cfg.criticLR = 8e-5f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.55f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;  // 10% aerial training
        break;

    case TrainingPhase::PHASE_8_SSL:
        cfg.entropyScale = 0.008f;
        cfg.policyLR = 5e-5f;
        cfg.criticLR = 5e-5f;
        cfg.noTouchTimeout = 12.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.6f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;  // 5% aerial training
        break;

    case TrainingPhase::PHASE_9_PRO:
        cfg.entropyScale = 0.008f;          // Maintain some exploration
        cfg.policyLR = 3e-5f;               // Very fine tuning
        cfg.criticLR = 3e-5f;
        cfg.noTouchTimeout = 12.0f;         // Strictest
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.65f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;  // 5% aerial training
        break;

    default:
        // Fallback to Bronze settings
        cfg.entropyScale = 0.045f;
        cfg.policyLR = 2.5e-4f;
        cfg.criticLR = 2.5e-4f;
        cfg.noTouchTimeout = 15.0f;
        cfg.useRandomState = true;
        cfg.randomStateChance = 0.3f;
        cfg.useAerialTraining = false;
        cfg.aerialTrainingChance = 0.0f;
        break;
    }

    return cfg;
}

void ConfigureLearnerConfig(LearnerConfig& cfg, const PhaseConfig& phaseCfg) {
    // === CHECKPOINTS ===
    // Use absolute path to ensure it works regardless of working directory
    cfg.checkpointFolder = "C:\\Projects\\RLGymBot_v3\\GigaLearnBot\\build\\checkpoints";
    cfg.tsPerSave = 150'000'000ULL;      // Save every 100M steps
    cfg.checkpointsToKeep = 128;

    // === POLICY VERSIONS ===
    cfg.tsPerVersion = 300'000'000ULL;   // New version every 200M steps
    cfg.maxOldVersions = 64;

    cfg.trainAgainstOldVersions = true;
    cfg.trainAgainstOldChance = 0.3f;

    // === SKILL TRACKER ===
    // Skill tracker evaluates the agent across mixed game modes (1v1, 2v2, 3v3)
    // The game mode sampling (GameModeConfig) automatically randomizes evaluations across modes
    // with distribution: 1v1 (1/6), 2v2 (2/6), 3v3 (3/6)
    // This ensures the skill rating reflects performance across all modes
    cfg.skillTracker.enabled = true;
    cfg.skillTracker.numArenas = 32;       // Sufficient for mixed-mode evaluation
    cfg.skillTracker.simTime = 60.f;       // Per-arena sim time
    cfg.skillTracker.maxSimTime = 240.f;
    cfg.skillTracker.updateInterval = 8;
    cfg.skillTracker.ratingInc = 5.f;
    cfg.skillTracker.initialRating = 0.f;
    cfg.skillTracker.deterministic = false;

    // === MuZero Implementation (Light policy-only mode) ===
    cfg.reanalysis.enabled = false;
    cfg.reanalysis.mctsSimulations = 16;           // Very low for testing
    cfg.reanalysis.reanalysisInterval = 50;        // Every 50 iterations instead of 100
    cfg.reanalysis.samplesPerReanalysis = 200;     // Small batch
    cfg.reanalysis.minBufferSize = 5000;           // Lower threshold
    cfg.reanalysis.policyReanalysisWeight = 0.0f;  // Light MCTS on policy (START LOW)
    cfg.reanalysis.valueReanalysisWeight = 0.0f;   // NO MCTS on critic (policy-only mode)


    // === DEVICE ===
    cfg.deviceType = LearnerDeviceType::GPU_CUDA;

    // === TIMING ===
    cfg.tickSkip = 8;
    cfg.actionDelay = cfg.tickSkip - 1;
    cfg.numGames = 310;
    cfg.randomSeed = 456;

    // === PPO CONFIG ===
    int tsPerItr = 300'000;
    cfg.ppo.tsPerItr = tsPerItr;
    cfg.ppo.batchSize = tsPerItr;
    cfg.ppo.miniBatchSize = 25'000;
    cfg.ppo.epochs = 1;
    cfg.ppo.maskEntropy = true; // Enable entropy masking to combine with action masking. very important

    // Phase-specific learning parameters
    cfg.ppo.entropyScale = phaseCfg.entropyScale;
    cfg.ppo.gaeGamma = 0.9901;
    cfg.ppo.policyLR = phaseCfg.policyLR;
    cfg.ppo.criticLR = phaseCfg.criticLR;

    // === NETWORK SIZE (CONSTANT FOR ALL PHASES) ===
    cfg.ppo.sharedHead.layerSizes = { 1024, 512, 512 };
    cfg.ppo.policy.layerSizes = { 512, 512 };
    cfg.ppo.critic.layerSizes = { 512, 512 };

    auto optim = ModelOptimType::ADAMW;
    cfg.ppo.policy.optimType = optim;
    cfg.ppo.critic.optimType = optim;
    cfg.ppo.sharedHead.optimType = optim;

    auto activation = ModelActivationType::GELU;
    cfg.ppo.policy.activationType = activation;
    cfg.ppo.critic.activationType = activation;
    cfg.ppo.sharedHead.activationType = activation;

    bool addLayerNorm = true;
    cfg.ppo.policy.addLayerNorm = addLayerNorm;
    cfg.ppo.critic.addLayerNorm = addLayerNorm;
    cfg.ppo.sharedHead.addLayerNorm = addLayerNorm;
}
