/*
 * File: AdvancedDemoRewards.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Advanced demo tactics reward system that teaches WHEN to demo and when NOT to.
 *
 *   DO Demo:
 *   - Remove goalie (defending goal)
 *   - Prevent goal (opponent about to score)
 *   - Break rotation (disrupt opponent positioning)
 *   - Air dribble assist (advanced tactic)
 *   - Solo defender (ball rolling to net)
 *
 *   DON'T Demo:
 *   - Rage demo (repeated failures)
 *   - Chasing (moving away from ball)
 *   - Non-threats (far from play)
 *   - Breaking own rotation (last back)
 *   - Adverse effects (open net for teammate)
 *   - Spawn trapping (near spawn points)
 *
 * Author: @user + Claude
 * Date: January 2026
 * Revision: 1.0
 */

#pragma once

#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"
#include <map>
#include <vector>
#include <cmath>
#include "../CommonValues.h"

namespace RLGC {

    // ============================================================================
    // OPPONENT ROLE ENUM
    // Used to classify opponent's current role in the game
    // ============================================================================
    enum class OpponentRole {
        GOALIE,           // Defending goal area (within 1500 units)
        ATTACKER,         // Close to ball, attacking
        ROTATOR,          // Moving back to defense
        NON_THREAT,       // Far from ball/goal
        LAST_DEFENDER     // Solo defender with ball approaching
    };

    // ============================================================================
    // CONTEXTUAL DEMO REWARD
    // Main reward that evaluates tactical correctness of demos.
    // Analyzes opponent role, game situation, and own positioning.
    // Rewards: +12 to +25 for good demos
    // Penalties: -3 to -15 for bad demos
    // ============================================================================
    class ContextualDemoReward : public Reward {
    private:
        struct DemoMemory {
            uint64_t lastDemoTick = 0;
            uint32_t targetCarId = 0;
            OpponentRole targetRole = OpponentRole::NON_THREAT;
            Vec demoLocation;
            int consecutiveFails = 0;
            float lastFailTime = 0.0f;
        };

        mutable std::map<uint32_t, DemoMemory> memory;

        // Tuning parameters
        float goalieZoneRadius = 1500.0f;       // Distance from goal to be "goalie"
        float attackerDistThreshold = 1000.0f;  // Close to ball = attacker
        float nonThreatDistThreshold = 3000.0f; // Far from ball = non-threat
        float chasePenaltyDistThreshold = 500.0f;  // Extra distance to opponent vs ball

        // Reward magnitudes
        float removeGoalieReward = 25.0f;
        float preventGoalReward = 20.0f;
        float breakRotationReward = 12.0f;

        // Penalty magnitudes
        float rageDemoPenalty = -8.0f;
        float chasePenalty = -5.0f;
        float breakOwnRotationPenalty = -10.0f;
        float openNetPenalty = -15.0f;
        float nonThreatPenalty = -3.0f;

        // Determine opponent's role based on positioning
        OpponentRole DetermineOpponentRole(const Player& opponent, const GameState& state, Team playerTeam) const {
            Vec oppGoal = (opponent.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            float distToOwnGoal = (opponent.pos - oppGoal).Length();
            float distToBall = (opponent.pos - state.ball.pos).Length();

            // GOALIE: Within goal zone, facing ball
            if (distToOwnGoal < goalieZoneRadius) {
                Vec toBall = (state.ball.pos - opponent.pos).Normalized();
                float facing = opponent.rotMat.forward.Dot(toBall);
                if (facing > 0.5f) {
                    return OpponentRole::GOALIE;
                }
            }

            // ATTACKER: Close to ball, moving toward it
            if (distToBall < attackerDistThreshold) {
                Vec toBall = (state.ball.pos - opponent.pos).Normalized();
                float velToward = opponent.vel.Dot(toBall);
                if (velToward > 300.0f) {
                    return OpponentRole::ATTACKER;
                }
            }

            // ROTATOR: Moving toward own goal, away from ball
            Vec toOwnGoal = (oppGoal - opponent.pos).Normalized();
            float velToGoal = opponent.vel.Dot(toOwnGoal);
            if (velToGoal > 500.0f && distToBall > 1500.0f) {
                return OpponentRole::ROTATOR;
            }

            // NON_THREAT: Far from ball and goal
            if (distToBall > nonThreatDistThreshold && distToOwnGoal > 2500.0f) {
                return OpponentRole::NON_THREAT;
            }

            return OpponentRole::ROTATOR;  // Default
        }

        // Check if opponent is about to score
        bool IsAboutToScore(const Player& opponent, const GameState& state) const {
            float distToBall = (opponent.pos - state.ball.pos).Length();
            if (distToBall > 500.0f) return false;

            // Check if ball heading toward our goal using RewardMath helper
            Vec ourGoal = (opponent.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

            Vec ballToGoal = ourGoal - state.ball.pos;
            float ballSpeed = state.ball.vel.Length();

            if (ballSpeed < 400.0f) return false;

            float alignment = state.ball.vel.Dot(ballToGoal.Normalized());
            return alignment > 0.6f;
        }

        // Check if player is chasing (moving away from ball to get opponent)
        bool IsChasing(const Player& player, const GameState& state, const Player& target) const {
            Vec toBall = state.ball.pos - player.pos;
            Vec toTarget = target.pos - player.pos;

            float ballDist = toBall.Length();
            float targetDist = toTarget.Length();

            // Chasing if moving toward target, away from ball, and target farther
            Vec playerDir = player.vel.Normalized();
            float towardTarget = playerDir.Dot(toTarget.Normalized());
            float towardBall = playerDir.Dot(toBall.Normalized());

            bool movingTowardTarget = towardTarget > 0.7f;
            bool notTowardBall = towardBall < 0.3f;
            bool targetFarther = targetDist > ballDist + chasePenaltyDistThreshold;

            return movingTowardTarget && notTowardBall && targetFarther;
        }

        // Check if player is breaking own rotation (last back going for demo)
        bool BreaksOwnRotation(const Player& player, const GameState& state) const {
            Vec ownGoal = (player.team == Team::BLUE) ?
                CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;

            float playerDistToGoal = (player.pos - ownGoal).Length();

            // Count teammates closer to goal
            int teammatesBehind = 0;
            for (const auto& tm : state.players) {
                if (tm.team != player.team || tm.carId == player.carId) continue;
                if (tm.isDemoed) continue;

                float tmDistToGoal = (tm.pos - ownGoal).Length();
                if (tmDistToGoal < playerDistToGoal) {
                    teammatesBehind++;
                }
            }

            // If last back and moving away from ball, breaking rotation
            if (teammatesBehind == 0) {
                Vec toBall = state.ball.pos - player.pos;
                float towardBall = player.vel.Dot(toBall.Normalized());
                return towardBall < 0.0f;  // Moving away
            }

            return false;
        }

        // Check if open net (teammate has clear shot)
        bool IsOpenNet(const Player& player, const GameState& state) const {
            Vec oppGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

            // Check if any teammate is in better scoring position
            for (const auto& tm : state.players) {
                if (tm.team != player.team || tm.carId == player.carId) continue;
                if (tm.isDemoed) continue;

                float tmDistToGoal = (tm.pos - oppGoal).Length();
                float tmDistToBall = (tm.pos - state.ball.pos).Length();

                // Teammate close to ball and goal = open net situation
                if (tmDistToBall < 500.0f && tmDistToGoal < 2000.0f) {
                    return true;
                }
            }

            return false;
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            memory.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;

            // Initialize memory
            if (memory.find(id) == memory.end()) {
                memory[id] = DemoMemory();
            }

            auto& mem = memory[id];

            // Check if demo occurred this step
            if (!player.eventState.demo) {
                return 0.0f;
            }

            // Find the demoed opponent
            const Player* demoedOpponent = nullptr;
            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;
                if (opp.eventState.demoed) {
                    demoedOpponent = &opp;
                    break;
                }
            }

            if (!demoedOpponent) {
                return 0.0f;  // No opponent found (shouldn't happen)
            }

            // Determine opponent role
            OpponentRole role = DetermineOpponentRole(*demoedOpponent, state, player.team);

            // Check for bad demo scenarios first (penalties)
            float reward = 0.0f;

            // 1. Open net penalty
            if (IsOpenNet(player, state)) {
                return openNetPenalty;
            }

            // 2. Breaking own rotation penalty
            if (BreaksOwnRotation(player, state)) {
                return breakOwnRotationPenalty;
            }

            // 3. Chasing penalty
            if (IsChasing(player, state, *demoedOpponent)) {
                return chasePenalty;
            }

            // 4. Non-threat penalty
            if (role == OpponentRole::NON_THREAT) {
                return nonThreatPenalty;
            }

            // Now check for good demo scenarios (rewards)

            // 1. Remove goalie
            if (role == OpponentRole::GOALIE) {
                return removeGoalieReward;
            }

            // 2. Prevent goal (attacker about to score)
            if (role == OpponentRole::ATTACKER && IsAboutToScore(*demoedOpponent, state)) {
                return preventGoalReward;
            }

            // 3. Break rotation (demo rotating opponent)
            if (role == OpponentRole::ROTATOR) {
                return breakRotationReward;
            }

            // Default: small reward for any tactical demo
            return 3.0f;
        }
    };

    // ============================================================================
    // FAILED DEMO ATTEMPT PENALTY
    // Penalizes repeated failed supersonic approaches (chasing/rage demos).
    // Tracks supersonic state near opponents without demo result.
    // Escalating penalties: -1, -3, -8 for consecutive fails.
    // ============================================================================
    class FailedDemoAttemptPenalty : public Reward {
    private:
        struct AttemptMemory {
            bool wasSupersonic = false;
            bool wasNearOpponent = false;
            uint32_t targetId = 0;
            float lastAttemptTime = 0.0f;
            int consecutiveFails = 0;
        };

        mutable std::map<uint32_t, AttemptMemory> memory;
        float currentTime = 0.0f;

        static constexpr float ATTEMPT_DETECTION_RANGE = 400.0f;
        static constexpr float COOLDOWN_TIME = 1.0f;
        static constexpr float SUPERSONIC_THRESHOLD = 2200.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            memory.clear();
            currentTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            currentTime += state.deltaTime;
            uint32_t id = player.carId;

            if (memory.find(id) == memory.end()) {
                memory[id] = AttemptMemory();
            }

            auto& mem = memory[id];

            bool isSupersonic = player.vel.Length() >= SUPERSONIC_THRESHOLD;

            // Find closest opponent
            const Player* closestOpp = nullptr;
            float closestDist = std::numeric_limits<float>::max();

            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;
                if (opp.isDemoed) continue;

                float dist = (player.pos - opp.pos).Length();
                if (dist < closestDist) {
                    closestDist = dist;
                    closestOpp = &opp;
                }
            }

            bool nearOpponent = (closestDist < ATTEMPT_DETECTION_RANGE);

            // Check if we just succeeded in a demo
            if (player.eventState.demo) {
                mem.consecutiveFails = 0;  // Reset on success
                mem.wasSupersonic = false;
                mem.wasNearOpponent = false;
                return 0.0f;
            }

            // Detect failed attempt (was supersonic + near opponent, now not)
            if (mem.wasSupersonic && mem.wasNearOpponent && (!isSupersonic || !nearOpponent)) {
                // Failed attempt detected
                if (currentTime - mem.lastAttemptTime > COOLDOWN_TIME) {
                    mem.consecutiveFails++;
                    mem.lastAttemptTime = currentTime;

                    // Escalating penalty
                    if (mem.consecutiveFails == 1) {
                        mem.wasSupersonic = isSupersonic;
                        mem.wasNearOpponent = nearOpponent;
                        return -1.0f;
                    }
                    else if (mem.consecutiveFails == 2) {
                        mem.wasSupersonic = isSupersonic;
                        mem.wasNearOpponent = nearOpponent;
                        return -3.0f;
                    }
                    else {
                        mem.wasSupersonic = isSupersonic;
                        mem.wasNearOpponent = nearOpponent;
                        return -8.0f;  // Rage demo penalty
                    }
                }
            }

            // Update state
            mem.wasSupersonic = isSupersonic;
            mem.wasNearOpponent = nearOpponent;

            return 0.0f;
        }
    };

    // ============================================================================
    // SPAWN CAMPING PENALTY
    // Detects and penalizes spawn camping behavior.
    // Uses CommonValues spawn positions (4 per team).
    // Penalty: -0.5/sec near spawn, -2/sec if actively camping (>1.5 seconds).
    // ============================================================================
    class SpawnCampingPenalty : public Reward {
    private:
        struct CampingMemory {
            float timeNearSpawn = 0.0f;
            int visitCount = 0;
        };

        mutable std::map<uint32_t, CampingMemory> memory;

        static constexpr float SPAWN_PROXIMITY_RADIUS = 800.0f;
        static constexpr float CAMPING_THRESHOLD = 1.5f;

        // Get all spawn points for opponent team
        std::vector<Vec> GetOpponentSpawnPoints(Team opponentTeam) const {
            std::vector<Vec> spawns;

            if (opponentTeam == Team::BLUE) {
                spawns.push_back(CommonValues::BLUE_DEMOLISH_RIGHT_INSIDE);
                spawns.push_back(CommonValues::BLUE_DEMOLISH_RIGHT_OUTSIDE);
                spawns.push_back(CommonValues::BLUE_DEMOLISH_LEFT_INSIDE);
                spawns.push_back(CommonValues::BLUE_DEMOLISH_LEFT_OUTSIDE);
            }
            else {
                spawns.push_back(CommonValues::ORANGE_DEMOLISH_RIGHT_INSIDE);
                spawns.push_back(CommonValues::ORANGE_DEMOLISH_RIGHT_OUTSIDE);
                spawns.push_back(CommonValues::ORANGE_DEMOLISH_LEFT_INSIDE);
                spawns.push_back(CommonValues::ORANGE_DEMOLISH_LEFT_OUTSIDE);
            }

            return spawns;
        }

        bool IsNearSpawnPoint(const Vec& pos, Team opponentTeam) const {
            auto spawns = GetOpponentSpawnPoints(opponentTeam);

            for (const Vec& spawn : spawns) {
                float dist = (pos - spawn).Length();
                if (dist < SPAWN_PROXIMITY_RADIUS) {
                    return true;
                }
            }

            return false;
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            memory.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;

            if (memory.find(id) == memory.end()) {
                memory[id] = CampingMemory();
            }

            auto& mem = memory[id];

            // Determine opponent team
            Team opponentTeam = (player.team == Team::BLUE) ? Team::ORANGE : Team::BLUE;

            bool nearSpawn = IsNearSpawnPoint(player.pos, opponentTeam);

            if (nearSpawn) {
                mem.timeNearSpawn += state.deltaTime;

                // Apply penalty
                if (mem.timeNearSpawn > CAMPING_THRESHOLD) {
                    // Actively camping
                    return -2.0f * state.deltaTime;
                }
                else {
                    // Near spawn but not yet camping
                    return -0.5f * state.deltaTime;
                }
            }
            else {
                // Reset if moved away
                mem.timeNearSpawn = 0.0f;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // SOLO DEFENDER BUMP REWARD
    // Rewards bumping the last defender when ball is rolling toward their net.
    // Situation: Ball moving to goal, only 1 defender nearby, bump them.
    // Reward: +18 for successful solo defender bump.
    // ============================================================================
    class SoloDefenderBumpReward : public Reward {
    private:
        static constexpr float BALL_TO_GOAL_SPEED_THRESHOLD = 400.0f;
        static constexpr float DEFENDER_DIST_THRESHOLD = 1200.0f;
        static constexpr float REWARD_AMOUNT = 18.0f;

        bool IsSoloDefenderSituation(const Player& player, const GameState& state, const Player** defenderOut) const {
            // Check if ball is moving toward opponent goal
            Vec oppGoal = (player.team == Team::BLUE) ?
                CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

            Vec ballToGoal = oppGoal - state.ball.pos;
            float ballSpeed = state.ball.vel.Length();

            if (ballSpeed < BALL_TO_GOAL_SPEED_THRESHOLD) {
                return false;
            }

            float alignment = state.ball.vel.Dot(ballToGoal.Normalized());
            if (alignment < 0.5f) {
                return false;  // Ball not heading to goal
            }

            // Count defenders near goal
            int defenderCount = 0;
            const Player* lastDefender = nullptr;

            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;
                if (opp.isDemoed) continue;

                float distToGoal = (opp.pos - oppGoal).Length();
                if (distToGoal < DEFENDER_DIST_THRESHOLD) {
                    defenderCount++;
                    lastDefender = &opp;
                }
            }

            // Solo defender situation
            if (defenderCount == 1 && lastDefender != nullptr) {
                *defenderOut = lastDefender;
                return true;
            }

            return false;
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            // No state needed
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Check if bump occurred
            if (!player.eventState.bump) {
                return 0.0f;
            }

            // Check if solo defender situation
            const Player* defender = nullptr;
            if (IsSoloDefenderSituation(player, state, &defender)) {
                // Verify we bumped the defender
                float distToDefender = (player.pos - defender->pos).Length();
                if (distToDefender < 300.0f) {
                    return REWARD_AMOUNT;
                }
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // AIR DRIBBLE DEMO ASSIST REWARD
    // Rewards bumps during air dribbles (advanced tactic).
    // Detects air dribble state: airborne >0.3s, 2+ consecutive touches.
    // Bonus: +8 for successful air dribble bump.
    // ============================================================================
    class AirDribbleDemoAssist : public Reward {
    private:
        struct AirDribbleState {
            bool active = false;
            float timeAirborne = 0.0f;
            int consecutiveTouches = 0;
            float lastTouchTime = 0.0f;
        };

        mutable std::map<uint32_t, AirDribbleState> airDribbleTracking;
        float currentTime = 0.0f;

        static constexpr float MIN_AIRBORNE_TIME = 0.3f;
        static constexpr int MIN_TOUCHES = 2;
        static constexpr float TOUCH_TIMEOUT = 2.0f;
        static constexpr float BUMP_ASSIST_REWARD = 8.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            airDribbleTracking.clear();
            currentTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            currentTime += state.deltaTime;
            uint32_t id = player.carId;

            if (airDribbleTracking.find(id) == airDribbleTracking.end()) {
                airDribbleTracking[id] = AirDribbleState();
            }

            auto& adState = airDribbleTracking[id];

            // Update airborne time
            if (!player.isOnGround) {
                adState.timeAirborne += state.deltaTime;
            }
            else {
                adState.timeAirborne = 0.0f;
                adState.active = false;
                adState.consecutiveTouches = 0;
            }

            // Track ball touches while aerial
            if (player.ballTouchedStep && !player.isOnGround) {
                if (currentTime - adState.lastTouchTime < TOUCH_TIMEOUT) {
                    adState.consecutiveTouches++;
                }
                else {
                    adState.consecutiveTouches = 1;
                }
                adState.lastTouchTime = currentTime;
            }

            // Check if air dribble is active
            if (adState.timeAirborne > MIN_AIRBORNE_TIME && adState.consecutiveTouches >= MIN_TOUCHES) {
                adState.active = true;
            }

            // Reward bump during air dribble
            if (adState.active && player.eventState.bump) {
                return BUMP_ASSIST_REWARD;
            }

            return 0.0f;
        }
    };

    // ============================================================================
    // MASTER DEMO TACTICS REWARD
    // Combines all demo reward components with appropriate weighting.
    // Usage: new MasterDemoTacticsReward()
    // ============================================================================
    class MasterDemoTacticsReward : public Reward {
    private:
        ContextualDemoReward contextualDemo;
        FailedDemoAttemptPenalty failedAttemptPenalty;
        SpawnCampingPenalty spawnCampingPenalty;
        AirDribbleDemoAssist airDribbleAssist;
        SoloDefenderBumpReward soloDefenderBump;

    public:
        virtual void Reset(const GameState& initialState) override {
            contextualDemo.Reset(initialState);
            failedAttemptPenalty.Reset(initialState);
            spawnCampingPenalty.Reset(initialState);
            airDribbleAssist.Reset(initialState);
            soloDefenderBump.Reset(initialState);
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float total = 0.0f;

            // Main contextual demo evaluation (highest weight)
            total += contextualDemo.GetReward(player, state, isFinal) * 1.0f;

            // Failed attempt penalty (prevent chasing/rage)
            total += failedAttemptPenalty.GetReward(player, state, isFinal) * 0.8f;

            // Spawn camping penalty (strongly discourage)
            total += spawnCampingPenalty.GetReward(player, state, isFinal) * 1.2f;

            // Advanced tactics (lower weight, bonus rewards)
            total += airDribbleAssist.GetReward(player, state, isFinal) * 0.6f;
            total += soloDefenderBump.GetReward(player, state, isFinal) * 0.7f;

            return total;
        }
    };

} // namespace RLGC
