#pragma once

#include "Reward.h"
#include "../CommonValues.h"
#include <map>
#include <atomic>
#include <cmath>

namespace RLGC {

// ============================================================================
// OFFENSIVE POSITIONING REWARDS
// ============================================================================
// Teaches smart offensive positioning by:
// - Penalizing corner play (low % shots, easy to defend)
// - Rewarding center-oriented attacks (high % scoring opportunities)
// ============================================================================

// ============================================================================
// CornerPlayPenalty
// Penalizes playing the ball into opponent's corners
// - Corners = low scoring percentage, easy to defend, hard to escape
// - Penalty scales with proximity to corner (closer = worse)
// - Applied when touching ball or being near ball in opponent's corner
// ============================================================================
class CornerPlayPenalty : public Reward {
private:
    const float CORNER_PENALTY_RADIUS = 1500.0f;  // Distance from corner to start penalizing
    const float MAX_PENALTY_PER_TICK = -0.15f;    // Max penalty when deep in corner
    const float BALL_TOUCH_PENALTY_MULT = 3.0f;   // Multiply penalty on ball touches

    inline static std::atomic<long long> s_penaltyMilli{0};
    inline static std::atomic<int> s_cornerTouchCount{0};

    // Calculate distance to nearest opponent corner
    float GetDistanceToNearestOpponentCorner(const Vec& pos, Team playerTeam) const {
        // Opponent's end of field
        float oppEndY = (playerTeam == Team::BLUE) ?
            CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;

        // Four corners: opponent back-left, opponent back-right
        Vec corner1(CommonValues::SIDE_WALL_X, oppEndY, 0.0f);      // Right corner
        Vec corner2(-CommonValues::SIDE_WALL_X, oppEndY, 0.0f);     // Left corner

        float dist1 = (pos - corner1).Length();
        float dist2 = (pos - corner2).Length();

        return RS_MIN(dist1, dist2);
    }

    // Check if position is in opponent's half
    bool IsInOpponentHalf(const Vec& pos, Team playerTeam) const {
        if (playerTeam == Team::BLUE) {
            return pos.y > 0.0f;  // Blue attacks positive Y
        } else {
            return pos.y < 0.0f;  // Orange attacks negative Y
        }
    }

public:
    struct Metrics {
        double totalPenalty = 0.0;
        int cornerTouches = 0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        long long p = s_penaltyMilli.exchange(0);
        m.totalPenalty = double(p) / 1000.0;
        m.cornerTouches = s_cornerTouchCount.exchange(0);
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        // No state needed
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only penalize in opponent's half
        if (!IsInOpponentHalf(state.ball.pos, player.team)) {
            return 0.0f;
        }

        // Calculate distance to nearest opponent corner
        float distToCorner = GetDistanceToNearestOpponentCorner(state.ball.pos, player.team);

        // No penalty if outside corner radius
        if (distToCorner > CORNER_PENALTY_RADIUS) {
            return 0.0f;
        }

        // Calculate penalty based on proximity to corner
        // Closer to corner = stronger penalty
        float cornerProximity = 1.0f - (distToCorner / CORNER_PENALTY_RADIUS); // 0.0 at edge, 1.0 at corner
        float basePenalty = MAX_PENALTY_PER_TICK * cornerProximity;

        // Check if player is near the ball (influencing corner play)
        float distToBall = (player.pos - state.ball.pos).Length();
        bool nearBall = distToBall < 800.0f;

        if (!nearBall) {
            return 0.0f;  // Not influencing the play
        }

        // Apply stronger penalty on ball touches (actively playing into corner)
        float penalty = basePenalty;
        if (player.ballTouchedStep) {
            penalty *= BALL_TOUCH_PENALTY_MULT;
            s_cornerTouchCount.fetch_add(1);
        }

        // Track metrics
        if (penalty < 0.0f) {
            s_penaltyMilli.fetch_add(static_cast<long long>(std::abs(penalty) * 1000.0f));
        }

        return penalty;
    }
};

// ============================================================================
// CenterPlayReward
// Rewards offensive positioning and plays toward center of opponent's goal
// - Center shots = higher scoring percentage, harder to defend
// - Rewards ball positioning, touches, and player positioning near center
// - Scales with proximity to goal and centeredness
// ============================================================================
class CenterPlayReward : public Reward {
private:
    const float CENTER_REWARD_ZONE_DEPTH = 3000.0f;  // Distance from goal to reward
    const float CENTER_WIDTH = 2000.0f;               // Width of "center zone" (±1000 from x=0)
    const float MAX_REWARD_PER_TICK = 0.08f;          // Base positioning reward
    const float BALL_TOUCH_REWARD_MULT = 4.0f;        // Multiply on ball touches
    const float SHOT_ON_GOAL_BONUS = 3.0f;            // Bonus for shot toward goal

    inline static std::atomic<long long> s_rewardMilli{0};
    inline static std::atomic<int> s_centerTouchCount{0};
    inline static std::atomic<int> s_shotOnGoalCount{0};

    // Calculate how "centered" a position is (0.0 = edge, 1.0 = perfect center)
    float GetCenteredness(const Vec& pos) const {
        float xDist = std::abs(pos.x);  // Distance from center line (x=0)
        float halfCenterWidth = CENTER_WIDTH / 2.0f;

        if (xDist > halfCenterWidth) {
            return 0.0f;  // Outside center zone
        }

        return 1.0f - (xDist / halfCenterWidth);  // 1.0 at x=0, 0.0 at edge
    }

    // Check if ball is moving toward goal
    bool IsShotOnGoal(const GameState& state, Team playerTeam) const {
        Vec oppGoal = (playerTeam == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        Vec toGoal = (oppGoal - state.ball.pos).Normalized();
        Vec ballVelDir = state.ball.vel.Normalized();

        float alignment = ballVelDir.Dot(toGoal);
        float speed = state.ball.vel.Length();

        // Shot on goal: moving toward goal with decent speed and alignment
        return (alignment > 0.6f && speed > 800.0f);
    }

    // Check if position is in opponent's attacking third
    bool IsInOpponentAttackingThird(const Vec& pos, Team playerTeam) const {
        float oppGoalY = (playerTeam == Team::BLUE) ?
            CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;

        float distToGoal = std::abs(pos.y - oppGoalY);
        return distToGoal < CENTER_REWARD_ZONE_DEPTH;
    }

public:
    struct Metrics {
        double totalReward = 0.0;
        int centerTouches = 0;
        int shotsOnGoal = 0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        long long r = s_rewardMilli.exchange(0);
        m.totalReward = double(r) / 1000.0;
        m.centerTouches = s_centerTouchCount.exchange(0);
        m.shotsOnGoal = s_shotOnGoalCount.exchange(0);
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        // No state needed
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only reward in opponent's attacking third
        if (!IsInOpponentAttackingThird(state.ball.pos, player.team)) {
            return 0.0f;
        }

        // Calculate centeredness of ball position
        float centeredness = GetCenteredness(state.ball.pos);

        if (centeredness <= 0.0f) {
            return 0.0f;  // Ball not in center zone
        }

        // Check if player is influencing the play
        float distToBall = (player.pos - state.ball.pos).Length();
        bool nearBall = distToBall < 800.0f;

        if (!nearBall) {
            return 0.0f;  // Not influencing the play
        }

        // Base reward for being near centered ball
        float reward = MAX_REWARD_PER_TICK * centeredness;

        // Ball touch bonus (actively playing toward center)
        if (player.ballTouchedStep) {
            reward *= BALL_TOUCH_REWARD_MULT;
            s_centerTouchCount.fetch_add(1);

            // Additional bonus for shot on goal from center
            if (IsShotOnGoal(state, player.team)) {
                reward += SHOT_ON_GOAL_BONUS * centeredness;
                s_shotOnGoalCount.fetch_add(1);
            }
        }

        // Track metrics
        if (reward > 0.0f) {
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        }

        return reward;
    }
};

// ============================================================================
// MasterOffensivePositioningReward
// Combines corner penalty and center reward for optimal offensive positioning
// ============================================================================
class MasterOffensivePositioningReward : public Reward {
private:
    CornerPlayPenalty cornerPenalty;
    CenterPlayReward centerReward;

    // Weights (tuned for balance)
    const float CORNER_PENALTY_WEIGHT = 1.0f;
    const float CENTER_REWARD_WEIGHT = 1.0f;

public:
    virtual void Reset(const GameState& initialState) override {
        cornerPenalty.Reset(initialState);
        centerReward.Reset(initialState);
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float total = 0.0f;

        total += cornerPenalty.GetReward(player, state, isFinal) * CORNER_PENALTY_WEIGHT;
        total += centerReward.GetReward(player, state, isFinal) * CENTER_REWARD_WEIGHT;

        return total;
    }

    // Combined metrics
    static void PrintMetrics() {
        auto corner = CornerPlayPenalty::GetAndResetMetrics();
        auto center = CenterPlayReward::GetAndResetMetrics();

        printf("=== OFFENSIVE POSITIONING METRICS ===\n");
        printf("Corner Penalty: %.2f total, %d corner touches\n",
               corner.totalPenalty, corner.cornerTouches);
        printf("Center Reward: %.2f total, %d center touches, %d shots on goal\n",
               center.totalReward, center.centerTouches, center.shotsOnGoal);
    }
};

// ============================================================================
// ActiveSupportingPositionReward
// Rewards active upfield positioning for the 2nd man (supporting role)
// - Encourages positioning near opponent's net for passing opportunities
// - Only rewards if team has possession (closest player is teammate moving toward ball)
// - Context-aware: different rewards based on distance to ball
// - Stateless per-frame evaluation
// ============================================================================
class ActiveSupportingPositionReward : public Reward {
private:
    // Distance thresholds for supporting positioning
    const float MIN_SUPPORT_DIST = 1500.0f;      // Too close = 1st man territory
    const float OPTIMAL_SUPPORT_DIST = 2500.0f;  // Sweet spot for support
    const float MAX_SUPPORT_DIST = 3500.0f;      // Too far = passive

    // Possession and role detection
    const float POSSESSION_VELOCITY_THRESHOLD = 100.0f;  // Min velocity dot to count as "moving toward ball"
    const float MIN_GAME_SIZE = 4;  // Minimum players for team play (2v2)

    inline static std::atomic<long long> s_rewardMilli{0};
    inline static std::atomic<int> s_supportFrames{0};
    inline static std::atomic<int> s_upfieldFrames{0};

    // Enum for role detection (matching RoleAwarenessReward)
    enum class SimpleRole {
        CLOSEST,    // Closest to ball
        MIDDLE,     // Middle distance (2nd man / supporting)
        FURTHEST    // Furthest from ball
    };

    // Determine player's role based on distance to ball relative to teammates
    SimpleRole DetermineRole(const Player& player, const GameState& state) const {
        float distToBall = (player.pos - state.ball.pos).Length();

        std::vector<float> teammateDists;
        for (const auto& p : state.players) {
            if (p.team == player.team && p.carId != player.carId) {
                teammateDists.push_back((p.pos - state.ball.pos).Length());
            }
        }

        if (teammateDists.empty()) {
            return SimpleRole::CLOSEST;
        }

        std::sort(teammateDists.begin(), teammateDists.end());

        // Determine role based on relative distance
        if (distToBall < teammateDists[0]) {
            return SimpleRole::CLOSEST;
        }
        else if (teammateDists.size() >= 2 && distToBall > teammateDists[1]) {
            return SimpleRole::FURTHEST;
        }
        else {
            return SimpleRole::MIDDLE;
        }
    }

    // Check if team has possession (closest player is teammate moving toward ball)
    bool DoesTeamHavePossession(const Player& player, const GameState& state) const {
        float closestDistAllTeams = 1e10f;
        Team closestTeam = Team::BLUE;  // Default
        Vec closestPlayerVel = Vec(0, 0, 0);
        Vec closestPlayerPos = Vec(0, 0, 0);

        // Find closest player (any team)
        for (const auto& p : state.players) {
            float dist = (p.pos - state.ball.pos).Length();
            if (dist < closestDistAllTeams) {
                closestDistAllTeams = dist;
                closestTeam = p.team;
                closestPlayerVel = p.vel;
                closestPlayerPos = p.pos;
            }
        }

        // If opponent has closest player, they have possession
        if (closestTeam != player.team) {
            return false;
        }

        // Check if closest teammate is actively moving toward ball
        Vec dirToBall = (state.ball.pos - closestPlayerPos).Normalized();
        float movementTowardBall = closestPlayerVel.Dot(dirToBall);

        return movementTowardBall > POSSESSION_VELOCITY_THRESHOLD;
    }

    // Check if player is positioned upfield (closer to opponent goal than ball)
    bool IsPositionedUpfield(const Player& player, const GameState& state) const {
        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;

        float playerDistToOppGoal = (player.pos - oppGoal).Length();
        float ballDistToOppGoal = (state.ball.pos - oppGoal).Length();

        return playerDistToOppGoal < ballDistToOppGoal;
    }

public:
    struct Metrics {
        double totalReward = 0.0;
        int supportFrames = 0;
        int upfieldFrames = 0;
        int optimalPositioningFrames = 0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        long long r = s_rewardMilli.exchange(0);
        m.totalReward = double(r) / 1000.0;
        m.supportFrames = s_supportFrames.exchange(0);
        m.upfieldFrames = s_upfieldFrames.exchange(0);
        // optimalPositioningFrames tracked implicitly in reward frames
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        // No state needed - stateless per-frame evaluation
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Skip 1v1 (only reward in 2v2+)
        if (state.players.size() < static_cast<int>(MIN_GAME_SIZE)) {
            return 0.0f;
        }

        // Check if team has possession
        // (Closest player must be teammate AND moving toward ball)
        if (!DoesTeamHavePossession(player, state)) {
            return 0.0f;  // Opponent has possession - play defense instead
        }

        // Determine player's role
        SimpleRole role = DetermineRole(player, state);

        // Only reward 2nd man (supporting / MIDDLE role)
        if (role != SimpleRole::MIDDLE) {
            return 0.0f;  // Not supporting role
        }

        // Check if positioned upfield
        if (!IsPositionedUpfield(player, state)) {
            return 0.0f;  // Not positioned upfield
        }

        // Check distance to ball (should be in supporting range)
        float distToBall = (player.pos - state.ball.pos).Length();

        if (distToBall < MIN_SUPPORT_DIST || distToBall > MAX_SUPPORT_DIST) {
            return 0.0f;  // Outside supporting range
        }

        // Calculate reward based on how close to optimal distance
        // Maximum reward when at optimal distance
        float deviation = std::abs(distToBall - OPTIMAL_SUPPORT_DIST);
        float reward = 0.4f * (1.0f - (deviation / OPTIMAL_SUPPORT_DIST));
        reward = RS_CLAMP(reward, 0.0f, 0.4f);

        // Track metrics
        s_supportFrames.fetch_add(1);
        s_upfieldFrames.fetch_add(1);
        if (reward > 0.0f) {
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        }

        return reward;
    }
};

} // namespace RLGC
