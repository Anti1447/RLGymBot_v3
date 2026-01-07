#ifndef ADVANCED_MECHANIC_REWARDS_H
#define ADVANCED_MECHANIC_REWARDS_H

#include <vector>

// Forward declarations
struct Vec3;
struct PhysicsObject;
struct PlayerData;
struct GameState;

// ============================================================================
// DOUBLE TAP REWARD
// Rewards 2-touch sequences where ball contacts surface between touches
// ============================================================================
class DoubleTapReward {
private:
    struct TouchData;
    std::vector<TouchData> recent_touches;
    std::vector<bool> last_ball_wall_contact;
    std::vector<bool> last_ball_backboard_contact;
    std::vector<bool> last_ball_ceiling_contact;
    float last_update_time;
    
    bool is_near_wall(const Vec3& pos) const;
    bool is_near_backboard(const Vec3& pos) const;
    bool is_near_ceiling(const Vec3& pos) const;
    bool is_goal(const Vec3& pos, int team) const;

public:
    DoubleTapReward();
    float calculate_reward(const GameState& state, int player_idx, float current_time);
};

// ============================================================================
// BOOST STEALING REWARD
// Rewards collecting 100 boost pads from opponent's side of field
// ============================================================================
class BoostStealReward {
private:
    std::vector<Vec3> big_boost_pads;
    std::vector<float> last_boost_amounts;
    bool is_opposing_side(const Vec3& pad_pos, int team) const;

public:
    BoostStealReward();
    float calculate_reward(const GameState& state, int player_idx);
};

// ============================================================================
// REVERSE AERIAL REWARD
// Rewards jumping off ground facing away from ball, then hitting ball
// ============================================================================
class ReverseAerialReward {
private:
    struct AerialState;
    std::vector<AerialState> aerial_states;

public:
    ReverseAerialReward();
    float calculate_reward(const GameState& state, int player_idx, float current_time);
};

// ============================================================================
// FIELD SPACING REWARD
// Defines coverage "cones" and rewards good spacing, punishes overlap
// ============================================================================
class FieldSpacingReward {
private:
    struct CoverageArea;
    CoverageArea calculate_coverage_cone(const PlayerData& player, const Vec3& ball_pos) const;
    float calculate_overlap(const CoverageArea& a1, const CoverageArea& a2) const;

public:
    FieldSpacingReward();
    float calculate_reward(const GameState& state, int player_idx);
};

// ============================================================================
// COMBINED REWARD MANAGER
// ============================================================================
class AdvancedMechanicRewards {
private:
    DoubleTapReward double_tap;
    BoostStealReward boost_steal;
    ReverseAerialReward reverse_aerial;
    FieldSpacingReward field_spacing;
    float current_time;

public:
    AdvancedMechanicRewards();
    
    // Get combined reward
    float calculate_total_reward(const GameState& state, int player_idx, float dt);
    
    // Individual reward getters for analysis/debugging
    float get_double_tap_reward(const GameState& state, int player_idx);
    float get_boost_steal_reward(const GameState& state, int player_idx);
    float get_reverse_aerial_reward(const GameState& state, int player_idx);
    float get_field_spacing_reward(const GameState& state, int player_idx);
    
    void reset();
};

#endif // ADVANCED_MECHANIC_REWARDS_H
