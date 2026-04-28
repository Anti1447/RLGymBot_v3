#pragma once

/*
 * File: AirDribbleBumpConversionReward.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Event-gated reward for the complete air dribble bump mechanic:
 *
 *     1. Air dribble is established  (>=2 air touches within 1.5s)
 *     2. Ball is aimed at opponent goal at the moment of the bump
 *     3. Defender is bumped or demoed (and was the nearest opponent to the ball)
 *     4. Ball trajectory remains goal-bound 0.5s after the bump
 *
 *   All four must be true for payout. Fires at most once per airborne sequence.
 *
 *   Supersedes:
 *     - AirDribbleBumpReward    (CompleteMechanicsRewards.h) — no goal-bound gate
 *     - AirBumpDemoReward       (CompleteMechanicsRewards.h) — no dribble requirement
 *     - AirDribbleDemoAssist    (AdvancedDemoRewards.h)      — no post-bump check
 *
 *   Intended phase: Phase 6 Champion+ only.
 *   See Reward_Change_Log.md [2026-04-21] for full spec.
 *
 *   Shadow Mode: set config.shadowMode = true to log metrics without paying reward
 *   to the optimizer. Recommended for 200-500M steps before weight commit.
 *
 * Author: Nathan + Claude
 * Date: April 2026
 * Revision: 1.0
 */

#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"
#include "../CommonValues.h"
#include <map>
#include <deque>
#include <atomic>
#include <cfloat>
#include <cstdint>

namespace RLGC {

    // ============================================================================
    // AIR DRIBBLE BUMP CONVERSION REWARD
    // 2026-04-21 | Rev 1.0 | NJH
    // ============================================================================
    class AirDribbleBumpConversionReward : public Reward {
    public:
        struct Config {
            // --- Air dribble establishment ---
            int   minAirTouches        = 2;       // >=N air touches required
            float airTouchWindowSec    = 1.5f;    // ...within this lookback window
            float minBallHeightAtBump  = 300.0f;  // Ball must be airborne (z >= this)

            // --- Goal-directedness at bump time ---
            float minGoalAlignment     = 0.5f;    // ball.vel_hat · toGoal_hat >= this

            // --- Contact / defender identity ---
            float maxBumpContactDist   = 350.0f;  // Car-to-opp distance to count as contact
            float maxDefenderToBallDist = 2500.0f;// Nearest-opp-to-ball must be within this
            float lastDefenderDist     = 2500.0f; // "Last defender" lives within this of own goal

            // --- Post-bump conversion window ---
            float conversionWindowSec  = 0.5f;    // Time to confirm ball still goal-bound
            float goalLookaheadSec     = 1.5f;    // Horizon for BallIntersectsOpponentGoalMouth

            // --- Front-block positional bonus ---
            float frontBlockLateralMax = 900.0f;  // Max lateral distance from ball→goal line
            float frontBlockMinLead    = 100.0f;  // Player must be this far ahead of ball

            // --- Payouts ---
            float basePayoutBump       = 15.0f;
            float basePayoutDemo       = 25.0f;
            float frontBlockBonusMul   = 1.5f;    // 1.5× if car blocks recovery during window
            float lastDefenderBonusMul = 1.2f;    // 1.2× if contacted opp was the last defender

            // --- Anti-farming ---
            float cooldownSec          = 5.0f;    // Per-player cooldown between payouts

            // --- Modes ---
            bool  shadowMode           = false;   // true = metrics only, no reward to optimizer
        };

        struct Metrics {
            // Raw counters (atomics exchanged)
            int airBumpContacts   = 0;
            int qualified         = 0;
            int demos             = 0;
            int bumps             = 0;
            int postBumpGoalBound = 0;
            int frontBlocks       = 0;
            int lastDefenderHits  = 0;
            double rewardSum      = 0.0;

            // Derived
            double qualifyRate      = 0.0;  // qualified / airBumpContacts
            double conversionRate   = 0.0;  // postBumpGoalBound / qualified
            double demoShare        = 0.0;  // demos / (demos + bumps)
            double avgRewardPerConv = 0.0;  // rewardSum / postBumpGoalBound
        };

    private:
        struct PendingConversion {
            float    timeLeft       = 0.0f;
            uint32_t targetCarId    = UINT32_MAX;
            bool     wasDemo        = false;
            bool     wasLastDefender = false;
            bool     frontBlockSeen = false;
            float    basePayout     = 0.0f;
        };

        struct PlayerTracking {
            std::deque<float> airTouchTimes;       // timestamps of recent air touches
            float             lastPayoutTime = -1000.0f;
            PendingConversion pending;
            bool              hasPending    = false;
        };

        mutable std::map<uint32_t, PlayerTracking> track;
        mutable float gameTime = 0.0f;

        Config config;

        // ---- Metrics (W&B) ----
        inline static std::atomic<int> s_airBumpContacts   { 0 };
        inline static std::atomic<int> s_qualified         { 0 };
        inline static std::atomic<int> s_demos             { 0 };
        inline static std::atomic<int> s_bumps             { 0 };
        inline static std::atomic<int> s_postBumpGoalBound { 0 };
        inline static std::atomic<int> s_frontBlocks       { 0 };
        inline static std::atomic<int> s_lastDefenderHits  { 0 };
        inline static std::atomic<long long> s_rewardMilli { 0 };

    public:
        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.airBumpContacts   = s_airBumpContacts.exchange(0);
            m.qualified         = s_qualified.exchange(0);
            m.demos             = s_demos.exchange(0);
            m.bumps             = s_bumps.exchange(0);
            m.postBumpGoalBound = s_postBumpGoalBound.exchange(0);
            m.frontBlocks       = s_frontBlocks.exchange(0);
            m.lastDefenderHits  = s_lastDefenderHits.exchange(0);
            long long rm        = s_rewardMilli.exchange(0);
            m.rewardSum         = double(rm) / 1000.0;

            if (m.airBumpContacts > 0)
                m.qualifyRate = double(m.qualified) / m.airBumpContacts;
            if (m.qualified > 0)
                m.conversionRate = double(m.postBumpGoalBound) / m.qualified;
            int totalHits = m.demos + m.bumps;
            if (totalHits > 0)
                m.demoShare = double(m.demos) / totalHits;
            if (m.postBumpGoalBound > 0)
                m.avgRewardPerConv = m.rewardSum / m.postBumpGoalBound;

            return m;
        }

        AirDribbleBumpConversionReward() : config() {}
        explicit AirDribbleBumpConversionReward(const Config& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            track.clear();
            gameTime = 0.0f;
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Kickoff exclusion — full state wipe so kickoff chaos doesn't leak into open play
            const bool isKickoff =
                (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 200.0f);
            if (isKickoff) {
                auto it = track.find(player.carId);
                if (it != track.end()) {
                    it->second.airTouchTimes.clear();
                    it->second.hasPending = false;
                }
                return 0.0f;
            }

            const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            gameTime += dt;

            auto& tr = track[player.carId];
            float reward = 0.0f;

            // ================================================================
            // PHASE 1 — Maintain rolling air-touch log
            // ================================================================
            if (player.ballTouchedStep && !player.isOnGround) {
                tr.airTouchTimes.push_back(gameTime);
            }
            const float cutoff = gameTime - config.airTouchWindowSec;
            while (!tr.airTouchTimes.empty() && tr.airTouchTimes.front() < cutoff) {
                tr.airTouchTimes.pop_front();
            }

            // ================================================================
            // PHASE 2 — Pending conversion window (if armed)
            // ================================================================
            if (tr.hasPending) {
                PendingConversion& pc = tr.pending;
                pc.timeLeft -= dt;

                // Track whether the car achieves front-block geometry during the window
                if (IsInFrontOfBall(player, state)) {
                    pc.frontBlockSeen = true;
                }

                if (pc.timeLeft <= 0.0f) {
                    // Resolve: ball must still be heading for the goal mouth
                    const bool stillGoalBound = RLGC::RewardMath::BallIntersectsOpponentGoalMouth(
                        player, state,
                        config.goalLookaheadSec,
                        /*requireBackNet=*/false,
                        /*useGravity=*/true);

                    if (stillGoalBound) {
                        s_postBumpGoalBound.fetch_add(1);

                        float payout = pc.basePayout;
                        if (pc.frontBlockSeen) {
                            payout *= config.frontBlockBonusMul;
                            s_frontBlocks.fetch_add(1);
                        }
                        if (pc.wasLastDefender) {
                            payout *= config.lastDefenderBonusMul;
                            s_lastDefenderHits.fetch_add(1);
                        }

                        // Metrics always accumulate (even in shadow mode)
                        s_rewardMilli.fetch_add(static_cast<long long>(payout * 1000.0f));

                        if (!config.shadowMode) {
                            reward += payout;
                        }

                        tr.lastPayoutTime = gameTime;
                    }
                    // else: bump disrupted the shot — no reward, no penalty.

                    tr.hasPending = false;
                }
            }

            // ================================================================
            // PHASE 3 — Detect qualifying bump this frame
            // ================================================================

            // Fast rejects (ordered cheap → expensive)
            if (tr.hasPending) return reward;
            if (player.isOnGround) return reward;
            if (!(player.eventState.bump || player.eventState.demo)) return reward;

            // Cooldown
            if ((gameTime - tr.lastPayoutTime) < config.cooldownSec) return reward;

            // Dribble establishment
            if (static_cast<int>(tr.airTouchTimes.size()) < config.minAirTouches) return reward;

            // Ball airborne?
            if (state.ball.pos.z < config.minBallHeightAtBump) return reward;

            // Ball goal-bound at bump frame?
            const Vec oppGoalBack = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_BACK
                : CommonValues::BLUE_GOAL_BACK;
            const Vec toGoal = oppGoalBack - state.ball.pos;
            const float ballSpeed = state.ball.vel.Length();
            if (ballSpeed < 1e-3f) return reward;
            const float goalAlign = state.ball.vel.Normalized().Dot(SafeNorm(toGoal));
            if (goalAlign < config.minGoalAlignment) return reward;

            // Identify contacted opponent (triangulated via eventState.bumped/demoed + proximity)
            const Player* bumpTarget = FindBumpTarget(player, state);
            if (!bumpTarget) return reward;

            // This is the only place we increment airBumpContacts — we know a real contact happened
            s_airBumpContacts.fetch_add(1);

            // Was the target the closest opponent to the ball?
            const Player* nearestOpp = NearestOpponentToBall(player, state);
            if (!nearestOpp || nearestOpp->carId != bumpTarget->carId) {
                return reward;  // bumped someone irrelevant
            }

            // Was that nearest opp within defender-range of the ball?
            const float oppToBallDist = (nearestOpp->pos - state.ball.pos).Length();
            if (oppToBallDist > config.maxDefenderToBallDist) return reward;

            // --- All pre-bump gates passed — arm pending conversion ---
            s_qualified.fetch_add(1);

            PendingConversion pc;
            pc.timeLeft        = config.conversionWindowSec;
            pc.targetCarId     = bumpTarget->carId;
            pc.wasDemo         = player.eventState.demo;
            pc.basePayout      = pc.wasDemo ? config.basePayoutDemo : config.basePayoutBump;
            pc.wasLastDefender = IsLastDefender(bumpTarget, player.team, state);
            pc.frontBlockSeen  = false;

            if (pc.wasDemo) s_demos.fetch_add(1);
            else            s_bumps.fetch_add(1);

            tr.pending    = pc;
            tr.hasPending = true;

            return reward;  // payout is deferred to Phase 2 resolution next window-end
        }

    private:
        // ------------------------------------------------------------------------
        // Identify the opponent we just contacted.
        // Triangulation: our event flag + their event flag + proximity. This is
        // more robust than proximity-only (which misrepresents aerial chaos).
        // ------------------------------------------------------------------------
        const Player* FindBumpTarget(const Player& player, const GameState& state) const {
            const Player* best = nullptr;
            float bestDist = FLT_MAX;

            for (const auto& p : state.players) {
                if (p.team == player.team) continue;
                if (p.carId == player.carId) continue;
                // Opponent's event flag must confirm they were hit this step.
                // (If WE demoed, they are demoed; if WE bumped, they are bumped.)
                const bool theyHit = p.eventState.bumped || p.eventState.demoed;
                if (!theyHit) continue;

                const float d = (p.pos - player.pos).Length();
                if (d < bestDist && d < config.maxBumpContactDist) {
                    bestDist = d;
                    best = &p;
                }
            }
            return best;
        }

        // ------------------------------------------------------------------------
        // Nearest opponent to the BALL (not to us).
        // ------------------------------------------------------------------------
        const Player* NearestOpponentToBall(const Player& player, const GameState& state) const {
            const Player* best = nullptr;
            float bestDist = FLT_MAX;

            for (const auto& p : state.players) {
                if (p.team == player.team) continue;
                if (p.isDemoed) continue;
                const float d = (p.pos - state.ball.pos).Length();
                if (d < bestDist) {
                    bestDist = d;
                    best = &p;
                }
            }
            return best;
        }

        // ------------------------------------------------------------------------
        // "Last defender" test:
        //   (a) opponent is within lastDefenderDist of their own goal, AND
        //   (b) no teammate of theirs is further back (closer to own goal).
        //
        // attackerTeam is the team that's DOING the bumping; the defender belongs
        // to the other team.
        // ------------------------------------------------------------------------
        bool IsLastDefender(const Player* opp, Team attackerTeam, const GameState& state) const {
            if (!opp) return false;

            // The opponent's own goal (= the attacker's target goal)
            const Vec defOwnGoal = (attackerTeam == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_CENTER
                : CommonValues::BLUE_GOAL_CENTER;

            const float oppDistToOwnGoal = (opp->pos - defOwnGoal).Length();
            if (oppDistToOwnGoal > config.lastDefenderDist) return false;

            // Any teammate of theirs closer to their own goal disqualifies.
            for (const auto& p : state.players) {
                if (p.team != opp->team) continue;
                if (p.carId == opp->carId) continue;
                if (p.isDemoed) continue;
                if ((p.pos - defOwnGoal).Length() < oppDistToOwnGoal) {
                    return false;  // someone is further back
                }
            }
            return true;
        }

        // ------------------------------------------------------------------------
        // Front-block geometry: car is between ball and opp goal, within a
        // reasonable lateral band to the ball→goal line. Blocks defender recovery.
        // ------------------------------------------------------------------------
        bool IsInFrontOfBall(const Player& player, const GameState& state) const {
            const Vec oppGoal = (player.team == Team::BLUE)
                ? CommonValues::ORANGE_GOAL_BACK
                : CommonValues::BLUE_GOAL_BACK;

            const Vec ballToGoal = oppGoal - state.ball.pos;
            const float ballGoalDist = ballToGoal.Length();
            if (ballGoalDist < 1.0f) return false;

            const float playerGoalDist = (player.pos - oppGoal).Length();
            if (playerGoalDist > (ballGoalDist - config.frontBlockMinLead)) return false;

            // Lateral distance of player from the ball→goal line
            const Vec dir = ballToGoal / ballGoalDist;
            const Vec ballToPlayer = player.pos - state.ball.pos;
            const float along = ballToPlayer.Dot(dir);
            const Vec perp = ballToPlayer - dir * along;
            if (perp.Length() > config.frontBlockLateralMax) return false;

            return true;
        }
    };

} // namespace RLGC
