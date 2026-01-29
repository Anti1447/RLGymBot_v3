#pragma once

#include "Reward.h"
#include "../CommonValues.h"
#include <map>
#include <atomic>
#include <cmath>

namespace RLGC {

// ============================================================================
// SMART WALL PLAY REWARDS
// ============================================================================
// Teaches context-aware wall play decision-making with DENSE REWARDS:
// - CLEAR: Defensive wall clears away from own goal
// - SHOOT: Offensive wall shots toward opponent goal
// - OUTPLAY: Beat opponents challenging on wall
// - PASS: Wall touches that set up teammates
// - SHAPING: Continuous feedback for positioning and decision-making
// - PENALTY: Giving away possession (ball to opponent)
//
// DESIGN PHILOSOPHY:
// - Dense rewards: Provide continuous shaping signals every tick
// - Immediate feedback: Reward good positioning BEFORE the touch
// - Gradient-friendly: Smooth reward curves, no binary triggers
// - Progressive: Small rewards for partial success, big rewards for completion
// ============================================================================

// ============================================================================
// WallClearReward (DENSE VERSION)
// Rewards defensive wall clears with CONTINUOUS SHAPING:
// 1. Small reward for approaching wall in defensive zone (positioning)
// 2. Medium reward for being on wall with ball nearby (setup)
// 3. Large reward for successful clear (execution)
// ============================================================================
class WallClearReward : public Reward {
private:
    const float WALL_DIST_THRESHOLD = 400.0f;
    const float MIN_CLEAR_SPEED = 600.0f;  // Lowered threshold for more triggers
    const float DEFENSIVE_ZONE_Y = 2500.0f;  // Expanded zone

    // Dense shaping constants
    const float POSITIONING_REWARD = 0.02f;   // Per tick for good positioning
    const float SETUP_REWARD = 0.05f;         // Per tick for wall setup
    const float TOUCH_REWARD_BASE = 3.0f;     // Base clear reward (reduced but still significant)

    inline static std::atomic<int> s_clearCount{0};
    inline static std::atomic<long long> s_rewardMilli{0};

    bool IsOnWall(const Vec& pos) const {
        float distX = abs(pos.x) - CommonValues::SIDE_WALL_X;
        float distY = abs(pos.y) - CommonValues::BACK_WALL_Y;
        return (abs(distX) < WALL_DIST_THRESHOLD || abs(distY) < WALL_DIST_THRESHOLD);
    }

    bool IsInDefensiveZone(const Vec& pos, Team playerTeam) const {
        float ownGoalY = (playerTeam == Team::BLUE) ?
            -CommonValues::BACK_WALL_Y : CommonValues::BACK_WALL_Y;
        float distToOwnGoal = abs(pos.y - ownGoalY);
        return distToOwnGoal < DEFENSIVE_ZONE_Y;
    }

    // DENSE SHAPING: Reward for moving toward wall in defensive situations
    float GetPositioningReward(const Player& player, const GameState& state) const {
        if (!IsInDefensiveZone(state.ball.pos, player.team)) return 0.0f;
        if (!IsOnWall(state.ball.pos)) return 0.0f;

        float distToBall = (player.pos - state.ball.pos).Length();
        if (distToBall > 1200.0f) return 0.0f;  // Too far to be relevant

        // Moving toward ball on wall
        Vec toBall = (state.ball.pos - player.pos).Normalized();
        float movingToward = player.vel.Normalized().Dot(toBall);

        if (movingToward > 0.3f) {
            // Scale reward by proximity (closer = better)
            float proximityScale = 1.0f - RS_CLAMP(distToBall / 1200.0f, 0.0f, 1.0f);
            return POSITIONING_REWARD * proximityScale;
        }

        return 0.0f;
    }

    // DENSE SHAPING: Reward for being on wall with ball nearby
    float GetSetupReward(const Player& player, const GameState& state) const {
        if (!IsInDefensiveZone(state.ball.pos, player.team)) return 0.0f;
        if (!IsOnWall(state.ball.pos)) return 0.0f;

        float distToBall = (player.pos - state.ball.pos).Length();
        if (distToBall > 500.0f) return 0.0f;

        // Player on or very near wall
        float distX = abs(player.pos.x) - CommonValues::SIDE_WALL_X;
        float distY = abs(player.pos.y) - CommonValues::BACK_WALL_Y;
        float minWallDist = RS_MIN(abs(distX), abs(distY));

        if (minWallDist < WALL_DIST_THRESHOLD) {
            // Check if facing ball (ready to clear)
            Vec toBall = (state.ball.pos - player.pos).Normalized();
            float facing = player.rotMat.forward.Dot(toBall);

            if (facing > 0.3f) {
                float facingScale = RS_CLAMP((facing - 0.3f) / 0.7f, 0.0f, 1.0f);
                return SETUP_REWARD * facingScale;
            }
        }

        return 0.0f;
    }

public:
    struct Metrics {
        int clears = 0;
        double totalReward = 0.0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.clears = s_clearCount.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.totalReward = double(r) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;

        // DENSE SHAPING: Always provide feedback
        reward += GetPositioningReward(player, state);
        reward += GetSetupReward(player, state);

        // TOUCH REWARD: Successful clear execution
        if (player.ballTouchedStep && state.prev) {
            if (!IsOnWall(state.ball.pos)) goto end;
            if (!IsInDefensiveZone(state.ball.pos, player.team)) goto end;

            Vec ownGoal = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
            Vec oppGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

            Vec toOwnGoal = (ownGoal - state.ball.pos).Normalized();
            Vec ballVelDir = state.ball.vel.Normalized();
            float awayFromGoal = -ballVelDir.Dot(toOwnGoal);

            // SOFT THRESHOLD: Reward even weak clears (reduced from 0.3f)
            if (awayFromGoal < 0.1f) goto end;

            float ballSpeed = state.ball.vel.Length();
            if (ballSpeed < MIN_CLEAR_SPEED) goto end;

            // PROGRESSIVE REWARD: Start smaller, scale up
            float touchReward = TOUCH_REWARD_BASE;

            // Direction quality (smooth scaling)
            float directionScale = RS_CLAMP(awayFromGoal, 0.0f, 1.0f);
            touchReward *= (0.5f + 0.5f * directionScale);  // 0.5x to 1.0x multiplier

            // Speed bonus (smooth curve)
            float speedBonus = RS_CLAMP((ballSpeed - MIN_CLEAR_SPEED) / 1400.0f, 0.0f, 1.0f) * 2.0f;
            touchReward += speedBonus;

            // Direction bonus (toward opponent goal)
            Vec toOppGoal = (oppGoal - state.ball.pos).Normalized();
            float towardOppGoal = ballVelDir.Dot(toOppGoal);
            if (towardOppGoal > 0.3f) {  // Lowered threshold
                float dirBonus = RS_CLAMP((towardOppGoal - 0.3f) / 0.7f, 0.0f, 1.0f) * 1.5f;
                touchReward += dirBonus;
            }

            // Height bonus (smooth curve)
            float heightBonus = RS_CLAMP(state.ball.vel.z / 1000.0f, 0.0f, 1.0f) * 1.5f;
            touchReward += heightBonus;

            // Danger bonus (clearing from risky positions)
            float distToOwnGoal = (state.ball.pos - ownGoal).Length();
            float dangerBonus = RS_CLAMP((2000.0f - distToOwnGoal) / 2000.0f, 0.0f, 1.0f) * 2.0f;
            touchReward += dangerBonus;

            reward += touchReward;
            s_clearCount.fetch_add(1);
        }

        end:
        if (reward > 0.0f) {
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        }

        return reward;
    }
};

// ============================================================================
// WallShotReward
// Rewards offensive wall shots toward opponent goal
// - Detects wall touches in opponent half
// - Rewards shots on target
// - Bonus for power, accuracy, and scoring
// ============================================================================
class WallShotReward : public Reward {
private:
    const float WALL_DIST_THRESHOLD = 400.0f;
    const float MIN_SHOT_SPEED = 1000.0f;
    const float OFFENSIVE_ZONE_Y = 2000.0f;

    inline static std::atomic<int> s_shotCount{0};
    inline static std::atomic<int> s_goalCount{0};
    inline static std::atomic<long long> s_rewardMilli{0};

    bool IsOnWall(const Vec& pos) const {
        float distX = abs(pos.x) - CommonValues::SIDE_WALL_X;
        float distY = abs(pos.y) - CommonValues::BACK_WALL_Y;
        return (abs(distX) < WALL_DIST_THRESHOLD || abs(distY) < WALL_DIST_THRESHOLD);
    }

    bool IsInOffensiveZone(const Vec& pos, Team playerTeam) const {
        float oppGoalY = (playerTeam == Team::BLUE) ?
            CommonValues::BACK_WALL_Y : -CommonValues::BACK_WALL_Y;
        float distToOppGoal = abs(pos.y - oppGoalY);
        return distToOppGoal < OFFENSIVE_ZONE_Y;
    }

    bool IsOnTarget(const Vec& ballPos, const Vec& ballVel, Team playerTeam) const {
        Vec oppGoal = (playerTeam == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        Vec toGoal = (oppGoal - ballPos).Normalized();
        Vec velDir = ballVel.Normalized();

        float alignment = velDir.Dot(toGoal);
        return alignment > 0.6f;  // Reasonably on target
    }

public:
    struct Metrics {
        int shots = 0;
        int goals = 0;
        double totalReward = 0.0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.shots = s_shotCount.exchange(0);
        m.goals = s_goalCount.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.totalReward = double(r) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep || !state.prev) return 0.0f;

        // Must be on wall
        if (!IsOnWall(state.ball.pos)) return 0.0f;

        // Must be in offensive zone
        if (!IsInOffensiveZone(state.ball.pos, player.team)) return 0.0f;

        // Must be on target
        if (!IsOnTarget(state.ball.pos, state.ball.vel, player.team)) return 0.0f;

        float ballSpeed = state.ball.vel.Length();
        if (ballSpeed < MIN_SHOT_SPEED) return 0.0f;

        // Base shot reward
        float reward = 6.0f;

        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Power bonus
        float powerBonus = RS_CLAMP((ballSpeed - MIN_SHOT_SPEED) / 1300.0f, 0.0f, 1.0f) * 4.0f;
        reward += powerBonus;

        // Accuracy bonus (tight angle to goal)
        Vec toGoal = (oppGoal - state.ball.pos).Normalized();
        float alignment = state.ball.vel.Normalized().Dot(toGoal);
        float accuracyBonus = RS_CLAMP((alignment - 0.6f) / 0.4f, 0.0f, 1.0f) * 3.0f;
        reward += accuracyBonus;

        // Goal scoring bonus
        if (state.goalScored) {
            reward += 12.0f;
            s_goalCount.fetch_add(1);
        }

        s_shotCount.fetch_add(1);
        s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

        return reward;
    }
};

// ============================================================================
// WallOutplayReward
// Rewards beating opponents who are challenging on the wall
// - Detects opponent proximity during wall play
// - Rewards winning the challenge (maintaining/gaining possession)
// - Bonus for clean beats and follow-up plays
// ============================================================================
class WallOutplayReward : public Reward {
private:
    struct OutplayState {
        bool inChallenge = false;
        float challengeStartTime = 0.0f;
        uint32_t opponentId = 0;
        Vec ballPosAtStart;
    };

    mutable std::map<uint32_t, OutplayState> playerStates;
    mutable float currentTime = 0.0f;

    const float WALL_DIST_THRESHOLD = 400.0f;
    const float OPPONENT_PROXIMITY = 600.0f;  // Opponent must be this close to be "challenging"
    const float CHALLENGE_TIMEOUT = 2.0f;

    inline static std::atomic<int> s_outplayCount{0};
    inline static std::atomic<long long> s_rewardMilli{0};

    bool IsOnWall(const Vec& pos) const {
        float distX = abs(pos.x) - CommonValues::SIDE_WALL_X;
        float distY = abs(pos.y) - CommonValues::BACK_WALL_Y;
        return (abs(distX) < WALL_DIST_THRESHOLD || abs(distY) < WALL_DIST_THRESHOLD);
    }

    const Player* GetNearestOpponent(const Player& player, const GameState& state) const {
        const Player* nearest = nullptr;
        float minDist = OPPONENT_PROXIMITY;

        for (const auto& other : state.players) {
            if (other.team == player.team) continue;

            float dist = (other.pos - player.pos).Length();
            if (dist < minDist) {
                minDist = dist;
                nearest = &other;
            }
        }

        return nearest;
    }

public:
    struct Metrics {
        int outplays = 0;
        double totalReward = 0.0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.outplays = s_outplayCount.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.totalReward = double(r) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        playerStates.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        uint32_t id = player.carId;

        if (playerStates.find(id) == playerStates.end()) {
            playerStates[id] = OutplayState();
        }

        auto& pState = playerStates[id];
        float reward = 0.0f;

        // Check if on wall with opponent nearby
        bool onWall = IsOnWall(state.ball.pos);
        const Player* opponent = GetNearestOpponent(player, state);

        // Start challenge tracking
        if (onWall && opponent && !pState.inChallenge) {
            float distToBall = (player.pos - state.ball.pos).Length();
            float oppDistToBall = (opponent->pos - state.ball.pos).Length();

            // Both players near ball on wall
            if (distToBall < 500.0f && oppDistToBall < OPPONENT_PROXIMITY) {
                pState.inChallenge = true;
                pState.challengeStartTime = currentTime;
                pState.opponentId = opponent->carId;
                pState.ballPosAtStart = state.ball.pos;
            }
        }

        // Evaluate challenge outcome
        if (pState.inChallenge && player.ballTouchedStep) {
            // Player touched ball during challenge - check if it's a good touch
            float ballSpeed = state.ball.vel.Length();

            if (ballSpeed > 600.0f) {  // Solid touch
                // Base outplay reward
                reward = 8.0f;

                // Speed bonus
                float speedBonus = RS_CLAMP((ballSpeed - 600.0f) / 1400.0f, 0.0f, 1.0f) * 3.0f;
                reward += speedBonus;

                // Quick challenge bonus (fast reactions)
                float challengeDuration = currentTime - pState.challengeStartTime;
                if (challengeDuration < 0.5f) {
                    reward += 3.0f;
                }

                // Control bonus (keeping ball on own side of wall)
                float distMoved = (state.ball.pos - pState.ballPosAtStart).Length();
                if (distMoved > 400.0f) {
                    reward += 2.0f;
                }

                s_outplayCount.fetch_add(1);
                s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
            }

            pState.inChallenge = false;
        }

        // Timeout check
        if (pState.inChallenge && currentTime - pState.challengeStartTime > CHALLENGE_TIMEOUT) {
            pState.inChallenge = false;
        }

        return reward;
    }
};

// ============================================================================
// WallPassReward
// Rewards wall passes to teammates in better positions
// - Detects wall touches that move ball toward teammates
// - Rewards positioning and successful receptions
// - Different from WallPassReward in AdvancedPlayRewards.h - this focuses on
//   smart decision-making (pass when teammate is open, not forcing wall plays)
// ============================================================================
class WallTeamPassReward : public Reward {
private:
    struct PassState {
        bool passActive = false;
        float passStartTime = 0.0f;
        uint32_t passerId = 0;
        Vec ballPosAtPass;
    };

    mutable std::map<uint32_t, PassState> passStates;
    mutable float currentTime = 0.0f;

    const float WALL_DIST_THRESHOLD = 400.0f;
    const float PASS_TIMEOUT = 3.0f;
    const float TEAMMATE_PROXIMITY = 1200.0f;

    inline static std::atomic<int> s_passCount{0};
    inline static std::atomic<long long> s_rewardMilli{0};

    bool IsOnWall(const Vec& pos) const {
        float distX = abs(pos.x) - CommonValues::SIDE_WALL_X;
        float distY = abs(pos.y) - CommonValues::BACK_WALL_Y;
        return (abs(distX) < WALL_DIST_THRESHOLD || abs(distY) < WALL_DIST_THRESHOLD);
    }

    const Player* GetBestPositionedTeammate(const Player& player, const GameState& state) const {
        const Player* best = nullptr;
        float bestScore = 0.0f;

        Vec oppGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        for (const auto& teammate : state.players) {
            if (teammate.team != player.team || teammate.carId == player.carId) continue;

            float distToBall = (teammate.pos - state.ball.pos).Length();
            if (distToBall > TEAMMATE_PROXIMITY * 2.0f) continue;

            // Score based on position toward goal and readiness
            float distToGoal = (teammate.pos - oppGoal).Length();
            float score = 1.0f / (distToGoal + 1.0f);  // Closer to goal = better

            if (teammate.isOnGround) score *= 1.3f;  // Ready to receive

            if (score > bestScore) {
                bestScore = score;
                best = &teammate;
            }
        }

        return best;
    }

public:
    struct Metrics {
        int passes = 0;
        double totalReward = 0.0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.passes = s_passCount.exchange(0);
        long long r = s_rewardMilli.exchange(0);
        m.totalReward = double(r) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        passStates.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        uint32_t id = player.carId;

        if (passStates.find(id) == passStates.end()) {
            passStates[id] = PassState();
        }

        auto& pState = passStates[id];
        float reward = 0.0f;

        // Detect wall pass initiation
        if (player.ballTouchedStep && IsOnWall(state.ball.pos)) {
            const Player* teammate = GetBestPositionedTeammate(player, state);

            if (teammate) {
                // Check if ball is moving toward teammate
                Vec toTeammate = (teammate->pos - state.ball.pos).Normalized();
                Vec ballVelDir = state.ball.vel.Normalized();
                float towardTeammate = ballVelDir.Dot(toTeammate);

                if (towardTeammate > 0.4f) {
                    pState.passActive = true;
                    pState.passStartTime = currentTime;
                    pState.passerId = player.carId;
                    pState.ballPosAtPass = state.ball.pos;

                    // Initial pass reward
                    reward = 3.0f;

                    // Accuracy bonus
                    float accuracyBonus = RS_CLAMP((towardTeammate - 0.4f) / 0.6f, 0.0f, 1.0f) * 2.0f;
                    reward += accuracyBonus;
                }
            }
        }

        // Detect pass reception
        if (pState.passActive && player.ballTouchedStep && player.carId != pState.passerId) {
            float passTime = currentTime - pState.passStartTime;

            if (passTime < PASS_TIMEOUT) {
                // Successful reception
                reward = 5.0f;

                // Quick reception bonus
                if (passTime < 1.5f) {
                    reward += 2.0f;
                }

                s_passCount.fetch_add(1);
                pState.passActive = false;
            }
        }

        // Timeout
        if (pState.passActive && currentTime - pState.passStartTime > PASS_TIMEOUT) {
            pState.passActive = false;
        }

        if (reward > 0.0f) {
            s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));
        }

        return reward;
    }
};

// ============================================================================
// WallPossessionGiveawayPenalty (DENSE VERSION)
// Penalizes wall touches that give possession to opponents WITH SHAPING:
// 1. Small penalty if opponent is closer to ball after touch (immediate feedback)
// 2. Medium penalty if opponent gains proximity advantage
// 3. Large penalty if opponent fully gains possession
// ============================================================================
class WallPossessionGiveawayPenalty : public Reward {
private:
    struct GiveawayState {
        bool trackingTouch = false;
        float touchTime = 0.0f;
        uint32_t toucherId = 0;
        float playerDistAtTouch = 0.0f;  // Track player distance for comparison
    };

    mutable std::map<uint32_t, GiveawayState> giveawayStates;
    mutable float currentTime = 0.0f;

    const float WALL_DIST_THRESHOLD = 400.0f;
    const float GIVEAWAY_WINDOW = 2.0f;
    const float POSSESSION_DISTANCE = 500.0f;

    // Dense penalty constants (smaller, more frequent)
    const float PROXIMITY_LOSS_PENALTY = -0.5f;    // Per tick if opponent getting closer
    const float ADVANTAGE_PENALTY = -2.0f;         // If opponent closer than player
    const float FULL_GIVEAWAY_PENALTY = -5.0f;     // If opponent has possession (reduced from -8/-12)

    inline static std::atomic<int> s_giveawayCount{0};
    inline static std::atomic<long long> s_penaltyMilli{0};

    bool IsOnWall(const Vec& pos) const {
        float distX = abs(pos.x) - CommonValues::SIDE_WALL_X;
        float distY = abs(pos.y) - CommonValues::BACK_WALL_Y;
        return (abs(distX) < WALL_DIST_THRESHOLD || abs(distY) < WALL_DIST_THRESHOLD);
    }

    const Player* GetClosestPlayerToBall(const GameState& state) const {
        const Player* closest = nullptr;
        float minDist = FLT_MAX;

        for (const auto& player : state.players) {
            float dist = (player.pos - state.ball.pos).Length();
            if (dist < minDist) {
                minDist = dist;
                closest = &player;
            }
        }

        return closest;
    }

public:
    struct Metrics {
        int giveaways = 0;
        double totalPenalty = 0.0;
    };

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.giveaways = s_giveawayCount.exchange(0);
        long long p = s_penaltyMilli.exchange(0);
        m.totalPenalty = double(p) / 1000.0;
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        giveawayStates.clear();
        currentTime = 0.0f;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        currentTime += state.deltaTime;
        uint32_t id = player.carId;

        if (giveawayStates.find(id) == giveawayStates.end()) {
            giveawayStates[id] = GiveawayState();
        }

        auto& gState = giveawayStates[id];
        float penalty = 0.0f;

        // Track wall touches
        if (player.ballTouchedStep && IsOnWall(state.ball.pos)) {
            gState.trackingTouch = true;
            gState.touchTime = currentTime;
            gState.toucherId = player.carId;
            gState.playerDistAtTouch = (player.pos - state.ball.pos).Length();
        }

        // DENSE SHAPING: Continuous feedback during tracking window
        if (gState.trackingTouch) {
            float timeSinceTouch = currentTime - gState.touchTime;

            if (timeSinceTouch > GIVEAWAY_WINDOW) {
                gState.trackingTouch = false;
            } else {
                const Player* closest = GetClosestPlayerToBall(state);
                float playerDistToBall = (player.pos - state.ball.pos).Length();

                if (closest && closest->team != player.team) {
                    float oppDistToBall = (closest->pos - state.ball.pos).Length();

                    // PROGRESSIVE PENALTIES:

                    // 1. Small penalty if opponent is getting closer
                    if (oppDistToBall < 800.0f) {
                        penalty += PROXIMITY_LOSS_PENALTY;
                    }

                    // 2. Medium penalty if opponent has proximity advantage
                    if (oppDistToBall < playerDistToBall && oppDistToBall < 600.0f) {
                        penalty += ADVANTAGE_PENALTY;
                    }

                    // 3. Large penalty if opponent has clear possession
                    if (oppDistToBall < POSSESSION_DISTANCE && oppDistToBall < playerDistToBall * 0.7f) {
                        // Base giveaway penalty
                        float basePenalty = FULL_GIVEAWAY_PENALTY;

                        // Scale by danger (smooth curve)
                        Vec ownGoal = (player.team == Team::BLUE) ?
                            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
                        float distToOwnGoal = (state.ball.pos - ownGoal).Length();
                        float dangerScale = RS_CLAMP((3000.0f - distToOwnGoal) / 3000.0f, 0.0f, 1.0f);
                        basePenalty *= (1.0f + dangerScale);  // 1.0x to 2.0x multiplier

                        penalty += basePenalty;

                        s_giveawayCount.fetch_add(1);
                        gState.trackingTouch = false;  // Stop tracking after full giveaway
                    }
                }
            }
        }

        if (penalty < 0.0f) {
            s_penaltyMilli.fetch_add(static_cast<long long>(std::abs(penalty) * 1000.0f));
        }

        return penalty;
    }
};

// ============================================================================
// MasterSmartWallPlayReward
// Combines all smart wall play rewards for context-aware decision-making
// ============================================================================
class MasterSmartWallPlayReward : public Reward {
private:
    WallClearReward wallClear;
    WallShotReward wallShot;
    WallOutplayReward wallOutplay;
    WallTeamPassReward wallPass;
    WallPossessionGiveawayPenalty giveawayPenalty;

    // Weights
    const float CLEAR_WEIGHT = 1.0f;
    const float SHOT_WEIGHT = 1.2f;
    const float OUTPLAY_WEIGHT = 1.0f;
    const float PASS_WEIGHT = 0.9f;
    const float GIVEAWAY_PENALTY_WEIGHT = 1.5f;  // Strong signal

public:
    virtual void Reset(const GameState& initialState) override {
        wallClear.Reset(initialState);
        wallShot.Reset(initialState);
        wallOutplay.Reset(initialState);
        wallPass.Reset(initialState);
        giveawayPenalty.Reset(initialState);
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float total = 0.0f;

        total += wallClear.GetReward(player, state, isFinal) * CLEAR_WEIGHT;
        total += wallShot.GetReward(player, state, isFinal) * SHOT_WEIGHT;
        total += wallOutplay.GetReward(player, state, isFinal) * OUTPLAY_WEIGHT;
        total += wallPass.GetReward(player, state, isFinal) * PASS_WEIGHT;
        total += giveawayPenalty.GetReward(player, state, isFinal) * GIVEAWAY_PENALTY_WEIGHT;

        return total;
    }

    static void PrintMetrics() {
        auto clear = WallClearReward::GetAndResetMetrics();
        auto shot = WallShotReward::GetAndResetMetrics();
        auto outplay = WallOutplayReward::GetAndResetMetrics();
        auto pass = WallTeamPassReward::GetAndResetMetrics();
        auto giveaway = WallPossessionGiveawayPenalty::GetAndResetMetrics();

        printf("=== SMART WALL PLAY METRICS ===\n");
        printf("Clears: %d (%.2f reward)\n", clear.clears, clear.totalReward);
        printf("Shots: %d, Goals: %d (%.2f reward)\n", shot.shots, shot.goals, shot.totalReward);
        printf("Outplays: %d (%.2f reward)\n", outplay.outplays, outplay.totalReward);
        printf("Passes: %d (%.2f reward)\n", pass.passes, pass.totalReward);
        printf("Giveaways: %d (%.2f penalty)\n", giveaway.giveaways, giveaway.totalPenalty);
    }
};

} // namespace RLGC
