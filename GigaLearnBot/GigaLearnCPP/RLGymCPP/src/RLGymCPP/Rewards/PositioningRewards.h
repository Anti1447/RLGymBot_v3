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
    const float MIN_SUPPORT_DIST = 1000.0f;      // Too close = 1st man territory
    const float OPTIMAL_SUPPORT_DIST = 2300.0f;  // Sweet spot for support
    const float MAX_SUPPORT_DIST = 3600.0f;      // Too far = passive

    // Possession and role detection
    const float MIN_GAME_SIZE = 4;  // Minimum players for team play (2v2)
    static constexpr float POSSESSION_RADIUS = 1500.0f; // UU — if closest player to ball is a teammate within this, team has possession

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

    // Check if team has possession: closest player to ball is a teammate within POSSESSION_RADIUS.
    // Proximity-only — no velocity check. The old "moving toward ball" dot-product broke during
    // dribbling (ball on roof of car → dirToBall is straight up, car velocity is horizontal → dot≈0).
    bool DoesTeamHavePossession(const Player& player, const GameState& state) const {
        float closestDist = 1e10f;
        Team  closestTeam = Team::BLUE;

        for (const auto& p : state.players) {
            float dist = (p.pos - state.ball.pos).Length();
            if (dist < closestDist) {
                closestDist = dist;
                closestTeam = p.team;
            }
        }

        return closestTeam == player.team && closestDist < POSSESSION_RADIUS;
    }

    // Check if player is upfield: farther along the attack axis (Y) than the ball.
    // Uses Y-axis directly rather than 3D distance to goal — avoids false positives when
    // ball is airborne (high Z inflates 3D distance) or player is wide on a side wall.
    bool IsPositionedUpfield(const Player& player, const GameState& state) const {
        if (player.team == Team::BLUE) {
            return player.pos.y > state.ball.pos.y;   // Blue attacks +Y
        } else {
            return player.pos.y < state.ball.pos.y;   // Orange attacks -Y
        }
    }

    // Returns true when ball is deep in the opponent's half AND near a side wall —
    // the "corner play" scenario where second man should collapse toward the goal mouth.
    bool IsBallInAttackingCorner(const Player& player, const GameState& state) const {
        float attackY = (player.team == Team::BLUE) ? state.ball.pos.y : -state.ball.pos.y;
        return attackY > 2800.0f && fabsf(state.ball.pos.x) > 2400.0f;
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

        // Base reward: tapers around optimal ball distance (peak 0.4 at OPTIMAL_SUPPORT_DIST)
        float deviation = std::abs(distToBall - OPTIMAL_SUPPORT_DIST);
        float reward = 0.4f * (1.0f - (deviation / OPTIMAL_SUPPORT_DIST));
        reward = RS_CLAMP(reward, 0.0f, 0.4f);

        // Centrality bonus: nudge support toward the center of the field when in the
        // attacking half — side-wall parking is worthless for passing/scoring.
        float attackY = (player.team == Team::BLUE) ? player.pos.y : -player.pos.y;
        if (attackY > 0.0f) {
            float centralityBonus = RS_CLAMP(1.0f - fabsf(player.pos.x) / 2500.0f, 0.0f, 1.0f);
            reward += 0.3f * centralityBonus;

            // X-side opposition bonus: spread horizontally if first man is already on a wing.
            // Better passing lanes when support is on opposite side of field from first man.
            const Player* firstManX = nullptr;
            float fmClosestDistX = 1e10f;
            for (const auto& p : state.players) {
                if (p.team == player.team && p.carId != player.carId) {
                    float d = (p.pos - state.ball.pos).Length();
                    if (d < fmClosestDistX) { fmClosestDistX = d; firstManX = &p; }
                }
            }
            if (firstManX) {
                float xAlignment = player.pos.x * firstManX->pos.x;  // Negative = opposite sides
                if (xAlignment < 0.0f) {  // On opposite sides
                    float oppositeSideBonus = RS_CLAMP(fabsf(player.pos.x) / 2500.0f, 0.0f, 1.0f);
                    reward += 0.1f * oppositeSideBonus;  // Light bonus, scales with distance from center
                }
            }
        }

        // Corner play bonus: when first man is in the opponent's corner, pull the second
        // man toward the goal mouth — ready to tap in any centering pass or deflection.
        // Optimal distance is around 750 units from goal (close enough to finish, far enough to react).
        if (IsBallInAttackingCorner(player, state)) {
            Vec oppGoal = (player.team == Team::BLUE)
                           ? CommonValues::ORANGE_GOAL_CENTER
                           : CommonValues::BLUE_GOAL_CENTER;
            float distToGoalMouth = (player.pos - oppGoal).Length();
            float optimalDist = 750.0f;  // Ideal distance from goal
            float distanceError = fabsf(distToGoalMouth - optimalDist);
            float cornerBonus = 0.4f * RS_CLAMP(1.0f - distanceError / 750.0f, 0.0f, 1.0f);
            reward += cornerBonus;
        }

        // Lead-runner bonus: 2nd man is pushed further toward opp goal than the 1st man.
        // Encourages aggressive forward runs to get open for passes, not just ball-trailing.
        const Player* firstMan = nullptr;
        float fmClosestDist = 1e10f;
        for (const auto& p : state.players) {
            if (p.team == player.team && p.carId != player.carId) {
                float d = (p.pos - state.ball.pos).Length();
                if (d < fmClosestDist) { fmClosestDist = d; firstMan = &p; }
            }
        }
        if (firstMan) {
            float myAttackY = (player.team == Team::BLUE) ?  player.pos.y   : -player.pos.y;
            float fmAttackY = (player.team == Team::BLUE) ?  firstMan->pos.y : -firstMan->pos.y;
            float lead = myAttackY - fmAttackY;
            // Only apply lead bonus if NOT within 1000 units of opponent's goal
            // (prevents overextension and getting caught out defensively)
            if (lead > 0.0f && myAttackY < 1000.0f) {
                reward += 0.55f * RS_CLAMP(lead / 2000.0f, 0.0f, 1.0f);
            }
        }

        // Scale reward by team boost advantage and absolute boost level
        // - Boost advantage: ±200 divisor gives 0.5-1.5x scaling
        // - Absolute level: critical penalties if team is low on boost
        float ourTeamTotalBoost = 0.0f;
        float oppTeamTotalBoost = 0.0f;
        int teamSize = 0;

        for (const auto& p : state.players) {
            if (p.team == player.team) {
                ourTeamTotalBoost += p.boost;
                teamSize++;
            } else {
                oppTeamTotalBoost += p.boost;
            }
        }

        float avgOurBoost = ourTeamTotalBoost / (float)teamSize;
        float avgOppBoost = oppTeamTotalBoost / (float)(state.players.size() - teamSize);
        float boostAdvantage = avgOurBoost - avgOppBoost;  // Positive = we're ahead
        float ourTeamBoostPct = ourTeamTotalBoost / (100.0f * teamSize);  // 0.0 to 1.0

        // Scaling: advantage-based (1.0 when even, 0.5-1.5 range)
        float boostScale = RS_CLAMP(1.0f + (boostAdvantage / 200.0f), 0.5f, 1.5f);

        // Critical penalty: if our team is low on boost, reduce reward
        float lowBoostPenalty = 1.0f;
        if (ourTeamBoostPct < 0.3f) {
            lowBoostPenalty = 0.3f;  // Only 30% reward when critically low (<30% avg)
        } else if (ourTeamBoostPct < 0.5f) {
            lowBoostPenalty = 0.6f;  // 60% reward when moderate (30-50% avg)
        }

        // Final multiplier: combines advantage and absolute boost level
        float boostMultiplier = boostScale * lowBoostPenalty;

        // Apply boost scaling to final reward
        reward *= boostMultiplier;

        // Track metrics
        s_supportFrames.fetch_add(1);
        s_upfieldFrames.fetch_add(1);
        if (reward > 0.0f) {
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        }

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

} // namespace RLGC
