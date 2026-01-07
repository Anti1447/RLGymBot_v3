#include "advanced_mechanic_rewards.h"
#include <iostream>

// Example integration with RLGym training loop
class RLGymRewardFunction {
private:
    AdvancedMechanicRewards advanced_rewards;
    float last_step_time;

public:
    RLGymRewardFunction() : last_step_time(0.0f) {}
    
    void reset() {
        advanced_rewards.reset();
        last_step_time = 0.0f;
    }
    
    float get_reward(const GameState& state, int player_idx, float dt) {
        float reward = 0.0f;
        
        // Get combined advanced mechanic rewards
        reward += advanced_rewards.calculate_total_reward(state, player_idx, dt);
        
        // You can still add other rewards here
        // reward += basic_movement_reward(state, player_idx);
        // reward += touch_ball_reward(state, player_idx);
        // etc.
        
        return reward;
    }
    
    // Optional: Get individual reward components for logging/analysis
    void log_reward_breakdown(const GameState& state, int player_idx) {
        std::cout << "Player " << player_idx << " Reward Breakdown:" << std::endl;
        std::cout << "  Double Tap: " 
                  << advanced_rewards.get_double_tap_reward(state, player_idx) << std::endl;
        std::cout << "  Boost Steal: " 
                  << advanced_rewards.get_boost_steal_reward(state, player_idx) << std::endl;
        std::cout << "  Reverse Aerial: " 
                  << advanced_rewards.get_reverse_aerial_reward(state, player_idx) << std::endl;
        std::cout << "  Field Spacing: " 
                  << advanced_rewards.get_field_spacing_reward(state, player_idx) << std::endl;
    }
};

// Example: Using in training step
void example_training_step() {
    RLGymRewardFunction reward_fn;
    GameState state;  // Would be populated from RLGym
    float dt = 1.0f / 120.0f;  // 120 Hz physics tick
    
    // Get rewards for each player
    for (size_t i = 0; i < state.players.size(); i++) {
        float reward = reward_fn.get_reward(state, i, dt);
        
        // Send reward to your RL agent
        // agent.step(observation, reward);
        
        // Optional: Log detailed breakdown
        if (i == 0) {  // Only log for first player
            reward_fn.log_reward_breakdown(state, i);
        }
    }
}

// Example: Episode reset
void example_episode_reset() {
    RLGymRewardFunction reward_fn;
    reward_fn.reset();
}

int main() {
    std::cout << "Advanced Mechanic Rewards - Usage Example" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Features implemented:" << std::endl;
    std::cout << "  1. Double Tap Rewards" << std::endl;
    std::cout << "     - Detects 2-touch sequences with surface contact" << std::endl;
    std::cout << "     - Tracks backboard (2.5x), wall (2.0x), ceiling (2.2x)" << std::endl;
    std::cout << "     - Extra rewards for aerial touches and goals" << std::endl;
    std::cout << std::endl;
    std::cout << "  2. Boost Stealing Rewards" << std::endl;
    std::cout << "     - Rewards collecting 100 boost from opponent side" << std::endl;
    std::cout << "     - Extra reward for corner steals (most aggressive)" << std::endl;
    std::cout << "     - Bonus when low on boost (desperation factor)" << std::endl;
    std::cout << std::endl;
    std::cout << "  3. Reverse Aerial Rewards" << std::endl;
    std::cout << "     - Detects jumping while facing away from ball" << std::endl;
    std::cout << "     - Rewards hitting ball after reverse takeoff" << std::endl;
    std::cout << "     - Bonuses for duration, height, and scoring" << std::endl;
    std::cout << std::endl;
    std::cout << "  4. Field Spacing Rewards" << std::endl;
    std::cout << "     - Calculates coverage 'cones' for each player" << std::endl;
    std::cout << "     - Slight reward for minimal overlap (<20%)" << std::endl;
    std::cout << "     - Medium penalty for medium overlap (20-50%)" << std::endl;
    std::cout << "     - Large penalty for severe overlap (>50%)" << std::endl;
    std::cout << "     - Encourages proper rotation (1st/2nd/3rd man)" << std::endl;
    std::cout << std::endl;
    
    return 0;
}
