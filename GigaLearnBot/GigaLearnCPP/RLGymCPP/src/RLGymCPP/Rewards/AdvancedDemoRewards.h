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
    // UPDATED 2/16/2026 with Claude Rev 1.0
    // Drastically simplified to focus on proximity-based demo detection rather than complex role classification. 
    // Detects demos when player is close to opponent (< 150 units) and moving fast (> 800 u/s), then awards context-based bonuses for goalie removal, goal prevention, etc.
    // ============================================================================
    class ContextualDemoReward : public Reward {
    private:
        struct DemoState {
            bool lastWasDemo = false;
            float lastDemoTime = 0.0f;
            uint32_t lastTargetId = 0;
        };

        mutable std::map<uint32_t, DemoState> playerStates;
        float currentTime = 0.0f;

        // Reward tuning (simplified for activation)
        static constexpr float REMOVE_GOALIE_REWARD = 6.0f;
        static constexpr float PREVENT_GOAL_REWARD = 5.0f;
        static constexpr float GENERAL_DEMO_REWARD = 3.0f;
        static constexpr float BAD_DEMO_PENALTY = -1.0f;

        // Distance thresholds
        static constexpr float GOALIE_ZONE = 2000.0f;
        static constexpr float BALL_PROXIMITY = 1500.0f;
        static constexpr float ATTACK_THREAT_DISTANCE = 1000.0f;

    public:
        virtual void Reset(const GameState& initialState) override {
            playerStates.clear();
            currentTime = 0.0f;
        }

        virtual void PreStep(const GameState& state) override {
            currentTime += state.deltaTime;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            uint32_t id = player.carId;

            if (playerStates.find(id) == playerStates.end()) {
                playerStates[id] = DemoState();
            }

            auto& pState = playerStates[id];

            // Detect demo/bump event - find closest opponent
            bool isDemoThisFrame = false;
            const Player* targetPlayer = nullptr;

            float closestDist = FLT_MAX;
            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;

                float dist = (player.pos - opp.pos).Length();
                if (dist < closestDist && dist < 500.0f) {
                    closestDist = dist;
                    targetPlayer = &opp;
                }
            }

            // Check if demo occurred this frame (very close proximity + fast movement)
            if (targetPlayer != nullptr && closestDist < 150.0f) {
                float playerSpeed = player.vel.Length();
                if (playerSpeed > 800.0f) {
                    isDemoThisFrame = true;
                    pState.lastTargetId = targetPlayer->carId;
                    pState.lastDemoTime = currentTime;
                }
            }

            // Return reward if demo occurred
            if (isDemoThisFrame && targetPlayer != nullptr) {
                float reward = GENERAL_DEMO_REWARD;

                // Bonus for removing goalie (near goal)
                float targetDistToGoal = (targetPlayer->pos - CommonValues::BLUE_GOAL_BACK).Length();
                if (targetDistToGoal < GOALIE_ZONE) {
                    reward += REMOVE_GOALIE_REWARD;
                }

                // Bonus for preventing goal (opponent near ball)
                float distToBall = (targetPlayer->pos - state.ball.pos).Length();
                if (distToBall < BALL_PROXIMITY) {
                    float ballToGoal = (state.ball.pos - CommonValues::ORANGE_GOAL_BACK).Length();
                    if (ballToGoal < 2000.0f) {
                        reward += PREVENT_GOAL_REWARD;
                    }
                }

                // Penalty for bad positioning (breaking own rotation)
                float playerDistToGoal = (player.pos - CommonValues::BLUE_GOAL_BACK).Length();
                if (playerDistToGoal > 3000.0f && closestDist < ATTACK_THREAT_DISTANCE) {
                    reward += BAD_DEMO_PENALTY;
                }

                return RS_CLAMP(reward, -1.0f, 1.0f);
            }

            return 0.0f;
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
