// ReanalysisBuffer.h - 2025-01-11 | Rev 1.0 | NJH
// Replay buffer for MuZero-style reanalysis
// Stores recent experience and provides sampling for offline MCTS

#pragma once
#include "../FrameworkTorch.h"
#include <RLGymCPP/Gamestates/GameState.h>
#include <vector>
#include <random>
#include <mutex>

namespace GGL {

// ============================================================================
// Experience Sample - One timestep of experience
// ============================================================================
struct ExperienceSample {
    RLGC::GameState state;
    int playerIndex;
    int action;           // Action taken
    float reward;         // Reward received
    float value;          // Value estimate (from PPO critic)
    std::vector<float> policy;  // Policy logits (from PPO actor)
    
    // Metadata
    uint64_t timestep;    // Global timestep
    int episodeId;        // Which episode this came from
};

// ============================================================================
// Reanalysis Buffer Config
// ============================================================================
struct ReanalysisBufferConfig {
    size_t maxSize = 100000;           // Max samples to store
    size_t minSizeToSample = 10000;    // Don't sample until we have this many
    bool prioritized = false;           // Use prioritized sampling (future)
};

// ============================================================================
// Reanalysis Buffer
// ============================================================================
class ReanalysisBuffer {
public:
    ReanalysisBuffer(const ReanalysisBufferConfig& config) 
        : config(config) {
        buffer.reserve(config.maxSize);
    }
    
    // Add samples (called after each PPO rollout)
    void Add(const std::vector<ExperienceSample>& samples);
    
    // Sample random batch for reanalysis
    std::vector<ExperienceSample> Sample(size_t batchSize);
    
    // Get current size
    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return buffer.size();
    }
    
    // Check if ready to sample
    bool CanSample() const {
        return Size() >= config.minSizeToSample;
    }
    
    // Clear buffer
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex);
        buffer.clear();
    }
    
private:
    ReanalysisBufferConfig config;
    std::vector<ExperienceSample> buffer;
    mutable std::mutex mutex;
    std::mt19937 rng{std::random_device{}()};
    
    // Circular buffer logic
    size_t writeIndex = 0;
};

// ============================================================================
// Implementation
// ============================================================================

inline void ReanalysisBuffer::Add(const std::vector<ExperienceSample>& samples) {
    std::lock_guard<std::mutex> lock(mutex);
    
    for (const auto& sample : samples) {
        if (buffer.size() < config.maxSize) {
            buffer.push_back(sample);
        } else {
            // Circular buffer: overwrite oldest
            buffer[writeIndex] = sample;
            writeIndex = (writeIndex + 1) % config.maxSize;
        }
    }
}

inline std::vector<ExperienceSample> ReanalysisBuffer::Sample(size_t batchSize) {
    std::lock_guard<std::mutex> lock(mutex);
    
    if (buffer.size() < config.minSizeToSample) {
        return {};  // Not ready yet
    }
    
    std::vector<ExperienceSample> batch;
    batch.reserve(batchSize);
    
    // Uniform random sampling
    std::uniform_int_distribution<size_t> dist(0, buffer.size() - 1);
    
    for (size_t i = 0; i < batchSize; i++) {
        size_t idx = dist(rng);
        batch.push_back(buffer[idx]);
    }
    
    return batch;
}

} // namespace GGL
