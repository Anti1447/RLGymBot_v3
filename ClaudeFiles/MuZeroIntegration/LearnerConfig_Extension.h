// LearnerConfig.h Extension - 2025-01-11 | Rev 1.0 | NJH
// Add this to your existing LearnerConfig.h

// Inside struct LearnerConfig:

struct {
    bool enabled = false;                    // Enable reanalysis
    
    // Buffer config
    size_t bufferSize = 100000;              // Experience samples to keep
    size_t minBufferSize = 10000;            // Min before sampling
    
    // Reanalysis schedule
    int reanalysisInterval = 5;              // Every N PPO updates
    size_t samplesPerReanalysis = 2000;      // How many to reanalyze
    
    // MCTS config
    int mctsSimulations = 8;                 // Sims per state (START LOW!)
    float mctsCPuct = 1.25f;                 // Exploration constant
    bool mctsAddNoise = false;               // Root noise (exploration)
    
    // Loss mixing
    float policyReanalysisWeight = 0.3f;     // How much to trust MCTS vs PPO policy
    float valueReanalysisWeight = 0.3f;      // How much to trust MCTS vs PPO value
    
    // Performance
    bool reanalysisBatchInference = true;    // Batch NN calls during MCTS
    int reanalysisThreads = 4;               // Parallel searches
    
} reanalysis;

// Usage in ExampleMain.cpp:
//
// cfg.reanalysis.enabled = true;
// cfg.reanalysis.mctsSimulations = 4;  // Start very low!
// cfg.reanalysis.reanalysisInterval = 10;
// cfg.reanalysis.samplesPerReanalysis = 1000;
