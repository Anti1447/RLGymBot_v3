#include "ConfigLoader.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace RLGC {

// Static member initialization
SimpleJsonParser::PhaseWeightMap ConfigLoader::s_phases;
std::string ConfigLoader::s_loadedPath;

std::string SimpleJsonParser::TrimWhitespace(const std::string& str) {
    size_t start = 0;
    while (start < str.length() && std::isspace(str[start])) {
        start++;
    }
    size_t end = str.length();
    while (end > start && std::isspace(str[end - 1])) {
        end--;
    }
    return str.substr(start, end - start);
}

SimpleJsonParser::PhaseWeightMap SimpleJsonParser::ParseRewardPhasesJson(const std::string& filePath) {
    // Read file
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filePath);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonContent = buffer.str();
    file.close();

    if (jsonContent.empty()) {
        throw std::runtime_error("JSON file is empty: " + filePath);
    }

    PhaseWeightMap result;

    // Extract "phases" object
    std::string phasesContent = ExtractPhasesObject(jsonContent);
    if (phasesContent.empty()) {
        throw std::runtime_error("Cannot find 'phases' object in JSON");
    }

    // Split phases by phase name
    // We expect: "PHASE_1_BRONZE": { ... }, "PHASE_2_SILVER": { ... }, ...
    size_t pos = 0;
    while (pos < phasesContent.length()) {
        // Find phase name (quoted string)
        size_t quoteStart = phasesContent.find('"', pos);
        if (quoteStart == std::string::npos) break;

        size_t quoteEnd = phasesContent.find('"', quoteStart + 1);
        if (quoteEnd == std::string::npos) break;

        std::string phaseName = phasesContent.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

        // Find the opening brace for this phase
        size_t braceStart = phasesContent.find('{', quoteEnd);
        if (braceStart == std::string::npos) break;

        // Find matching closing brace
        int braceCount = 1;
        size_t braceEnd = braceStart + 1;
        while (braceEnd < phasesContent.length() && braceCount > 0) {
            if (phasesContent[braceEnd] == '{') braceCount++;
            else if (phasesContent[braceEnd] == '}') braceCount--;
            braceEnd++;
        }

        if (braceCount != 0) {
            throw std::runtime_error("Mismatched braces in JSON for phase: " + phaseName);
        }

        // Extract and parse the phase object
        std::string phaseJson = phasesContent.substr(braceStart, braceEnd - braceStart);
        result[phaseName] = ParsePhaseObject(phaseJson);

        pos = braceEnd;
    }

    if (result.empty()) {
        throw std::runtime_error("No phases found in JSON file");
    }

    return result;
}

std::string SimpleJsonParser::ExtractPhasesObject(const std::string& jsonContent) {
    size_t phasesPos = jsonContent.find("\"phases\"");
    if (phasesPos == std::string::npos) {
        throw std::runtime_error("Cannot find 'phases' key in JSON");
    }

    // Find the opening brace
    size_t braceStart = jsonContent.find('{', phasesPos);
    if (braceStart == std::string::npos) {
        throw std::runtime_error("Cannot find opening brace for 'phases' object");
    }

    // Find the matching closing brace
    int braceCount = 1;
    size_t braceEnd = braceStart + 1;
    while (braceEnd < jsonContent.length() && braceCount > 0) {
        if (jsonContent[braceEnd] == '{') braceCount++;
        else if (jsonContent[braceEnd] == '}') braceCount--;
        braceEnd++;
    }

    if (braceCount != 0) {
        throw std::runtime_error("Mismatched braces in JSON");
    }

    return jsonContent.substr(braceStart + 1, braceEnd - braceStart - 2);
}

SimpleJsonParser::RewardWeightMap SimpleJsonParser::ParsePhaseObject(const std::string& phaseJson) {
    RewardWeightMap result;

    // Parse key-value pairs: "RewardName": 0.4, "AnotherReward": 5.0, ...
    size_t pos = 0;
    while (pos < phaseJson.length()) {
        // Find quoted reward name
        size_t quoteStart = phaseJson.find('"', pos);
        if (quoteStart == std::string::npos) break;

        size_t quoteEnd = phaseJson.find('"', quoteStart + 1);
        if (quoteEnd == std::string::npos) break;

        std::string rewardName = phaseJson.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

        // Find the colon and value
        size_t colonPos = phaseJson.find(':', quoteEnd);
        if (colonPos == std::string::npos) break;

        // Find the number (skip whitespace)
        size_t numStart = colonPos + 1;
        while (numStart < phaseJson.length() && (std::isspace(phaseJson[numStart]) || phaseJson[numStart] == ':')) {
            numStart++;
        }

        // Find the end of the number (comma or closing brace)
        size_t numEnd = numStart;
        while (numEnd < phaseJson.length() && (std::isdigit(phaseJson[numEnd]) || phaseJson[numEnd] == '.' || phaseJson[numEnd] == '-')) {
            numEnd++;
        }

        if (numEnd <= numStart) {
            throw std::runtime_error("Cannot parse numeric value for reward: " + rewardName);
        }

        std::string valueStr = phaseJson.substr(numStart, numEnd - numStart);
        float weight = std::stof(valueStr);
        result[rewardName] = weight;

        pos = numEnd;
    }

    return result;
}

void ConfigLoader::LoadDefaultConfig() {
    // Try different candidate paths
    const std::vector<std::string> candidates = {
        "reward_phases.json",
        "../reward_phases.json",
        "../../reward_phases.json",
    };

    // Also try environment variable
    const char* envPath = std::getenv("REWARD_PHASES_PATH");
    std::vector<std::string> pathsToTry = candidates;
    if (envPath) {
        pathsToTry.insert(pathsToTry.begin(), std::string(envPath));
    }

    for (const auto& path : pathsToTry) {
        if (TryLoadFromPath(path)) {
            return;
        }
    }

    throw std::runtime_error(
        "Cannot find reward_phases.json. Tried:\n"
        "  - reward_phases.json\n"
        "  - ../reward_phases.json\n"
        "  - ../../reward_phases.json\n"
        "  - $REWARD_PHASES_PATH environment variable\n"
        "Set REWARD_PHASES_PATH or place reward_phases.json in the working directory or build folder."
    );
}

void ConfigLoader::LoadConfig(const std::string& filePath) {
    s_phases = SimpleJsonParser::ParseRewardPhasesJson(filePath);
    s_loadedPath = filePath;
}

bool ConfigLoader::TryLoadFromPath(const std::string& candidatePath) {
    try {
        std::ifstream file(candidatePath);
        if (!file.is_open()) {
            return false;
        }
        file.close();

        LoadConfig(candidatePath);
        std::cerr << "[ConfigLoader] Loaded reward phases from: " << candidatePath << std::endl;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

const ConfigLoader::RewardWeightMap& ConfigLoader::GetPhaseRewards(const std::string& phaseName) {
    auto it = s_phases.find(phaseName);
    if (it == s_phases.end()) {
        throw std::runtime_error("Phase not found in configuration: " + phaseName);
    }
    return it->second;
}

const ConfigLoader::PhaseWeightMap& ConfigLoader::GetAllPhases() {
    return s_phases;
}

bool ConfigLoader::HasPhase(const std::string& phaseName) {
    return s_phases.find(phaseName) != s_phases.end();
}

void ConfigLoader::Validate() {
    if (s_phases.empty()) {
        throw std::runtime_error("No phases loaded in configuration");
    }

    // Basic validation: each phase should have at least one reward
    for (const auto& [phaseName, rewards] : s_phases) {
        if (rewards.empty()) {
            throw std::runtime_error("Phase '" + phaseName + "' has no rewards configured");
        }
    }
}

const std::string& ConfigLoader::GetLoadedFilePath() {
    return s_loadedPath;
}

}  // namespace RLGC
