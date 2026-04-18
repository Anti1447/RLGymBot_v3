#pragma once

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <stdexcept>
#include <GigaLearnCPP/Learner.h>

namespace RLGC {

/**
 * \brief Factory for creating Reward instances by name
 *
 * Centralized registry of all available reward types in the training system.
 * Used to instantiate rewards from configuration data (JSON) without hardcoding
 * reward class names throughout the codebase.
 *
 * Usage:
 *   RewardFactory::Initialize();  // Register all rewards at startup
 *   Reward* r = RewardFactory::Create("VelocityReward");
 *   if (!r) { // handle unknown reward }
 */
class RewardFactory {
public:
    using RewardConstructor = std::function<Reward*()>;

    /**
     * \brief Initialize the factory by registering all available rewards
     * Must be called once at application startup before using Create()
     */
    static void Initialize();

    /**
     * \brief Create a reward instance by name
     * \param name The registered name of the reward (e.g., "VelocityReward")
     * \return Pointer to new Reward instance, or nullptr if name is unknown
     * \throws std::runtime_error if the reward name is not registered
     */
    static Reward* Create(const std::string& name);

    /**
     * \brief Check if a reward name is registered
     * \param name The reward name to check
     * \return true if the reward can be created, false otherwise
     */
    static bool Has(const std::string& name);

    /**
     * \brief Get all registered reward names
     * Useful for validation and debugging
     * \return Vector of all registered reward names
     */
    static std::vector<std::string> GetRegisteredRewards();

    /**
     * \brief Register a reward type manually
     * \param name The name to register (e.g., "MyCustomReward")
     * \param constructor A lambda/function that creates the reward instance
     */
    static void Register(const std::string& name, RewardConstructor constructor);

private:
    static std::map<std::string, RewardConstructor>& GetRegistry();
    static bool& IsInitialized();
};

}  // namespace RLGC
