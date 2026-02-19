#pragma once

#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>

// Diagnostic types
enum class RewardProblemType {
    NO_PROBLEM,
    LOW_ACTIVATION,
    HIGH_VARIANCE,
    POOR_CORRELATION,
    CONFLICTING_WITH_OTHER,
    INCORRECT_SCALING,
    REWARD_SATURATION,
    GRADIENT_DILUTION,
    UNKNOWN
};

struct RewardMetrics {
    std::string rewardName;
    float activationRate;              // 0.0 to 1.0: percentage of steps where reward > 0
    float meanActivationValue;         // Average reward magnitude when active
    float varianceInActivation;        // Standard deviation of rewards (0.0 to 1.0 scale)
    float correlationWithWinRate;      // -1.0 to 1.0: does this help win?
    float baseWeight;                  // Current weight in reward configuration
    
    // Conflict analysis
    std::vector<float> correlationWithOtherRewards;  // Pairwise correlations with other rewards
    int conflictCount;                 // How many rewards conflict with this one
    
    // Performance tracking
    int compilationFailures;
    int runtimeErrors;
    float averageLatency;              // seconds per evaluate call
    
    // Computed metrics
    bool isActive;                     // activationRate > 0.01?
    bool isStable;                     // varianceInActivation < 0.6?
    bool isHelpful;                    // correlationWithWinRate > 0.15?
};

struct RewardDiagnosis {
    std::string rewardName;
    RewardProblemType problemType;
    float severity;                    // 0.0 to 1.0
    std::string explanation;
    std::vector<std::string> suggestedFixes;
    std::vector<std::string> conflictingRewardNames;
    
    // For LLM context generation
    std::string metricsContext;
};

class RewardDiagnostician {
public:
    /**
     * Analyze a single reward and diagnose problems
     */
    RewardDiagnosis DiagnoseSingleReward(
        const RewardMetrics& metrics,
        const std::vector<RewardMetrics>& allMetrics,
        const std::map<std::string, float>& rewardWeights
    ) {
        RewardDiagnosis diagnosis;
        diagnosis.rewardName = metrics.rewardName;
        diagnosis.problemType = RewardProblemType::NO_PROBLEM;
        diagnosis.severity = 0.0f;
        
        // Rule 1: Low activation detection
        if (CheckLowActivation(metrics)) {
            diagnosis.problemType = RewardProblemType::LOW_ACTIVATION;
            diagnosis.severity = CalculateLowActivationSeverity(metrics);
            diagnosis.explanation = "Reward rarely fires (< 5% of steps)";
            diagnosis.suggestedFixes = {
                "Increase reward sensitivity (lower thresholds)",
                "Increase reward weight to make it more attractive",
                "Verify the condition for this reward is achievable in current gameplay"
            };
            return diagnosis;
        }
        
        // Rule 2: High variance detection
        if (CheckHighVariance(metrics)) {
            diagnosis.problemType = RewardProblemType::HIGH_VARIANCE;
            diagnosis.severity = CalculateVarianceSeverity(metrics);
            diagnosis.explanation = "Reward signal is too noisy (high variance)";
            diagnosis.suggestedFixes = {
                "Smooth the reward function using exponential moving average",
                "Add state-dependent scaling to normalize outputs",
                "Consider using a more stable underlying metric"
            };
            return diagnosis;
        }
        
        // Rule 3: Conflict detection
        auto conflicts = DetectConflicts(metrics, allMetrics);
        if (!conflicts.empty()) {
            diagnosis.problemType = RewardProblemType::CONFLICTING_WITH_OTHER;
            diagnosis.severity = CalculateConflictSeverity(metrics, conflicts, rewardWeights);
            diagnosis.conflictingRewardNames = conflicts;
            
            if (diagnosis.severity > 0.5f) {
                diagnosis.explanation = "This reward conflicts with: " + JoinStrings(conflicts, ", ");
                diagnosis.suggestedFixes = {
                    "Add state gating to make these rewards mutually exclusive",
                    "Reduce weight of lower-priority reward",
                    "Restructure both rewards to target different situations"
                };
                return diagnosis;
            }
        }
        
        // Rule 4: Poor win correlation
        if (CheckPoorWinCorrelation(metrics)) {
            diagnosis.problemType = RewardProblemType::POOR_CORRELATION;
            diagnosis.severity = CalculateCorrelationSeverity(metrics);
            diagnosis.explanation = "Reward fires but doesn't help the bot win";
            diagnosis.suggestedFixes = {
                "Verify this is actually the behavior you want",
                "Check if the reward is measuring the right game state",
                "Consider removing or restructuring this reward"
            };
            return diagnosis;
        }
        
        // Rule 5: Gradient dilution (many active rewards)
        int activeRewardCount = CountActiveRewards(allMetrics);
        if (activeRewardCount > 35 && metrics.isActive) {
            diagnosis.problemType = RewardProblemType::GRADIENT_DILUTION;
            diagnosis.severity = 0.3f;  // Lower severity - manageable with GA
            diagnosis.explanation = fmt::format(
                "Too many active rewards ({}/40) causing gradient conflicts",
                activeRewardCount
            );
            diagnosis.suggestedFixes = {
                "Consolidate rewards with similar objectives",
                "Gate some rewards to only activate in specific scenarios",
                "Use genetic algorithm to find optimal weight combinations"
            };
            return diagnosis;
        }
        
        // Rule 6: Incorrect scaling
        if (CheckIncorrectScaling(metrics, rewardWeights)) {
            diagnosis.problemType = RewardProblemType::INCORRECT_SCALING;
            diagnosis.severity = 0.4f;
            diagnosis.explanation = "Weight doesn't match the importance of this reward";
            diagnosis.suggestedFixes = {
                "Increase weight if activation is good but win correlation is low",
                "Decrease weight if this is overshadowing more important mechanics"
            };
            return diagnosis;
        }
        
        // No problem detected
        diagnosis.problemType = RewardProblemType::NO_PROBLEM;
        return diagnosis;
    }
    
    /**
     * Diagnose all rewards and return sorted list
     */
    std::vector<RewardDiagnosis> DiagnoseAllRewards(
        const std::vector<RewardMetrics>& allMetrics,
        const std::map<std::string, float>& rewardWeights
    ) {
        std::vector<RewardDiagnosis> diagnoses;
        
        for (const auto& metrics : allMetrics) {
            auto diagnosis = DiagnoseSingleReward(metrics, allMetrics, rewardWeights);
            if (diagnosis.problemType != RewardProblemType::NO_PROBLEM) {
                diagnoses.push_back(diagnosis);
            }
        }
        
        // Sort by severity (highest first)
        std::sort(diagnoses.begin(), diagnoses.end(),
            [](const RewardDiagnosis& a, const RewardDiagnosis& b) {
                return a.severity > b.severity;
            });
        
        return diagnoses;
    }
    
private:
    // Thresholds (tunable)
    static constexpr float ACTIVATION_THRESHOLD = 0.05f;      // 5%
    static constexpr float VARIANCE_THRESHOLD = 0.6f;         // High variance
    static constexpr float WIN_CORRELATION_THRESHOLD = 0.15f; // Weak correlation
    static constexpr float CONFLICT_THRESHOLD = 0.5f;         // Strong negative correlation
    
    // === Detection Rules ===
    
    bool CheckLowActivation(const RewardMetrics& metrics) {
        return metrics.activationRate < ACTIVATION_THRESHOLD && 
               metrics.baseWeight < 0.5f;
    }
    
    float CalculateLowActivationSeverity(const RewardMetrics& metrics) {
        // Severity increases as activation drops to 0
        return std::max(0.0f, (ACTIVATION_THRESHOLD - metrics.activationRate) / ACTIVATION_THRESHOLD);
    }
    
    bool CheckHighVariance(const RewardMetrics& metrics) {
        // Only flag as problem if both high variance AND low mean value
        return metrics.varianceInActivation > VARIANCE_THRESHOLD &&
               metrics.meanActivationValue < 0.5f &&
               metrics.activationRate > 0.1f;  // Must be active enough to see variance
    }
    
    float CalculateVarianceSeverity(const RewardMetrics& metrics) {
        // Severity based on how noisy it is
        float noisiness = metrics.varianceInActivation / VARIANCE_THRESHOLD;
        return std::min(1.0f, noisiness);
    }
    
    std::vector<std::string> DetectConflicts(
        const RewardMetrics& metrics,
        const std::vector<RewardMetrics>& allMetrics
    ) {
        std::vector<std::string> conflictingRewards;
        
        for (size_t i = 0; i < allMetrics.size(); i++) {
            if (allMetrics[i].rewardName == metrics.rewardName) continue;
            
            float correlation = metrics.correlationWithOtherRewards[i];
            
            // Negative correlation = conflicting objectives
            if (correlation < -CONFLICT_THRESHOLD) {
                conflictingRewards.push_back(allMetrics[i].rewardName);
            }
        }
        
        return conflictingRewards;
    }
    
    float CalculateConflictSeverity(
        const RewardMetrics& metrics,
        const std::vector<std::string>& conflicts,
        const std::map<std::string, float>& rewardWeights
    ) {
        // Severity increases with:
        // 1. Number of conflicts
        // 2. Both rewards having high weight
        
        float avgConflictStrength = 0.0f;
        for (const auto& conflictName : conflicts) {
            // Get strength from correlation data
            avgConflictStrength += 0.5f;  // Placeholder
        }
        avgConflictStrength /= std::max(1.0f, (float)conflicts.size());
        
        // Both high weights = more severe
        float combinedWeight = metrics.baseWeight;
        for (const auto& name : conflicts) {
            combinedWeight += rewardWeights.at(name);
        }
        
        float weightImportance = std::min(1.0f, combinedWeight / 20.0f);
        
        return avgConflictStrength * 0.5f + weightImportance * 0.5f;
    }
    
    bool CheckPoorWinCorrelation(const RewardMetrics& metrics) {
        return metrics.isActive &&
               metrics.activationRate > 0.1f &&
               metrics.correlationWithWinRate < WIN_CORRELATION_THRESHOLD;
    }
    
    float CalculateCorrelationSeverity(const RewardMetrics& metrics) {
        // If it's active but doesn't help winning, that's bad
        if (!metrics.isActive) return 0.0f;
        
        return std::max(0.0f, WIN_CORRELATION_THRESHOLD - metrics.correlationWithWinRate);
    }
    
    bool CheckIncorrectScaling(
        const RewardMetrics& metrics,
        const std::map<std::string, float>& rewardWeights
    ) {
        // If weight is very low but reward is good, it's underweighted
        // If weight is very high but reward is neutral, it's overweighted
        
        bool underweighted = metrics.isHelpful && metrics.baseWeight < 1.0f;
        bool overweighted = !metrics.isHelpful && metrics.baseWeight > 20.0f;
        
        return underweighted || overweighted;
    }
    
    int CountActiveRewards(const std::vector<RewardMetrics>& allMetrics) {
        int count = 0;
        for (const auto& m : allMetrics) {
            if (m.isActive) count++;
        }
        return count;
    }
    
    // === Utility Functions ===
    
    std::string JoinStrings(const std::vector<std::string>& items, const std::string& separator) {
        if (items.empty()) return "";
        std::string result = items[0];
        for (size_t i = 1; i < items.size(); i++) {
            result += separator + items[i];
        }
        return result;
    }
};
