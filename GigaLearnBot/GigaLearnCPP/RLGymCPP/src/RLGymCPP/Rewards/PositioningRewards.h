#pragma once

#include "Reward.h"
#include "../CommonValues.h"
#include "RewardMath.h"
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
// - Mode-specific penalty magnitude:
//   * 1v1: -0.25f (harsher; corners more exploitable in 1v1)
//   * 2v2: -0.15f (baseline)
//   * 3v3+: -0.10f (gentler; multiple defenders reduce corner threat)
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
        if (!RewardMath::IsInOpponentHalf(state.ball.pos, player.team)) {
            return 0.0f;
        }

        // Determine game mode based on number of teammates
        int numTeammates = 0;
        for (const auto& p : state.players) {
            if (p.team == player.team && p.carId != player.carId) {
                numTeammates++;
            }
        }

        // Select penalty magnitude based on game mode
        float maxPenalty = MAX_PENALTY_PER_TICK;  // Default: 2v2 baseline
        if (numTeammates == 0) {
            // 1v1: harsher penalty (corners more exploitable 1v1)
            maxPenalty = -0.25f;
        } else if (numTeammates >= 2) {
            // 3v3+: gentler penalty (multiple defenders, corners less critical)
            maxPenalty = -0.10f;
        }
        // numTeammates == 1 (2v2): use default MAX_PENALTY_PER_TICK (-0.15f)

        // Calculate distance to nearest opponent corner
        float distToCorner = GetDistanceToNearestOpponentCorner(state.ball.pos, player.team);

        // No penalty if outside corner radius
        if (distToCorner > CORNER_PENALTY_RADIUS) {
            return 0.0f;
        }

        // Calculate penalty based on proximity to corner
        // Closer to corner = stronger penalty
        float cornerProximity = 1.0f - (distToCorner / CORNER_PENALTY_RADIUS); // 0.0 at edge, 1.0 at corner
        float basePenalty = maxPenalty * cornerProximity;

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
// OWN NET CENTERING PENALTY
// Penalizes centering the ball near own goal (defensive danger zone)
// Discourages risky centering plays in critical defensive positions
// ============================================================================
class OwnNetCenteringPenalty : public Reward {
private:
    static constexpr float DEFENSIVE_ZONE_Y = 2000.0f;  // Danger zone depth from own goal
    static constexpr float CENTER_WIDTH = 2000.0f;       // ±1000 from X=0 is "center"
    static constexpr float MAX_PENALTY_PER_TICK = -0.5f; // Maximum penalty per frame

public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Get own goal location (the goal we're defending)
        Vec ownGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

        // Check if ball is in our defensive zone
        float distToOwnGoal = (state.ball.pos - ownGoal).Length();

        // Must be close to own goal (within 2000 units = danger zone)
        if (distToOwnGoal > DEFENSIVE_ZONE_Y) {
            return 0.0f;  // Ball is not in danger zone
        }

        // Check if ball is "centered" (|X| < 1000 from field midline)
        float ballX = fabs(state.ball.pos.x);
        float halfCenterWidth = CENTER_WIDTH / 2.0f;

        // Calculate centeredness (1.0 = perfectly centered, 0.0 = on wing)
        float centeredness = 1.0f - RS_CLAMP(ballX / halfCenterWidth, 0.0f, 1.0f);

        if (centeredness < 0.3f) {
            return 0.0f;  // Ball is on the wing, not centered - acceptable
        }

        // Check if ball has low horizontal velocity (being centered, not being crossed)
        float ballXVel = fabs(state.ball.vel.x);
        bool isCentering = ballXVel < 500.0f;  // Low horizontal movement = centering action

        if (!isCentering) {
            return 0.0f;  // Ball is being crossed quickly, not centered slowly
        }

        // Penalty scales with:
        // 1. Proximity to own goal (closer = worse)
        // 2. Centeredness (more centered = worse)
        float proximityFactor = 1.0f - RS_CLAMP(distToOwnGoal / DEFENSIVE_ZONE_Y, 0.0f, 1.0f);

        // Return penalty scaled by both factors
        return MAX_PENALTY_PER_TICK * proximityFactor * centeredness;
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
// ActiveSupportingPositionReward (v3) - 3-Slot Pass-to-Score Positioning
// 2026-05-01 | NJH
//
// Redesigned from v2 (single cross-field target) to a 3-slot system that
// models actual pass-to-score positions. The 2nd man is rewarded for holding
// any of three optimal scoring positions given where the ball is:
//
//   Slot 1 - Far Post:      near far goal post (~700 UU off-center, ~500 UU out)
//   Slot 2 - Penalty Spot:  dead center, ~1000 UU in front of goal
//   Slot 3 - Wide Diagonal: opposite side from ball, upfield ~600 UU from ball
//
// Reward = BASE_SCALE × max(proximity_1, proximity_2, proximity_3)
// Gates: 2v2+, team possession, MIDDLE role, ball in opponent's half.
// ============================================================================
class ActiveSupportingPositionReward : public Reward {
private:
    // Slot geometry (UU)
    static constexpr float SLOT1_X        = 700.0f;     // far post x offset from center
    static constexpr float SLOT1_Y_BACK   = 500.0f;     // UU in front of opponent goal line
    static constexpr float SLOT2_Y_BACK   = 1000.0f;    // central penalty spot UU from goal
    static constexpr float SLOT3_X        = 2200.0f;    // wide far-side x offset
    static constexpr float SLOT3_Y_UP     = 600.0f;     // UU upfield from ball

    static constexpr float TARGET_FALLOFF  = 3000.0f;
    static constexpr float CENTER_BALL_EPS = 500.0f;

    // Overlap penalty (same-side stacking near ball carrier)
    static constexpr float OVERLAP_LATERAL_MAX = 1500.0f;
    static constexpr float OVERLAP_PENALTY     = 0.25f;
    static constexpr float OVERLAP_Y_WINDOW    = 2000.0f;

    // Gates
    static constexpr float MIN_GAME_SIZE     = 4;
    static constexpr float POSSESSION_RADIUS = 1500.0f;

    static constexpr float BASE_SCALE = 0.4f;

    inline static std::atomic<long long> s_rewardMilli{0};
    inline static std::atomic<long long> s_penaltyMilli{0};
    inline static std::atomic<int> s_supportFrames{0};
    inline static std::atomic<int> s_slot1Frames{0};
    inline static std::atomic<int> s_slot2Frames{0};
    inline static std::atomic<int> s_slot3Frames{0};
    inline static std::atomic<int> s_overlapFrames{0};

    enum class SimpleRole { CLOSEST, MIDDLE, FURTHEST };

    SimpleRole DetermineRole(const Player& player, const GameState& state) const {
        float distToBall = (player.pos - state.ball.pos).Length();
        std::vector<float> teammateDists;
        for (const auto& p : state.players) {
            if (p.team == player.team && p.carId != player.carId) {
                teammateDists.push_back((p.pos - state.ball.pos).Length());
            }
        }
        if (teammateDists.empty()) return SimpleRole::CLOSEST;
        std::sort(teammateDists.begin(), teammateDists.end());
        if (distToBall < teammateDists[0]) return SimpleRole::CLOSEST;
        if (teammateDists.size() >= 2 && distToBall > teammateDists[1]) return SimpleRole::FURTHEST;
        return SimpleRole::MIDDLE;
    }

    bool DoesTeamHavePossession(const Player& player, const GameState& state) const {
        float closestDist = 1e10f;
        Team closestTeam = Team::BLUE;
        for (const auto& p : state.players) {
            float dist = (p.pos - state.ball.pos).Length();
            if (dist < closestDist) { closestDist = dist; closestTeam = p.team; }
        }
        return closestTeam == player.team && closestDist < POSSESSION_RADIUS;
    }

public:
    struct Metrics {
        double totalReward  = 0.0;
        double totalPenalty = 0.0;
        int supportFrames   = 0;
        int slot1Frames     = 0;
        int slot2Frames     = 0;
        int slot3Frames     = 0;
        int overlapFrames   = 0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.totalReward   = double(s_rewardMilli.exchange(0))  / 1000.0;
        m.totalPenalty  = double(s_penaltyMilli.exchange(0)) / 1000.0;
        m.supportFrames = s_supportFrames.exchange(0);
        m.slot1Frames   = s_slot1Frames.exchange(0);
        m.slot2Frames   = s_slot2Frames.exchange(0);
        m.slot3Frames   = s_slot3Frames.exchange(0);
        m.overlapFrames = s_overlapFrames.exchange(0);
        return m;
    }

    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (state.players.size() < static_cast<int>(MIN_GAME_SIZE)) return 0.0f;
        if (!DoesTeamHavePossession(player, state)) return 0.0f;
        if (DetermineRole(player, state) != SimpleRole::MIDDLE) return 0.0f;
        if (!RewardMath::IsInOpponentHalf(state.ball.pos, player.team)) return 0.0f;

        const float oppGoalY   = (player.team == Team::BLUE) ? 5120.0f : -5120.0f;
        const float attackSign = (player.team == Team::BLUE) ? 1.0f : -1.0f;
        const float ballX      = state.ball.pos.x;
        const float ballY      = state.ball.pos.y;

        // Far-side sign: opposite from ball; ties broken by player's current x side
        float farSign;
        if (std::fabs(ballX) < CENTER_BALL_EPS) {
            farSign = (player.pos.x >= 0.0f) ? -1.0f : 1.0f;
        } else {
            farSign = (ballX > 0.0f) ? -1.0f : 1.0f;
        }

        // Slot 1: Far Post — near far goal post, just in front of goal line
        float s1x = RS_CLAMP(farSign * SLOT1_X, -3800.0f, 3800.0f);
        float s1y = RS_CLAMP(oppGoalY - attackSign * SLOT1_Y_BACK, -4900.0f, 4900.0f);

        // Slot 2: Central Penalty Spot — dead center, ~1000 UU from goal
        float s2x = 0.0f;
        float s2y = RS_CLAMP(oppGoalY - attackSign * SLOT2_Y_BACK, -4900.0f, 4900.0f);

        // Slot 3: Wide Far-Side Diagonal — opposite side, upfield from ball
        float s3x = RS_CLAMP(farSign * SLOT3_X, -3800.0f, 3800.0f);
        float s3y = RS_CLAMP(ballY + attackSign * SLOT3_Y_UP, -4900.0f, 4900.0f);

        // 2D proximity to each slot (ignore Z)
        auto prox = [&](float tx, float ty) {
            float dx = player.pos.x - tx;
            float dy = player.pos.y - ty;
            return RS_CLAMP(1.0f - std::sqrt(dx * dx + dy * dy) / TARGET_FALLOFF, 0.0f, 1.0f);
        };

        float p1 = prox(s1x, s1y);
        float p2 = prox(s2x, s2y);
        float p3 = prox(s3x, s3y);

        float bestScore;
        int bestSlot;
        if (p1 >= p2 && p1 >= p3) { bestScore = p1; bestSlot = 1; }
        else if (p2 >= p3)         { bestScore = p2; bestSlot = 2; }
        else                        { bestScore = p3; bestSlot = 3; }

        float reward = BASE_SCALE * bestScore;

        // Overlap penalty: same-side AND close to ball carrier (stacking is still bad)
        float overlap = 0.0f;
        const bool sameSide         = (ballX * player.pos.x > 0.0f) && std::fabs(ballX) > CENTER_BALL_EPS;
        const float lateralGap      = std::fabs(player.pos.x - ballX);
        const float longitudinalGap = std::fabs(player.pos.y - ballY);

        if (sameSide && lateralGap < OVERLAP_LATERAL_MAX && longitudinalGap < OVERLAP_Y_WINDOW) {
            float strength = 1.0f - (lateralGap / OVERLAP_LATERAL_MAX);
            overlap = -OVERLAP_PENALTY * strength;
            s_overlapFrames.fetch_add(1);
            s_penaltyMilli.fetch_add(static_cast<long long>(std::fabs(overlap) * 1000.0f));
        }

        // Metrics
        s_supportFrames.fetch_add(1);
        if (bestSlot == 1)      s_slot1Frames.fetch_add(1);
        else if (bestSlot == 2) s_slot2Frames.fetch_add(1);
        else                    s_slot3Frames.fetch_add(1);
        if (reward > 0.0f) s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

        return reward + overlap;
    }
};

// ============================================================================
// DeepBackPostCoverageReward - 3v3+ Back-Lane Defense
// 2026-04-18 | NJH
//
// Rewards the 3rd defender (furthest from ball) in 3v3+ for holding deep
// defensive position on the opposite x-side from the attacking ball,
// covering the back-post lane to prevent back-door breakaways.
//
// Activation gates: 3v3+, opponent possession in attacking half, FURTHEST role, own half.
// ============================================================================
class DeepBackPostCoverageReward : public Reward {
private:
    // Target position offsets for back-post coverage
    static constexpr float BACK_POST_LATERAL  = 2000.0f;    // |x| of ideal deep defender slot
    static constexpr float BACK_POST_FALLOFF  = 2500.0f;    // dist at which reward -> 0
    static constexpr float ACTIVE_THREAT_RANGE = 4500.0f;   // only fire if nearest attacker within this

    // Gates
    static constexpr float MIN_GAME_SIZE = 6;               // 3v3 (6 players)
    static constexpr float POSSESSION_RADIUS = 1500.0f;
    static constexpr float BASE_SCALE_BACK = 0.25f;

    inline static std::atomic<long long> s_rewardMilli{0};
    inline static std::atomic<int> s_backPostFrames{0};
    inline static std::atomic<int> s_coverageOppositeFrames{0};

    enum class SimpleRole { CLOSEST, MIDDLE, FURTHEST };

    SimpleRole DetermineRole(const Player& player, const GameState& state) const {
        float distToBall = (player.pos - state.ball.pos).Length();
        std::vector<float> teammateDists;
        for (const auto& p : state.players) {
            if (p.team == player.team && p.carId != player.carId) {
                teammateDists.push_back((p.pos - state.ball.pos).Length());
            }
        }
        if (teammateDists.empty()) return SimpleRole::CLOSEST;
        std::sort(teammateDists.begin(), teammateDists.end());
        if (distToBall < teammateDists[0]) return SimpleRole::CLOSEST;
        if (teammateDists.size() >= 2 && distToBall > teammateDists[1]) return SimpleRole::FURTHEST;
        return SimpleRole::MIDDLE;
    }

    bool IsOpponentInAttackingHalf(const Player& player, const GameState& state) const {
        for (const auto& p : state.players) {
            if (p.team == player.team) continue;  // Only check opponents
            if (RewardMath::IsInOpponentHalf(p)) return true;  // Opponent in attacking half
        }
        return false;
    }

    bool IsPlayerInOwnHalf(const Player& player) const {
        return RewardMath::IsInOwnHalf(player);
    }

    const Player* GetNearestAttacker(const Player& player, const GameState& state) const {
        const Player* nearest = nullptr;
        float nearestDist = FLT_MAX;
        for (const auto& p : state.players) {
            if (p.team == player.team) continue;
            float dist = (player.pos - p.pos).Length();
            if (dist < nearestDist) { nearestDist = dist; nearest = &p; }
        }
        return nearest;
    }

public:
    struct Metrics {
        double totalReward = 0.0;
        int backPostFrames = 0;
        int coverageOppositeFrames = 0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.totalReward          = double(s_rewardMilli.exchange(0)) / 1000.0;
        m.backPostFrames       = s_backPostFrames.exchange(0);
        m.coverageOppositeFrames = s_coverageOppositeFrames.exchange(0);
        return m;
    }

    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only 3v3+ (6+ players)
        if (state.players.size() < static_cast<int>(MIN_GAME_SIZE)) return 0.0f;

        // Only when defending (opponent has possession in attacking half)
        if (!IsOpponentInAttackingHalf(player, state)) return 0.0f;

        // Only FURTHEST role (3rd defender)
        if (DetermineRole(player, state) != SimpleRole::FURTHEST) return 0.0f;

        // Only in own half
        if (!IsPlayerInOwnHalf(player)) return 0.0f;

        // Only fire if nearest attacker is within threat range
        const Player* nearestAttacker = GetNearestAttacker(player, state);
        if (!nearestAttacker) return 0.0f;
        if ((player.pos - nearestAttacker->pos).Length() > ACTIVE_THREAT_RANGE) return 0.0f;

        // Determine own and opponent goal positions
        const float defendSign = (player.team == Team::BLUE) ? -1.0f : 1.0f;
        const float ownGoalY = defendSign * 4500.0f;

        // Target: opposite x-side from nearest attacker, deep in own half
        float targetX = (nearestAttacker->pos.x > 0.0f) ? -BACK_POST_LATERAL : +BACK_POST_LATERAL;
        float targetY = ownGoalY + defendSign * 500.0f;  // Slightly forward from back wall
        targetX = RS_CLAMP(targetX, -3500.0f, +3500.0f);

        // 2D distance to target
        float dx = player.pos.x - targetX;
        float dy = player.pos.y - targetY;
        float distToTarget = std::sqrt(dx * dx + dy * dy);

        float proximityScore = RS_CLAMP(1.0f - distToTarget / BACK_POST_FALLOFF, 0.0f, 1.0f);
        float reward = BASE_SCALE_BACK * proximityScore;

        // Metrics: track opposite-side coverage
        s_backPostFrames.fetch_add(1);
        const bool oppositeXSide = (nearestAttacker->pos.x * player.pos.x) <= 0.0f;
        if (oppositeXSide) s_coverageOppositeFrames.fetch_add(1);
        if (reward > 0.0f) s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

        return reward;
    }
};

// ============================================================================
// KickoffSupportBoostReward
// Encourages 2nd+ players to grab corner/mid boost pads during kickoff in 2v2+
// - Targets only support players (not closest to ball) during kickoff phase
// - Rewards proximity to and grabbing corner or mid-field boost pads
// - Only applies in 2v2+ modes; skips 1v1
// ============================================================================
class KickoffSupportBoostReward : public Reward {
private:
    const float KICKOFF_RADIUS = 300.0f;           // Ball distance to detect kickoff
    const float MAX_BALL_SPEED_KICKOFF = 500.0f;   // Max ball speed during kickoff
    const float BOOST_GRAB_MIN_DELTA = 30.0f;      // Minimum boost gain to count as grab
    const float MAX_BOOST_SEARCH_DIST = 5000.0f;   // Search radius for boost pads

    // Detect kickoff phase: ball near center with low velocity
    bool IsInKickoffPhase(const GameState& state) const {
        float ballDist = state.ball.pos.Length();
        float ballSpeed = state.ball.vel.Length();
        return (ballDist < KICKOFF_RADIUS) && (ballSpeed < MAX_BALL_SPEED_KICKOFF);
    }

    // Identify corner boost pads (large pads at |X| >= 3000, |Y| >= 4000)
    bool IsCornerBoostPad(const Vec& pos) const {
        return std::abs(pos.x) >= 3000.0f && std::abs(pos.y) >= 4000.0f;
    }

    // Identify mid-field boost pads (small pads in center strip)
    bool IsMidBoostPad(const Vec& pos) const {
        return std::abs(pos.y) <= 2000.0f && std::abs(pos.x) <= 3000.0f;
    }

    // Check if position is in our defensive half
    bool IsInOurDefensiveHalf(const Vec& pos, Team playerTeam) const {
        if (playerTeam == Team::BLUE) {
            return pos.y < 0.0f;  // Blue defends negative Y
        } else {
            return pos.y > 0.0f;  // Orange defends positive Y
        }
    }

    // Determine if player is the closest to ball (first player)
    bool IsClosestToBall(const Player& player, const GameState& state) const {
        float ourDist = (player.pos - state.ball.pos).Length();
        for (const auto& p : state.players) {
            if (p.team != player.team) continue;  // Only check teammates
            if (p.carId == player.carId) continue; // Skip self
            float theirDist = (p.pos - state.ball.pos).Length();
            if (theirDist < ourDist) {
                return false;  // Another teammate is closer
            }
        }
        return true;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        // No state needed
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Skip 1v1 (only reward in 2v2+)
        if (state.players.size() < 4) {
            return 0.0f;
        }

        // Only reward during kickoff phase
        if (!IsInKickoffPhase(state)) {
            return 0.0f;
        }

        // Only reward support players (not first player going for ball)
        if (IsClosestToBall(player, state)) {
            return 0.0f;
        }

        // Find nearest corner or mid boost pad in our defensive half
        float closestBoostDist = 1e10f;
        Vec closestBoostPos = Vec(0.0f, 0.0f, 0.0f);

        for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
            Vec padPos = CommonValues::BOOST_LOCATIONS[i];

            // Filter: only corner or mid pads
            bool isCorner = IsCornerBoostPad(padPos);
            bool isMid = IsMidBoostPad(padPos);
            if (!(isCorner || isMid)) {
                continue;
            }

            // Filter: only in our defensive half
            if (!IsInOurDefensiveHalf(padPos, player.team)) {
                continue;
            }

            // Calculate distance to this pad
            float dist = (padPos - player.pos).Length();
            if (dist < closestBoostDist) {
                closestBoostDist = dist;
                closestBoostPos = padPos;
            }
        }

        // No valid boost pad found
        if (closestBoostDist >= 1e10f) {
            return 0.0f;
        }

        // Reward for grabbing corner/mid boost pad (large boost gain)
        if (player.boost > player.prev->boost) {
            float boostGain = player.boost - player.prev->boost;
            if (boostGain > BOOST_GRAB_MIN_DELTA) {
                return 3.0f;  // Reward for grabbing corner/mid pad (100+ boost or 12 small pad)
            }
        }

        // Reward for positioning toward boost pad
        if (closestBoostDist <= MAX_BOOST_SEARCH_DIST) {
            float proximityReward = RS_CLAMP(1.0f - (closestBoostDist / MAX_BOOST_SEARCH_DIST), 0.0f, 1.0f);
            return proximityReward * 0.5f;  // Position reward (0.0 to 0.5)
        }

        return 0.0f;
    }
};


// ============================================================================
// AntiLayeringPenalty
// ============================================================================
// Penalizes 2v2/3v3 teams when two or more teammates occupy the same Y-band
// (±LAYER_Y_THRESHOLD units). "Layers" are catastrophic: one challenge beats
// two players simultaneously, leaving the remaining player in a numerical deficit.
//
// Exemption: if either player in the pair is within GOAL_EXEMPT_DIST of their
// own goal line (Blue y=-5120, Orange y=+5120), that pair is skipped. Goalkeeping
// or emergency retreating near the goal naturally creates same-Y clustering.
//
// Applied per-player, per-step: fires every frame a player is layered.
// Not ZeroSum — both layering players are penalized independently.
//
// W&B: Positioning/AntiLayering/PenaltyFrames, /AvgPairsPerFrame
// ============================================================================
class AntiLayeringPenalty : public Reward {
private:
    static constexpr float LAYER_Y_THRESHOLD = 200.0f;   // Y distance that counts as layering
    static constexpr float GOAL_EXEMPT_DIST  = 1500.0f;  // abs(y - ownGoalY) below this = exempt
    static constexpr float PENALTY_PER_STEP  = -0.1f;    // per step while layered

    inline static std::atomic<int> s_penaltyFrames { 0 };
    inline static std::atomic<int> s_pairsSum      { 0 };

public:
    struct Metrics {
        int    penaltyFrames  = 0;
        int    pairsSum       = 0;  // total layering pairs detected (÷penaltyFrames = avg overlap)
    };
    static Metrics GetAndResetMetrics() {
        return { s_penaltyFrames.exchange(0), s_pairsSum.exchange(0) };
    }

    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        // Only relevant in 2v2 or 3v3
        int teamCount = 0;
        for (const auto& p : state.players)
            if (p.team == player.team) teamCount++;
        if (teamCount < 2) return 0.0f;

        // Own goal Y: Blue defends y=-5120, Orange defends y=+5120
        float ownGoalY = (player.team == Team::BLUE) ? -5120.0f : 5120.0f;

        // Exempt this player if they are near their own goal
        if (fabsf(player.pos.y - ownGoalY) < GOAL_EXEMPT_DIST) return 0.0f;

        // Count layering pairs for this player
        int layeringPairs = 0;
        for (const auto& tm : state.players) {
            if (tm.team != player.team || tm.carId == player.carId || tm.isDemoed) continue;

            // Exempt if the teammate is near own goal (they're in goal position, not layering)
            if (fabsf(tm.pos.y - ownGoalY) < GOAL_EXEMPT_DIST) continue;

            if (fabsf(player.pos.y - tm.pos.y) < LAYER_Y_THRESHOLD) {
                layeringPairs++;
            }
        }

        if (layeringPairs > 0) {
            s_penaltyFrames.fetch_add(1);
            s_pairsSum.fetch_add(layeringPairs);
            return PENALTY_PER_STEP;
        }

        return 0.0f;
    }
};


// ============================================================================
// DefensiveZoneOverlapPenalty
// ============================================================================
// When the ball is in a team's defensive third, the defensive zone is split
// into three equal X-axis sections (left / center / right). No two teammates
// should occupy the same section simultaneously — that creates defensive overlap:
// one attacking play beats both players at once, leaving the goal exposed.
//
// This is the X-axis counterpart to AntiLayeringPenalty (which covers Y-axis).
//
// Sections (field width ±4096 → thirds at ±1365 UU):
//   Left:   x < -1365
//   Center: -1365 <= x <= 1365
//   Right:  x > 1365
//
// Exemptions:
//   - Ball not in own defensive third (active only when defending)
//   - Either player within EMERGENCY_EXEMPT of goal center (simultaneous goal defence)
//
// W&B: Penalty/DefZoneOverlap/PenaltyFrames, /AvgPairsPerFrame
// ============================================================================
class DefensiveZoneOverlapPenalty : public Reward {
private:
    static constexpr float DEFENSIVE_THIRD_Y = 3413.0f;  // 10240 / 3
    static constexpr float X_ZONE_BOUNDARY   = 1365.0f;  // 4096 / 3
    static constexpr float EMERGENCY_EXEMPT  = 600.0f;   // both players near net = OK
    static constexpr float PENALTY_PER_STEP  = -0.1f;

    inline static std::atomic<int> s_penaltyFrames { 0 };
    inline static std::atomic<int> s_pairsSum      { 0 };

    int GetXZone(float x) const {
        if (x < -X_ZONE_BOUNDARY) return 0;  // left
        if (x >  X_ZONE_BOUNDARY) return 2;  // right
        return 1;                             // center
    }

public:
    struct Metrics { int penaltyFrames = 0; int pairsSum = 0; };
    static Metrics GetAndResetMetrics() {
        return { s_penaltyFrames.exchange(0), s_pairsSum.exchange(0) };
    }

    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        // 2v2/3v3 only
        int teamCount = 0;
        for (const auto& p : state.players)
            if (p.team == player.team) teamCount++;
        if (teamCount < 2) return 0.0f;

        // Only when ball is in own defensive third
        float ownGoalY = (player.team == Team::BLUE) ? -5120.0f : 5120.0f;
        if (fabsf(state.ball.pos.y - ownGoalY) > DEFENSIVE_THIRD_Y) return 0.0f;

        // Emergency exemption: this player is pressed against own net
        Vec netCenter(0.0f, ownGoalY, 0.0f);
        if ((player.pos - netCenter).Length() < EMERGENCY_EXEMPT) return 0.0f;

        int myZone = GetXZone(player.pos.x);

        int layeringPairs = 0;
        for (const auto& tm : state.players) {
            if (tm.team != player.team || tm.carId == player.carId || tm.isDemoed) continue;
            // Exempt this teammate if they're in emergency goal position
            if ((tm.pos - netCenter).Length() < EMERGENCY_EXEMPT) continue;
            if (GetXZone(tm.pos.x) == myZone) layeringPairs++;
        }

        if (layeringPairs > 0) {
            s_penaltyFrames.fetch_add(1);
            s_pairsSum.fetch_add(layeringPairs);
            return PENALTY_PER_STEP;
        }
        return 0.0f;
    }
};


// ============================================================================
// OverflipPenalty
// ============================================================================
// Penalizes non-first-man players who flip toward the ball when they are already
// too close to the play. "Over-flipping" is committing a rotation to a zone that
// a teammate is already covering, wasting a flip and forcing an overcommit.
//
// Fires on the frame a new flip is detected when ALL of the following are true:
//   1. Player is NOT the closest to ball on their team (not first man)
//   2. Distance to ball < MAX_FLIP_DIST (within "too close" range)
//   3. Flip velocity is aimed toward ball (alignment > MIN_BALL_ALIGN)
//
// One-shot penalty per flip event (not continuous).
//
// W&B: Penalty/Overflip/Count, /AvgDistToBall
// ============================================================================
class OverflipPenalty : public Reward {
private:
    static constexpr float MAX_FLIP_DIST   = 3000.0f;  // within this of ball = too close to flip
    static constexpr float MIN_BALL_ALIGN  = 0.3f;     // cos(angle) toward ball to count
    static constexpr float PENALTY         = -0.5f;    // one-shot on the flip frame

    mutable std::map<uint32_t, bool> prevFlipping;

    inline static std::atomic<int> s_flipCount { 0 };
    inline static std::atomic<int> s_distSum   { 0 };  // UU, sum for avg

public:
    struct Metrics {
        int    flipCount    = 0;
        double avgDistToBall = 0.0;
    };
    static Metrics GetAndResetMetrics() {
        int count   = s_flipCount.exchange(0);
        int distSum = s_distSum.exchange(0);
        return { count, count > 0 ? double(distSum) / count : 0.0 };
    }

    virtual void Reset(const GameState& initialState) override { prevFlipping.clear(); }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        uint32_t id = player.carId;
        bool wasFlipping  = prevFlipping.count(id) ? prevFlipping[id] : false;
        bool flipStarted  = (!wasFlipping && player.isFlipping && !player.isOnGround);
        prevFlipping[id]  = player.isFlipping;

        if (!flipStarted) return 0.0f;

        // Is this player the first man (closest to ball on team)?
        float myDist = (player.pos - state.ball.pos).Length();
        bool isFirstMan = true;
        for (const auto& tm : state.players) {
            if (tm.team != player.team || tm.carId == player.carId || tm.isDemoed) continue;
            if ((tm.pos - state.ball.pos).Length() < myDist) { isFirstMan = false; break; }
        }
        if (isFirstMan) return 0.0f;  // first man can flip freely

        // Not first man: check distance and direction
        if (myDist > MAX_FLIP_DIST) return 0.0f;

        Vec toBall = (state.ball.pos - player.pos).Normalized();
        float align = player.vel.Normalized().Dot(toBall);
        if (align < MIN_BALL_ALIGN) return 0.0f;  // not heading toward ball

        s_flipCount.fetch_add(1);
        s_distSum.fetch_add(static_cast<int>(myDist));
        return PENALTY;
    }
};


// ============================================================================
// DiagonalZoneCoverageReward
// ============================================================================
// Trains the third man (player furthest from ball on their team) to position in
// the quadrant diagonal to where the ball is. The field is split into 4 quadrants
// by the center axes. The third man in the diagonal quadrant can cover any clear
// or counter across the entire field.
//
//   Ball in top-right  (+x, +y)  →  third man targets bottom-left  (-x, -y)
//   Ball in top-left   (-x, +y)  →  third man targets bottom-right (+x, -y)
//   etc.
//
// Partial credit (30%) for being correct on one axis only.
//
// Exemptions:
//   - Ball within QUADRANT_THRESHOLD of either center axis (ambiguous quadrant)
//   - Ball in own defensive third (defer to defensive zone positioning instead)
//   - Player within MIN_COVERAGE_DIST of ball (too close to be "third man coverage")
//
// W&B: Positioning/DiagonalZone/CorrectFrames, /TotalEligibleFrames
// ============================================================================
class DiagonalZoneCoverageReward : public Reward {
private:
    static constexpr float MIN_COVERAGE_DIST  = 1500.0f;  // must be this far from ball
    static constexpr float QUADRANT_THRESHOLD = 500.0f;   // ball must be this far from axis
    static constexpr float DEFENSIVE_THIRD_Y  = 3413.0f;  // skip when ball in own def third
    static constexpr float BASE_REWARD        = 1.0f;
    static constexpr float PARTIAL_REWARD     = 0.3f;     // correct on one axis only

    inline static std::atomic<int> s_correctFrames { 0 };
    inline static std::atomic<int> s_totalFrames   { 0 };

public:
    struct Metrics { int correctFrames = 0; int totalFrames = 0; };
    static Metrics GetAndResetMetrics() {
        return { s_correctFrames.exchange(0), s_totalFrames.exchange(0) };
    }

    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        // 2v2/3v3 only
        int teamCount = 0;
        for (const auto& p : state.players)
            if (p.team == player.team) teamCount++;
        if (teamCount < 2) return 0.0f;

        // Skip if ball is in an ambiguous quadrant (near center axes)
        if (fabsf(state.ball.pos.x) < QUADRANT_THRESHOLD ||
            fabsf(state.ball.pos.y) < QUADRANT_THRESHOLD) return 0.0f;

        // Skip if ball is in own defensive third (DefensiveZoneOverlapPenalty covers this)
        float ownGoalY = (player.team == Team::BLUE) ? -5120.0f : 5120.0f;
        if (fabsf(state.ball.pos.y - ownGoalY) < DEFENSIVE_THIRD_Y) return 0.0f;

        // Only applies to the third man (furthest from ball on team)
        float myDist = (player.pos - state.ball.pos).Length();
        for (const auto& tm : state.players) {
            if (tm.team != player.team || tm.carId == player.carId || tm.isDemoed) continue;
            if ((tm.pos - state.ball.pos).Length() > myDist) return 0.0f;  // someone is further
        }

        // Must be far enough from ball to count as coverage (not in the immediate play)
        if (myDist < MIN_COVERAGE_DIST) return 0.0f;

        // Determine diagonal target quadrant
        float idealX = (state.ball.pos.x > 0.0f) ? -1.0f : 1.0f;
        float idealY = (state.ball.pos.y > 0.0f) ? -1.0f : 1.0f;

        bool correctX = (player.pos.x * idealX > 0.0f);
        bool correctY = (player.pos.y * idealY > 0.0f);

        s_totalFrames.fetch_add(1);

        if (correctX && correctY) {
            s_correctFrames.fetch_add(1);
            return BASE_REWARD;
        }
        if (correctX || correctY) return PARTIAL_REWARD;

        return 0.0f;
    }
};


// ============================================================================
// NoMansLandPenalty
// ============================================================================
// Penalizes non-first-man players for sitting in the open center field ("no
// man's land") instead of the safe zone near the side walls.
//
// "Safe zone" per the transcript: side walls + roughly 1/4 of field width
// toward center. No-man's-land = the remaining center strip (|x| < 2000).
//
// A 0.3-second grace timer prevents penalizing players who are simply
// transiting through the center on their way to a wall.
//
// Exemptions:
//   - Player is first man (closest to ball on team)
//   - Player within GOAL_EXEMPT_DIST of own goal (defending)
//   - Player within BALL_EXEMPT_DIST of ball (in the immediate play)
//   - 1v1 (no concept of safe zone in 1v1)
//
// W&B: Penalty/NoMansLand/PenaltyFrames
// ============================================================================
class NoMansLandPenalty : public Reward {
private:
    static constexpr float NO_MANS_X_THRESHOLD = 2000.0f;  // |x| < this = no-man's-land
    static constexpr float GOAL_EXEMPT_DIST    = 2000.0f;  // near own goal = exempt
    static constexpr float BALL_EXEMPT_DIST    = 1500.0f;  // near ball = in the play, exempt
    static constexpr float GRACE_SECS         = 0.3f;      // transit grace before penalty
    static constexpr float PENALTY_PER_STEP   = -0.08f;

    mutable std::map<uint32_t, float> nmlTimer;  // seconds spent in no-man's-land

    inline static std::atomic<int> s_penaltyFrames { 0 };

public:
    struct Metrics { int penaltyFrames = 0; };
    static Metrics GetAndResetMetrics() {
        return { s_penaltyFrames.exchange(0) };
    }

    virtual void Reset(const GameState& initialState) override { nmlTimer.clear(); }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        // 2v2/3v3 only
        int teamCount = 0;
        for (const auto& p : state.players)
            if (p.team == player.team) teamCount++;
        if (teamCount < 2) return 0.0f;

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
        uint32_t id = player.carId;

        // Exempt if first man (closest to ball on team)
        float myDist = (player.pos - state.ball.pos).Length();
        bool isFirstMan = true;
        for (const auto& tm : state.players) {
            if (tm.team != player.team || tm.carId == player.carId || tm.isDemoed) continue;
            if ((tm.pos - state.ball.pos).Length() < myDist) { isFirstMan = false; break; }
        }
        if (isFirstMan) { nmlTimer[id] = 0.0f; return 0.0f; }

        // Exempt if near own goal
        float ownGoalY = (player.team == Team::BLUE) ? -5120.0f : 5120.0f;
        if (fabsf(player.pos.y - ownGoalY) < GOAL_EXEMPT_DIST) { nmlTimer[id] = 0.0f; return 0.0f; }

        // Exempt if close to ball (in the immediate play)
        if (myDist < BALL_EXEMPT_DIST) { nmlTimer[id] = 0.0f; return 0.0f; }

        // Check if in no-man's-land
        if (fabsf(player.pos.x) >= NO_MANS_X_THRESHOLD) { nmlTimer[id] = 0.0f; return 0.0f; }

        // Accumulate grace timer
        float& timer = nmlTimer[id];
        timer += dt;

        if (timer > GRACE_SECS) {
            s_penaltyFrames.fetch_add(1);
            return PENALTY_PER_STEP;
        }

        return 0.0f;
    }
};

} // namespace RLGC
