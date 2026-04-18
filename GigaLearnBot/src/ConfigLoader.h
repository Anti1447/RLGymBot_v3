#pragma once

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <fstream>
#include <sstream>

namespace RLGC {

/**
 * \brief Minimal JSON parser for loading reward phase configuration
 *
 * This is a simplified JSON parser tailored to the reward_phases.json format.
 * It's minimal but sufficient for parsing flat key-value structure.
 *
 * Format expected:
 * {
 *   "phases": {
 *     "PHASE_1_BRONZE": {
 *       "VelocityReward": 0.4,
 *       "FaceBallReward": 0.3,
 *       ...
 *     },
 *     ...
 *   }
 * }
 */
class SimpleJsonParser {
public:
    using RewardWeightMap = std::map<std::string, float>;
    using PhaseWeightMap = std::map<std::string, RewardWeightMap>;

    /**
     * \brief Parse the reward phases JSON file
     * \param filePath Path to the reward_phases.json file
     * \return Map of phase names to reward name->weight mappings
     * \throws std::runtime_error if file cannot be opened or JSON is malformed
     */
    static PhaseWeightMap ParseRewardPhasesJson(const std::string& filePath);

private:
    /**
     * \brief Extract the "phases" object from the JSON
     * \param jsonContent Full JSON file content as string
     * \return Content of the phases object (nested JSON)
     */
    static std::string ExtractPhasesObject(const std::string& jsonContent);

    /**
     * \brief Parse a single phase object like { "VelocityReward": 0.4, ... }
     * \param phaseJson The JSON for one phase
     * \return Map of reward names to weights
     */
    static RewardWeightMap ParsePhaseObject(const std::string& phaseJson);

    /**
     * \brief Remove whitespace and comments from JSON-like content
     */
    static std::string TrimWhitespace(const std::string& str);
};

/**
 * \brief Configuration loader for training rewards
 *
 * Loads reward phases from a JSON file at startup and provides
 * easy access to weights per phase.
 */
class ConfigLoader {
public:
    using RewardWeightMap = SimpleJsonParser::RewardWeightMap;
    using PhaseWeightMap = SimpleJsonParser::PhaseWeightMap;

    /**
     * \brief Load reward configuration from the standard location
     * Tries to load from:
     * 1. Current working directory: reward_phases.json
     * 2. ../reward_phases.json (build directory relative)
     * 3. Environment variable: REWARD_PHASES_PATH
     */
    static void LoadDefaultConfig();

    /**
     * \brief Load reward configuration from a specific file path
     * \param filePath Absolute or relative path to reward_phases.json
     * \throws std::runtime_error if file not found or JSON is invalid
     */
    static void LoadConfig(const std::string& filePath);

    /**
     * \brief Get reward weights for a specific phase
     * \param phaseName The phase name (e.g., "PHASE_1_BRONZE")
     * \return Map of reward names to weights
     * \throws std::runtime_error if phase not found
     */
    static const RewardWeightMap& GetPhaseRewards(const std::string& phaseName);

    /**
     * \brief Get all loaded phases
     */
    static const PhaseWeightMap& GetAllPhases();

    /**
     * \brief Check if a phase exists in the loaded configuration
     */
    static bool HasPhase(const std::string& phaseName);

    /**
     * \brief Validate that the configuration is well-formed
     * \throws std::runtime_error if validation fails
     */
    static void Validate();

    /**
     * \brief Get the loaded configuration file path
     */
    static const std::string& GetLoadedFilePath();

private:
    static PhaseWeightMap s_phases;
    static std::string s_loadedPath;

    /**
     * \brief Try to load from a candidate path
     * \return true if file was loaded successfully
     */
    static bool TryLoadFromPath(const std::string& candidatePath);
};

}  // namespace RLGC
