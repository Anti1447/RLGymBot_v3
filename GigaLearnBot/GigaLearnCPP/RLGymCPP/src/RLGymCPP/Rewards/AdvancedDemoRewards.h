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
    // Rewards demos based on tactical context:
    //   +2 base for any demo
    //   +3 goalie removal bonus (target was defending their own goal)
    //   +4 prevent-goal bonus  (target near ball, ball threatening our goal)
    //   -2 bad-demo penalty    (player far out of own rotation)
    // Uses engine-provided eventState.demo / eventState.demoed flags — no heuristic.
    // 2026-03-02 | NJH — full rewrite: fixed team-relative goal refs, removed
    //   RS_CLAMP squash, replaced proximity heuristic, removed dead DemoState tracking.
    // ============================================================================
    class ContextualDemoReward : public Reward {
    private:
        // Base reward + contextual adjustments (no clamp — weight scales these)
        static constexpr float GENERAL_DEMO_REWARD  = 2.0f;
        static constexpr float REMOVE_GOALIE_BONUS  = 3.0f;  // target was defending their own goal
        static constexpr float PREVENT_GOAL_BONUS   = 4.0f;  // target near ball threatening our goal
        static constexpr float BAD_DEMO_PENALTY     = -2.0f; // player is far out of rotation

        // Distance thresholds
        static constexpr float GOALIE_ZONE          = 2000.0f; // target within this of their own goal
        static constexpr float BALL_PROXIMITY       = 1500.0f; // target within this of ball
        static constexpr float THREAT_BALL_TO_GOAL  = 2000.0f; // ball within this of our goal = danger
        static constexpr float OUT_OF_POSITION_DIST = 3000.0f; // player this far from own goal = bad

    public:
        virtual void Reset(const GameState& initialState) override {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Use engine demo event flag — set by RocketSim exactly when a demo occurs.
            if (!player.eventState.demo) return 0.0f;

            // Find the opponent who was demoed this frame.
            const Player* target = nullptr;
            for (const auto& opp : state.players) {
                if (opp.team == player.team) continue;
                if (opp.eventState.demoed) { target = &opp; break; }
            }
            if (!target) return 0.0f;

            float reward = GENERAL_DEMO_REWARD;

            // Goalie removal: target was defending their own goal
            Vec targetOwnGoal = (target->team == Team::BLUE)
                                 ? CommonValues::BLUE_GOAL_BACK
                                 : CommonValues::ORANGE_GOAL_BACK;
            if ((target->pos - targetOwnGoal).Length() < GOALIE_ZONE)
                reward += REMOVE_GOALIE_BONUS;

            // Prevent goal: target was near ball AND ball was threatening our goal
            if ((target->pos - state.ball.pos).Length() < BALL_PROXIMITY) {
                Vec ourGoal = (player.team == Team::BLUE)
                               ? CommonValues::BLUE_GOAL_BACK
                               : CommonValues::ORANGE_GOAL_BACK;
                if ((state.ball.pos - ourGoal).Length() < THREAT_BALL_TO_GOAL)
                    reward += PREVENT_GOAL_BONUS;
            }

            // Bad demo: player is far from their own goal (broke rotation for the demo)
            Vec ownGoal = (player.team == Team::BLUE)
                           ? CommonValues::BLUE_GOAL_BACK
                           : CommonValues::ORANGE_GOAL_BACK;
            if ((player.pos - ownGoal).Length() > OUT_OF_POSITION_DIST)
                reward += BAD_DEMO_PENALTY;

            return reward;
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
    // SECOND MAN OFFENSIVE DEMO REWARD
    // Role-gated demo reward for the 2nd man (MIDDLE role) creating scoring
    // opportunities through demo aggression while the team has possession.
    //
    // Two components:
    //   A. Per-frame approach reward — fires while approaching a tactically
    //      valuable opponent at speed (>= MIN_APPROACH_COMPONENT UU/s toward target).
    //      Target priority: last-defender (2pts) > near ball (1pt) > in attacking half (1pt).
    //      Gates: MIDDLE role + team possession + target in attacking half or near ball.
    //
    //   B. Event reward — fires on player.eventState.demo while in MIDDLE role.
    //      +BASE_DEMO_REWARD base, +LAST_DEFENDER_BONUS if target was last defender,
    //      +NEAR_BALL_BONUS if target was near ball in attacking half.
    //
    // Does NOT fire in 1v1 (no MIDDLE role exists). Pairs naturally with
    // ContextualDemoReward (role-agnostic) and FailedDemoAttemptPenalty.
    // ============================================================================
    class SecondManOffensiveDemoReward : public Reward {
    private:
        static constexpr float APPROACH_PER_SEC       = 0.8f;   // approach reward/s at full speed
        static constexpr float MAX_APPROACH_SPEED     = 2300.0f; // UU/s (supersonic)
        static constexpr float MIN_APPROACH_COMPONENT = 1000.0f; // UU/s toward target to qualify
        static constexpr float BALL_PROX_THRESHOLD    = 1500.0f; // target "near ball"
        static constexpr float LAST_DEF_GOAL_DIST     = 1800.0f; // last-defender range from opp goal
        static constexpr float POSSESSION_RADIUS      = 1500.0f; // team possession check
        static constexpr float BASE_DEMO_REWARD       = 3.0f;
        static constexpr float LAST_DEFENDER_BONUS    = 2.5f;
        static constexpr float NEAR_BALL_BONUS        = 1.5f;

        enum class SimpleRole { CLOSEST, MIDDLE, FURTHEST };

        SimpleRole DetermineRole(const Player& player, const GameState& state) const {
            float myDist = (player.pos - state.ball.pos).Length();
            std::vector<float> dists;
            for (const auto& p : state.players) {
                if (p.team == player.team && p.carId != player.carId)
                    dists.push_back((p.pos - state.ball.pos).Length());
            }
            if (dists.empty()) return SimpleRole::CLOSEST;
            std::sort(dists.begin(), dists.end());
            if (myDist < dists[0]) return SimpleRole::CLOSEST;
            if (dists.size() >= 2 && myDist > dists[1]) return SimpleRole::FURTHEST;
            return SimpleRole::MIDDLE;
        }

        bool TeamHasPossession(const Player& player, const GameState& state) const {
            float closestDist = 1e10f;
            Team  closestTeam = Team::BLUE;
            for (const auto& p : state.players) {
                float d = (p.pos - state.ball.pos).Length();
                if (d < closestDist) { closestDist = d; closestTeam = p.team; }
            }
            return closestTeam == player.team && closestDist < POSSESSION_RADIUS;
        }

        // Score an opponent by tactical value as a demo target.
        // Returns -1 if the opponent should be ignored (in our half, not near ball).
        float TargetScore(const Player& opp, const Player& player,
                          const GameState& state) const {
            Vec oppGoal = (player.team == Team::BLUE)
                           ? CommonValues::ORANGE_GOAL_BACK
                           : CommonValues::BLUE_GOAL_BACK;

            float score = 0.0f;
            bool nearBall       = (opp.pos - state.ball.pos).Length() < BALL_PROX_THRESHOLD;
            float attackY       = (player.team == Team::BLUE) ? opp.pos.y : -opp.pos.y;
            bool inAttackHalf   = attackY > 0.0f;
            bool isLastDefender = (opp.pos - oppGoal).Length() < LAST_DEF_GOAL_DIST;

            // Ignore opponents who are irrelevant (defending half, not near ball)
            if (!inAttackHalf && !nearBall) return -1.0f;

            if (nearBall)       score += 1.0f;
            if (inAttackHalf)   score += 1.0f;
            if (isLastDefender) score += 2.0f;
            return score;
        }

    public:
        virtual void Reset(const GameState& initialState) override {}

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Skip 1v1 — no MIDDLE role exists
            if (state.players.size() < 4) return 0.0f;

            // Must be 2nd man
            if (DetermineRole(player, state) != SimpleRole::MIDDLE) return 0.0f;

            // Team must have possession
            if (!TeamHasPossession(player, state)) return 0.0f;

            // Find the highest-value demo target
            const Player* target   = nullptr;
            float         bestScore = -1.0f;
            for (const auto& opp : state.players) {
                if (opp.team == player.team || opp.isDemoed) continue;
                float s = TargetScore(opp, player, state);
                if (s > bestScore) { bestScore = s; target = &opp; }
            }
            if (!target) return 0.0f;

            float reward = 0.0f;

            // — Component B: event reward on successful demo —
            if (player.eventState.demo) {
                reward += BASE_DEMO_REWARD;
                Vec oppGoal = (player.team == Team::BLUE)
                               ? CommonValues::ORANGE_GOAL_BACK
                               : CommonValues::BLUE_GOAL_BACK;
                if ((target->pos - oppGoal).Length() < LAST_DEF_GOAL_DIST)
                    reward += LAST_DEFENDER_BONUS;
                bool nearBall     = (target->pos - state.ball.pos).Length() < BALL_PROX_THRESHOLD;
                float attackY     = (player.team == Team::BLUE) ? target->pos.y : -target->pos.y;
                if (nearBall && attackY > 0.0f)
                    reward += NEAR_BALL_BONUS;
                return reward;
            }

            // — Component A: per-frame approach reward —
            Vec toTarget     = target->pos - player.pos;
            float dist       = toTarget.Length();
            if (dist < 1.0f) return 0.0f;
            float velComp    = player.vel.Dot(toTarget / dist); // speed toward target
            if (velComp < MIN_APPROACH_COMPONENT) return 0.0f;

            float approachFrac = RS_CLAMP(velComp / MAX_APPROACH_SPEED, 0.0f, 1.0f);
            float dt           = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            reward             = APPROACH_PER_SEC * approachFrac * dt;

            return reward;
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

    // ============================================================================
    // StrategicDemoReward
    // Rewards demos during high-value tactical moments:
    // 1. Opponent is landing (after aerial/flip/double-jump) - predictable, vulnerable
    // 2. Opponent has low boost (<25) - can't evade or climb
    // 3. Opponent is last man back - sole defender, ball advancing
    // ALL THREE conditions must be met for reward to trigger (multi-gate system)
    // ============================================================================
    class StrategicDemoReward : public Reward {
    private:
        struct OppMemory {
            float timeSinceGrounded = 0.0f;  // Tracks landing window
        };

        mutable std::map<uint32_t, OppMemory> oppMemory;  // Per-opponent tracking

        // Thresholds
        static constexpr float LOW_BOOST_THRESHOLD = 25.0f;
        static constexpr float LANDING_WINDOW = 0.5f;
        static constexpr float BALL_ADVANCE_SPEED = 400.0f;
        static constexpr float BALL_CRITICAL_DISTANCE = 4000.0f;
        static constexpr float BASE_DEMO_REWARD = 8.0f;
        static constexpr float AERIAL_BONUS = 2.0f;
        static constexpr float ULTRA_LOW_BOOST_BONUS = 1.0f;

        bool IsLastManBack(const Player& opponent, const GameState& state, const Player& player) {
            Team oppTeam = opponent.team;
            Vec theirGoal = (oppTeam == Team::BLUE)
                ? Vec(0, -5120, 0)  // Blue goal back
                : Vec(0, 5120, 0);  // Orange goal back

            // Check 1: Opponent in defensive half
            bool inDefensive = (oppTeam == Team::BLUE) ? (opponent.pos.y < 0) : (opponent.pos.y > 0);
            if (!inDefensive) return false;

            // Check 2: Ball advancing toward their goal OR close to goal
            Vec ballToGoal = state.ball.pos - theirGoal;
            float distToGoal = ballToGoal.Length();

            // Calculate if ball is moving toward goal
            Vec goalDir = (theirGoal - state.ball.pos).Normalized();
            float ballAdvanceVel = state.ball.vel.Dot(goalDir);

            bool ballAdvancing = (ballAdvanceVel > BALL_ADVANCE_SPEED) || (distToGoal < BALL_CRITICAL_DISTANCE);
            if (!ballAdvancing) return false;

            // Check 3: No teammate is closer to ball + in defensive
            float oppDistToBall = (opponent.pos - state.ball.pos).Length();
            for (const auto& teammate : state.players) {
                if (teammate.team != oppTeam || teammate.carId == opponent.carId) continue;

                float tmDistToBall = (teammate.pos - state.ball.pos).Length();
                bool tmInDefensive = (oppTeam == Team::BLUE)
                    ? (teammate.pos.y < 0)
                    : (teammate.pos.y > 0);

                // If teammate is closer AND in defensive position, we have coverage
                if (tmDistToBall < oppDistToBall && tmInDefensive) {
                    return false;  // Not last man back
                }
            }

            return true;  // Last man back!
        }

    public:
        virtual void Reset(const GameState& initialState) override {
            oppMemory.clear();
        }

        virtual void PreStep(const GameState& state) override {
            // Update landing window timers for all opponents
            for (const auto& player : state.players) {
                if (!oppMemory.count(player.carId)) {
                    oppMemory[player.carId] = OppMemory();
                }

                auto& mem = oppMemory[player.carId];

                // Check for landing transitions (both flip end and airborne→grounded)
                bool shouldResetLandingWindow = false;

                // Check 1: Flip just ended
                if (state.prev) {
                    bool wasFlipping = state.prev->players[player.carId].isFlipping;
                    bool isFlipping = player.isFlipping;
                    if (wasFlipping && !isFlipping) {
                        shouldResetLandingWindow = true;  // Flip ended, start landing window
                    }
                }

                // Check 2: Airborne → grounded transition
                if (state.prev) {
                    bool wasAirborne = !state.prev->players[player.carId].isOnGround;
                    bool isGrounded = player.isOnGround;
                    if (wasAirborne && isGrounded) {
                        shouldResetLandingWindow = true;  // Just landed
                    }
                }

                // Update timer
                if (shouldResetLandingWindow) {
                    mem.timeSinceGrounded = 0.0f;
                } else if (player.isOnGround) {
                    mem.timeSinceGrounded += state.deltaTime > 0.0f ? state.deltaTime : 0.016f;
                } else {
                    // Still airborne, keep timer at 0
                    mem.timeSinceGrounded = 0.0f;
                }
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Only reward if this player successfully demoed someone
            if (!player.eventState.demo) {
                return 0.0f;
            }

            // Find the opponent who was demoed
            for (const auto& opponent : state.players) {
                // Look for opponent with demoed flag set
                if (opponent.team == player.team || !opponent.eventState.demoed) continue;

                auto& oppMem = oppMemory[opponent.carId];

                // Condition 1: Opponent is in landing window (vulnerable state)
                bool isLanding = (oppMem.timeSinceGrounded > 0.0f && oppMem.timeSinceGrounded < LANDING_WINDOW);
                if (!isLanding) continue;

                // Condition 2: Opponent has low boost
                bool hasLowBoost = (opponent.boost < LOW_BOOST_THRESHOLD);
                if (!hasLowBoost) continue;

                // Condition 3: Opponent is last man back
                bool isLastManBack = IsLastManBack(opponent, state, player);
                if (!isLastManBack) continue;

                // All three conditions met! Calculate reward
                float reward = BASE_DEMO_REWARD;

                // Aerial bonus: if opponent is high in the air, demo is more difficult
                if (opponent.pos.z > 500.0f) {
                    reward += AERIAL_BONUS;
                }

                // Ultra-low boost bonus: if opponent had almost no boost, reward more
                if (opponent.boost < 10.0f) {
                    reward += ULTRA_LOW_BOOST_BONUS;
                }

                return reward;
            }

            return 0.0f;
        }
    };

} // namespace RLGC
