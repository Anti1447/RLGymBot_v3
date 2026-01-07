#include <cmath>
#include <algorithm>
#include <vector>

// Assuming standard RLGym state structure
struct Vec3 {
    float x, y, z;
    
    float length() const {
        return std::sqrt(x*x + y*y + z*z);
    }
    
    Vec3 normalized() const {
        float len = length();
        if (len < 0.0001f) return {0, 0, 0};
        return {x/len, y/len, z/len};
    }
    
    float dot(const Vec3& other) const {
        return x*other.x + y*other.y + z*other.z;
    }
    
    Vec3 operator-(const Vec3& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }
    
    Vec3 operator+(const Vec3& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }
    
    Vec3 operator*(float scalar) const {
        return {x * scalar, y * scalar, z * scalar};
    }
};

struct PhysicsObject {
    Vec3 position;
    Vec3 velocity;
    Vec3 angular_velocity;
    Vec3 forward;
    Vec3 up;
    Vec3 right;
};

struct PlayerData {
    PhysicsObject physics;
    bool on_ground;
    bool has_jump;
    bool has_flip;
    float boost_amount;
    int team_num;
    int car_id;
};

struct GameState {
    PhysicsObject ball;
    std::vector<PlayerData> players;
    int blue_score;
    int orange_score;
};

// Constants
const float FIELD_X = 4096.0f;
const float FIELD_Y = 5120.0f;
const float FIELD_Z = 2044.0f;
const float BACKBOARD_Z = 1952.0f;
const float GOAL_LINE_Y = 5120.0f;

// ============================================================================
// DOUBLE TAP REWARD
// ============================================================================
class DoubleTapReward {
private:
    struct TouchData {
        int player_id;
        float time;
        Vec3 ball_pos;
        Vec3 ball_vel;
        bool was_aerial;
    };
    
    std::vector<TouchData> recent_touches;
    std::vector<bool> last_ball_wall_contact;
    std::vector<bool> last_ball_backboard_contact;
    std::vector<bool> last_ball_ceiling_contact;
    float last_update_time;
    
    const float TOUCH_TIMEOUT = 3.0f;  // Seconds to remember touches
    const float MIN_AERIAL_HEIGHT = 300.0f;
    
    bool is_near_wall(const Vec3& pos) const {
        return std::abs(pos.x) > FIELD_X - 300.0f;
    }
    
    bool is_near_backboard(const Vec3& pos) const {
        return (std::abs(pos.y) > FIELD_Y - 300.0f && pos.z > BACKBOARD_Z - 300.0f);
    }
    
    bool is_near_ceiling(const Vec3& pos) const {
        return pos.z > FIELD_Z - 300.0f;
    }
    
    bool is_goal(const Vec3& pos, int team) const {
        float target_y = (team == 0) ? GOAL_LINE_Y : -GOAL_LINE_Y;
        return (std::abs(pos.x) < 893.0f && 
                pos.z < 642.0f && 
                ((team == 0 && pos.y > target_y) || (team == 1 && pos.y < target_y)));
    }

public:
    DoubleTapReward() : last_update_time(0.0f) {}
    
    float calculate_reward(const GameState& state, int player_idx, float current_time) {
        float reward = 0.0f;
        const auto& player = state.players[player_idx];
        const auto& ball = state.ball;
        
        // Check if ball touched wall/backboard/ceiling
        bool ball_wall_contact = is_near_wall(ball.position);
        bool ball_backboard_contact = is_near_backboard(ball.position);
        bool ball_ceiling_contact = is_near_ceiling(ball.position);
        
        // Detect if player just touched ball
        float dist_to_ball = (ball.position - player.physics.position).length();
        bool player_touching_ball = dist_to_ball < 180.0f;
        
        if (player_touching_ball) {
            bool is_aerial = !player.on_ground && player.physics.position.z > MIN_AERIAL_HEIGHT;
            
            // Check if this is second touch in double tap sequence
            if (!recent_touches.empty()) {
                TouchData& last_touch = recent_touches.back();
                
                // Same player, within timeout, and ball contacted surface between touches
                if (last_touch.player_id == player_idx && 
                    (current_time - last_touch.time) < TOUCH_TIMEOUT) {
                    
                    bool surface_contact = false;
                    float surface_bonus = 1.0f;
                    
                    // Check which surface was contacted
                    if (ball_backboard_contact || last_ball_backboard_contact[player_idx]) {
                        surface_contact = true;
                        surface_bonus = 2.5f;  // Backboard most common and valuable
                    } else if (ball_wall_contact || last_ball_wall_contact[player_idx]) {
                        surface_contact = true;
                        surface_bonus = 2.0f;
                    } else if (ball_ceiling_contact || last_ball_ceiling_contact[player_idx]) {
                        surface_contact = true;
                        surface_bonus = 2.2f;
                    }
                    
                    if (surface_contact) {
                        // Check if ball going toward opponent goal
                        float target_y = (player.team_num == 0) ? GOAL_LINE_Y : -GOAL_LINE_Y;
                        Vec3 to_goal = {0, target_y - ball.position.y, 0};
                        float toward_goal = ball.velocity.normalized().dot(to_goal.normalized());
                        
                        if (toward_goal > 0.3f) {
                            // Base double tap reward
                            reward += 0.8f * surface_bonus;
                            
                            // Bonus for aerial second touch
                            if (is_aerial) {
                                reward += 0.5f;
                            }
                            
                            // Bonus for both touches being aerial
                            if (is_aerial && last_touch.was_aerial) {
                                reward += 0.7f;
                            }
                            
                            // Bonus for speed
                            float speed = ball.velocity.length();
                            reward += std::min(speed / 2300.0f, 1.0f) * 0.4f;
                            
                            // Major bonus if it scores
                            if (is_goal(ball.position, 1 - player.team_num)) {
                                reward += 3.0f * surface_bonus;
                            }
                        }
                    }
                }
            }
            
            // Record this touch
            recent_touches.push_back({
                player_idx,
                current_time,
                ball.position,
                ball.velocity,
                is_aerial
            });
            
            // Clean old touches
            recent_touches.erase(
                std::remove_if(recent_touches.begin(), recent_touches.end(),
                    [current_time, this](const TouchData& t) {
                        return (current_time - t.time) > TOUCH_TIMEOUT;
                    }),
                recent_touches.end()
            );
        }
        
        // Update surface contact tracking
        if (last_ball_wall_contact.size() <= player_idx) {
            last_ball_wall_contact.resize(player_idx + 1, false);
            last_ball_backboard_contact.resize(player_idx + 1, false);
            last_ball_ceiling_contact.resize(player_idx + 1, false);
        }
        
        last_ball_wall_contact[player_idx] = ball_wall_contact;
        last_ball_backboard_contact[player_idx] = ball_backboard_contact;
        last_ball_ceiling_contact[player_idx] = ball_ceiling_contact;
        
        return reward;
    }
};

// ============================================================================
// BOOST STEALING REWARD
// ============================================================================
class BoostStealReward {
private:
    // Big boost pad locations (standard RL field)
    const std::vector<Vec3> big_boost_pads = {
        {-3072.0f, -4096.0f, 73.0f},  // Blue back left
        {3072.0f, -4096.0f, 73.0f},   // Blue back right
        {-3584.0f, 0.0f, 73.0f},      // Mid left
        {3584.0f, 0.0f, 73.0f},       // Mid right
        {-3072.0f, 4096.0f, 73.0f},   // Orange back left
        {3072.0f, 4096.0f, 73.0f}     // Orange back right
    };
    
    std::vector<float> last_boost_amounts;
    
    bool is_opposing_side(const Vec3& pad_pos, int team) const {
        // Blue team = 0 (negative Y is their side)
        // Orange team = 1 (positive Y is their side)
        if (team == 0) {
            return pad_pos.y > 1000.0f;  // In orange half
        } else {
            return pad_pos.y < -1000.0f;  // In blue half
        }
    }

public:
    BoostStealReward() {}
    
    float calculate_reward(const GameState& state, int player_idx) {
        float reward = 0.0f;
        const auto& player = state.players[player_idx];
        
        // Initialize tracking
        if (last_boost_amounts.size() <= player_idx) {
            last_boost_amounts.resize(player_idx + 1, player.boost_amount);
            return 0.0f;
        }
        
        float boost_gained = player.boost_amount - last_boost_amounts[player_idx];
        
        // Check if player gained boost (picked up pad)
        if (boost_gained > 90.0f) {  // Big boost pad threshold
            // Find which pad was likely picked up
            for (const auto& pad : big_boost_pads) {
                float dist = (pad - player.physics.position).length();
                
                if (dist < 200.0f) {  // Close enough to have picked it up
                    if (is_opposing_side(pad, player.team_num)) {
                        // Stole boost from opponent side!
                        reward += 0.4f;
                        
                        // Extra reward if in opponent corner (most aggressive)
                        float corner_dist = std::abs(pad.y) > 3500.0f && std::abs(pad.x) > 2500.0f;
                        if (corner_dist) {
                            reward += 0.3f;
                        }
                        
                        // Small bonus based on how low on boost they were
                        float desperation = 1.0f - (last_boost_amounts[player_idx] / 100.0f);
                        reward += desperation * 0.2f;
                    }
                    break;
                }
            }
        }
        
        last_boost_amounts[player_idx] = player.boost_amount;
        return reward;
    }
};

// ============================================================================
// REVERSE AERIAL REWARD
// ============================================================================
class ReverseAerialReward {
private:
    struct AerialState {
        bool in_reverse_aerial;
        float start_time;
        Vec3 initial_forward;
    };
    
    std::vector<AerialState> aerial_states;
    
    const float MIN_AERIAL_HEIGHT = 250.0f;
    const float REVERSE_ANGLE_THRESHOLD = 2.0f;  // ~115 degrees

public:
    ReverseAerialReward() {}
    
    float calculate_reward(const GameState& state, int player_idx, float current_time) {
        float reward = 0.0f;
        const auto& player = state.players[player_idx];
        const auto& ball = state.ball;
        
        // Initialize tracking
        if (aerial_states.size() <= player_idx) {
            aerial_states.resize(player_idx + 1, {false, 0.0f, {0, 0, 0}});
        }
        
        auto& aerial_state = aerial_states[player_idx];
        
        Vec3 to_ball = (ball.position - player.physics.position).normalized();
        float forward_dot = player.physics.forward.dot(to_ball);
        
        bool is_aerial = !player.on_ground && player.physics.position.z > MIN_AERIAL_HEIGHT;
        bool facing_away = forward_dot < -0.4f;  // Facing away from ball
        
        // Detect start of reverse aerial
        if (is_aerial && facing_away && !aerial_state.in_reverse_aerial) {
            // Just jumped off ground facing away from ball
            aerial_state.in_reverse_aerial = true;
            aerial_state.start_time = current_time;
            aerial_state.initial_forward = player.physics.forward;
        }
        
        // Check if still in reverse aerial
        if (aerial_state.in_reverse_aerial) {
            if (!is_aerial) {
                // Landed, reset
                aerial_state.in_reverse_aerial = false;
            } else {
                // Check if hit ball
                float dist_to_ball = (ball.position - player.physics.position).length();
                bool hit_ball = dist_to_ball < 180.0f;
                
                if (hit_ball) {
                    float duration = current_time - aerial_state.start_time;
                    
                    // Calculate how reversed the initial jump was
                    Vec3 initial_to_ball = (ball.position - player.physics.position).normalized();
                    float reverse_amount = -aerial_state.initial_forward.dot(initial_to_ball);
                    reverse_amount = std::max(0.0f, reverse_amount);
                    
                    // Base reward for hitting ball in reverse aerial
                    reward += 0.8f * reverse_amount;
                    
                    // Bonus for duration (harder to maintain)
                    reward += std::min(duration / 2.0f, 1.0f) * 0.4f;
                    
                    // Bonus for height
                    float height_ratio = (player.physics.position.z - MIN_AERIAL_HEIGHT) / 1000.0f;
                    reward += std::min(height_ratio, 1.0f) * 0.3f;
                    
                    // Check if ball going toward goal
                    float target_y = (player.team_num == 0) ? GOAL_LINE_Y : -GOAL_LINE_Y;
                    Vec3 to_goal = {0, target_y - ball.position.y, 0};
                    float toward_goal = ball.velocity.normalized().dot(to_goal.normalized());
                    
                    if (toward_goal > 0.5f) {
                        reward += 0.5f;
                        
                        // Major bonus if it results in goal
                        if ((player.team_num == 0 && ball.position.y > GOAL_LINE_Y) ||
                            (player.team_num == 1 && ball.position.y < -GOAL_LINE_Y)) {
                            reward += 2.0f;
                        }
                    }
                    
                    aerial_state.in_reverse_aerial = false;
                }
            }
        }
        
        return reward;
    }
};

// ============================================================================
// FIELD SPACING REWARD
// ============================================================================
class FieldSpacingReward {
private:
    struct CoverageArea {
        Vec3 center;
        float radius;
        float coverage_value;
    };
    
    // Calculate coverage cone for a player
    CoverageArea calculate_coverage_cone(const PlayerData& player, const Vec3& ball_pos) const {
        CoverageArea area;
        
        // Coverage center is between player and ball, weighted toward ball
        Vec3 to_ball = ball_pos - player.physics.position;
        float dist_to_ball = to_ball.length();
        
        // Base radius on distance and boost
        float base_radius = 800.0f + (player.boost_amount * 5.0f);
        
        // Coverage is stronger when facing ball and closer
        Vec3 to_ball_norm = to_ball.normalized();
        float facing_ball = std::max(0.0f, player.physics.forward.dot(to_ball_norm));
        
        area.center = player.physics.position + to_ball * 0.6f;
        area.radius = base_radius * (0.5f + 0.5f * facing_ball);
        
        // Value based on position relative to ball and goal
        float value_multiplier = 1.0f;
        
        // Prefer being between ball and own goal
        float own_goal_y = (player.team_num == 0) ? -GOAL_LINE_Y : GOAL_LINE_Y;
        bool between_ball_and_goal = 
            (player.team_num == 0 && player.physics.position.y < ball_pos.y) ||
            (player.team_num == 1 && player.physics.position.y > ball_pos.y);
        
        if (between_ball_and_goal) {
            value_multiplier *= 1.3f;
        }
        
        // Prefer offensive positioning when team has ball control
        bool offensive_position =
            (player.team_num == 0 && player.physics.position.y > 0) ||
            (player.team_num == 1 && player.physics.position.y < 0);
        
        if (offensive_position && dist_to_ball < 2000.0f) {
            value_multiplier *= 1.2f;
        }
        
        area.coverage_value = value_multiplier;
        
        return area;
    }
    
    float calculate_overlap(const CoverageArea& a1, const CoverageArea& a2) const {
        float dist = (a1.center - a2.center).length();
        float combined_radius = a1.radius + a2.radius;
        
        if (dist >= combined_radius) {
            return 0.0f;  // No overlap
        }
        
        // Calculate overlap percentage
        float overlap = (combined_radius - dist) / combined_radius;
        return overlap;
    }

public:
    FieldSpacingReward() {}
    
    float calculate_reward(const GameState& state, int player_idx) {
        float reward = 0.0f;
        const auto& player = state.players[player_idx];
        const auto& ball = state.ball;
        
        // Calculate coverage area for this player
        CoverageArea my_coverage = calculate_coverage_cone(player, ball.position);
        
        // Check overlap with teammates
        for (size_t i = 0; i < state.players.size(); i++) {
            if (i == player_idx) continue;
            
            const auto& other = state.players[i];
            if (other.team_num != player.team_num) continue;  // Skip opponents
            
            CoverageArea other_coverage = calculate_coverage_cone(other, ball.position);
            float overlap = calculate_overlap(my_coverage, other_coverage);
            
            if (overlap < 0.2f) {
                // Good spacing - slight reward
                reward += 0.1f;
            } else if (overlap >= 0.2f && overlap < 0.5f) {
                // Slight overlap - small penalty
                reward -= 0.15f * overlap;
            } else if (overlap >= 0.5f && overlap < 0.8f) {
                // Medium overlap - medium penalty
                reward -= 0.4f * overlap;
            } else {
                // Severe overlap - large penalty
                reward -= 0.8f * overlap;
            }
        }
        
        // Reward good positioning relative to ball
        float dist_to_ball = (ball.position - player.physics.position).length();
        
        // Encourage varied distances (not everyone on ball)
        int teammates_closer = 0;
        for (size_t i = 0; i < state.players.size(); i++) {
            if (i == player_idx) continue;
            const auto& other = state.players[i];
            if (other.team_num != player.team_num) continue;
            
            float other_dist = (ball.position - other.physics.position).length();
            if (other_dist < dist_to_ball) {
                teammates_closer++;
            }
        }
        
        // Reward based on role in positioning
        if (teammates_closer == 0 && dist_to_ball < 1000.0f) {
            reward += 0.15f;  // Closest player, going for ball
        } else if (teammates_closer == 1 && dist_to_ball > 1500.0f && dist_to_ball < 3500.0f) {
            reward += 0.2f;  // Second man, good supporting distance
        } else if (teammates_closer >= 2 && dist_to_ball > 3000.0f) {
            reward += 0.15f;  // Third man, good back position
        }
        
        return reward;
    }
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
    AdvancedMechanicRewards() : current_time(0.0f) {}
    
    float calculate_total_reward(const GameState& state, int player_idx, float dt) {
        current_time += dt;
        
        float total = 0.0f;
        
        // Calculate all rewards
        total += double_tap.calculate_reward(state, player_idx, current_time) * 1.0f;
        total += boost_steal.calculate_reward(state, player_idx) * 0.8f;
        total += reverse_aerial.calculate_reward(state, player_idx, current_time) * 1.2f;
        total += field_spacing.calculate_reward(state, player_idx) * 0.6f;
        
        return total;
    }
    
    // Individual reward getters for analysis
    float get_double_tap_reward(const GameState& state, int player_idx) {
        return double_tap.calculate_reward(state, player_idx, current_time);
    }
    
    float get_boost_steal_reward(const GameState& state, int player_idx) {
        return boost_steal.calculate_reward(state, player_idx);
    }
    
    float get_reverse_aerial_reward(const GameState& state, int player_idx) {
        return reverse_aerial.calculate_reward(state, player_idx, current_time);
    }
    
    float get_field_spacing_reward(const GameState& state, int player_idx) {
        return field_spacing.calculate_reward(state, player_idx);
    }
    
    void reset() {
        current_time = 0.0f;
    }
};
