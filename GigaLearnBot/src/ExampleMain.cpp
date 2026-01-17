#include "RewardConfig.h"
#include "ModelConfig.h"
#include "GameModeConfig.h"
#include "DribbleFlickMetrics.h"

#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/OBSBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/StateSetters/RandomState.h>
#include <RLGymCPP/StateSetters/CombinedState.h>
#include <RLGymCPP/StateSetters/AerialTrainingState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/Gamestates/GameState.h>

using namespace GGL;
using namespace RLGC;

// ============================================================================
// TRAINING CONFIGURATION
// ============================================================================
// Phase configurations are in RewardConfig.h and ModelConfig.cpp
// To change phase: Update CURRENT_PHASE in RewardConfig.h
// To change rewards: Update GetPhasedRewards() in RewardConfig.cpp
// To change model/training: Update ConfigureLearnerConfig() in ModelConfig.cpp
// ============================================================================

// Global flag to indicate if we're in render mode (used for fixed game modes)
bool g_renderMode = false;

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

    // Sample game mode based on environment index
    GameModeType mode;
    if (g_renderMode) {
        // In render mode with 3 games: game 0=1v1, game 1=2v2, game 2=3v3
        if (index == 0) {
            mode = GameModeType::MODE_1V1;
        } else if (index == 1) {
            mode = GameModeType::MODE_2V2;
        } else {
            mode = GameModeType::MODE_3V3;
        }
    } else {
        // In training mode: random sampling with 1:2:3 distribution
        mode = GameModeConfig::SampleMode();
    }
    int playersPerTeam = GameModeConfig::GetPlayersPerTeam(mode);

    auto arena = Arena::Create(GameMode::SOCCAR);
    for (int i = 0; i < playersPerTeam; i++) {
        arena->AddCar(Team::BLUE);
        arena->AddCar(Team::ORANGE);
    }

    EnvCreateResult result = {};
    result.actionParser = new DefaultAction();
    result.obsBuilder = new AdvancedObsPadded(3);  // Max 3 players per team, not 6. lol.

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

void StepCallback(Learner* learner, const std::vector<GameState>& states, Report& report) {
    // To prevent expensive metrics from eating at performance, we will only run them on 1/4th of steps
    // This doesn't really matter unless you have expensive metrics (which this example doesn't)
    bool doExpensiveMetrics = (rand() % 4) == 0;

    // Add our metrics
    for (auto& state : states) {
        // Determine game mode from current state
        int numPlayersPerTeam = state.players.size() / 2;  // Assuming balanced teams
        const char* modeStr = nullptr;
        switch (numPlayersPerTeam) {
            case 1: modeStr = "1v1"; break;
            case 2: modeStr = "2v2"; break;
            case 3: modeStr = "3v3"; break;
            default: modeStr = "unknown";
        }

        if (doExpensiveMetrics) {
            for (auto& player : state.players) {
                // Per-mode metrics
                std::string modePrefix = std::string("Mode/") + modeStr + "/";

                report.AddAvg(modePrefix + "Player/In Air Ratio", !player.isOnGround);
                report.AddAvg(modePrefix + "Player/Ball Touch Ratio", player.ballTouchedStep);
                report.AddAvg(modePrefix + "Player/Demoed Ratio", player.isDemoed);

                report.AddAvg(modePrefix + "Player/Speed", player.vel.Length());
                Vec dirToBall = (state.ball.pos - player.pos).Normalized();
                report.AddAvg(modePrefix + "Player/Speed Towards Ball", RS_MAX(0, player.vel.Dot(dirToBall)));

                report.AddAvg(modePrefix + "Player/Boost", player.boost);

                if (player.ballTouchedStep)
                    report.AddAvg(modePrefix + "Player/Touch Height", state.ball.pos.z);
            }
        }

        if (state.goalScored) {
            std::string modePrefix = std::string("Mode/") + modeStr + "/";
            report.AddAvg(modePrefix + "Game/Goal Speed", state.ball.vel.Length());
            report.AddAvg("Game/Goal Speed", state.ball.vel.Length());  // Also track overall
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

    // Set global render flag (used by EnvCreateFunc for cycling mode)
    g_renderMode = renderMode;

    // Toggle here: change to KickoffMode::STOCHASTIC to disable hardcoded kickoffs, KickoffMode::HARDCODED to enable them
    const RLGC::KickoffMode KICKOFF_MODE = RLGC::KickoffMode::STOCHASTIC;

    std::cout << "[CONFIG] Kickoff mode: " <<
        (KICKOFF_MODE == RLGC::KickoffMode::HARDCODED ? "HARDCODED" : "STOCHASTIC") << std::endl;

    // Get phase configuration
    PhaseConfig phaseCfg = GetPhaseConfig();
    LearnerConfig cfg = {};

    // Configure all training settings from ModelConfig
    ConfigureLearnerConfig(cfg, phaseCfg);

    // Configure render mode settings
    if (renderMode) {
        cfg.sendMetrics = false;           // No need to log metrics from render instance
        cfg.renderMode = true;             // Enable visualization
        cfg.ppo.deterministic = true;      // Use deterministic policy (best actions, no exploration)
        cfg.numGames = 3;                  // 3 games: 1v1, 2v2, 3v3 (one per mode, cycling)
        std::cout << "[CONFIG] Rendering with deterministic policy (1v1, 2v2, 3v3 cycling)\n";
    } else {
        cfg.sendMetrics = true;
        cfg.renderMode = false;
    }

    cfg.renderTimeScale = 3.f;
    cfg.kickoffMode = KICKOFF_MODE;  // SET HERE

    // Print current phase info
    const char* phaseNames[] = {    
        "PHASE 1: BRONZE (0-200M)",
        "PHASE 2: SILVER (200M-300M)",
        "PHASE 3: GOLD (300M-5B)",
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