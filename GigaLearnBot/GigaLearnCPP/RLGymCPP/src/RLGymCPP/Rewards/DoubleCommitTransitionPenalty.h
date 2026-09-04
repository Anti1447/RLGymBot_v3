#pragma once

/*
 * File: DoubleCommitTransitionPenalty.h
 * Location: RLGymCPP/Rewards/
 *
 * Description:
 *   Fully discrete, event-gated sibling of DoubleCommitPenalty (MultiModalRewards.h).
 *   DoubleCommitPenalty pays -penaltyPerBot*commitCount on EVERY step a non-first-man
 *   player is judged to be illegitimately committing (continuous shaping — total
 *   penalty scales with how many steps the mistake lasts). This class instead pays a
 *   single flat penalty once, on the step a player's double-commit state transitions
 *   from false to true (a "mistake made" event), then stays silent for the rest of
 *   that same continuous episode. 2v2/3v3 only — 1v1 returns 0 via the same
 *   `hasTeammate` gate as the original (no teammate to double-commit with).
 *
 *   This is an architecture comparison, not a behavior replacement — matches the
 *   SimpleAerialTouchReward precedent (see Reward_Change_Log.md 2026-08-28): both
 *   classes are wired alongside each other at the same RewardConfig.cpp sites so
 *   W&B data (this class's TransitionCount/AvgPenalty vs. DoubleCommitPenalty's
 *   PenaltyFrames) decides which one is kept. DoubleCommitPenalty itself is
 *   untouched.
 *
 *   Trigger condition, gates, and constants are copied verbatim from
 *   DoubleCommitPenalty Rev 3.0 (commitDistance, minDistBeforePenalty, the 60-degree
 *   commit cone, the emergency defensive-zone exemption, first-man exclusion by
 *   carId) — see that class's own Rev 2.0/3.0 history comments for why those values
 *   are what they are. The only new behavior is WHEN the (unchanged) condition pays
 *   out, plus two additions that only make sense in a transition-triggered design
 *   (see ClaudeFiles/docs/DoubleCommitPenalty_Discrete_Investigation.md for the full
 *   scenario-by-scenario reasoning behind both):
 *
 *   1. Sustained-before-fire, not cooldown-after-fire: `commitThreshold` (0.5s,
 *      unchanged from the original) must hold with NO interruption before the
 *      transition is considered real. Any single false frame resets the timer to
 *      zero, exactly like the original's `commitStartTime.erase()`. This is what
 *      keeps boundary flicker (a player oscillating right at the distance/angle
 *      gate) from firing multiple discrete events for one continuous marginal
 *      situation — a separate cooldown-after-fire timer would be redundant on top
 *      of this, since a fresh full 0.5s of unbroken commitment is already required
 *      before any re-fire is possible.
 *
 *   2. First-man handoff grace (`firstManGraceSec`): a player who just LOST
 *      first-man status (a teammate's angle to a bouncing ball became shorter,
 *      not any decision this player made) still carries momentum from a read that
 *      was correct when made. The sustained-commit timer does not even START
 *      accumulating until `firstManGraceSec` has passed since this player's last
 *      first-man handoff, so a legitimate bounce-caused swap can't itself trigger
 *      a "double commit" mistake a fraction of a second later. Deliberately NOT
 *      extended to demo-caused handoffs in this revision (Scenario 6 in the
 *      investigation doc) — the reward has no reliable way to distinguish a demo
 *      handoff from a genuine misread from GameState alone, and adding a special
 *      case for it before knowing whether it fires often enough to matter would be
 *      guessing. Watch this class's TransitionCount for anomalies around demos
 *      before extending the grace logic there.
 *
 *   Team-level escalation (investigation Scenario 3: a second illegitimate
 *   committer joins while the first is already mid-episode) is deliberately NOT
 *   re-scored — each player is judged only against their own onset, once. Simpler,
 *   and matches "one variable at a time": if W&B shows escalating pile-ons are
 *   systematically under-penalized, that's a separate, later change.
 *
 * Author: Nathan + Claude
 * Date: August 2026
 * Revision: 1.0
 */

#include "Reward.h"
#include "../CommonValues.h"
#include "../Math.h"
#include <atomic>
#include <cstdint>
#include <unordered_map>

namespace RLGC {

    // ============================================================================
    // DOUBLE COMMIT TRANSITION PENALTY
    // 2026-08-29 | Rev 1.0 | NJH + Claude
    // ============================================================================
    class DoubleCommitTransitionPenalty : public Reward {
    public:
        struct Config {
            float commitDistance       = 1500.0f; // radius: teammate within this of ball counts
            float minDistBeforePenalty = 300.0f;  // no penalty if player is basically on ball
            float commitThreshold      = 0.5f;    // seconds of UNBROKEN commit before the transition fires
            float minCommitSpeed       = 500.0f;  // min speed to count as committing
            float commitAlignCos       = 0.5f;    // cos(angle) threshold — 60 degree cone
            float emergencyGoalDist    = 1000.0f; // ball within this of own goal -> no penalty
            float emergencyGoalHeight  = 1000.0f; // ball below this height near own goal -> no penalty
            float penaltyPerBot        = 0.5f;    // ONE-SHOT penalty per illegitimate committer at fire time
            float firstManGraceSec     = 0.4f;    // suppress the commit timer this long after losing first-man
            bool  shadowMode           = false;
        };

        struct Metrics {
            int    transitionCount = 0;   // number of discrete penalty events fired
            int    commitCountSum  = 0;   // sum of commitCount across fired events (/transitionCount = avg severity)
            double penaltySum      = 0.0; // total penalty magnitude issued
        };

    private:
        struct PlayerState {
            float committingSince      = -1.0f;   // gameTime the unbroken commit condition began; -1 = not accumulating
            bool  wasSustained         = false;   // was the fired-eligible condition true last step (blocks re-fire while continuous)
            bool  wasFirstMan          = false;   // was this player the team's first man last step
            float lastFirstManLossTime = -1000.0f;// gameTime this player last stopped being first man
            int   lastCommitCount      = 0;       // commitCount computed this step, sizes the payout if it fires
        };

        Config config;
        std::unordered_map<uint32_t, PlayerState> _states;
        std::unordered_map<uint32_t, float> _pendingRewards; // this step's payouts only, rebuilt every PreStep
        float _gameTime = 0.0f;

        inline static std::atomic<int>       s_transitionCount{ 0 };
        inline static std::atomic<int>       s_commitCountSum{ 0 };
        inline static std::atomic<long long> s_penaltyMilli{ 0 };

        static bool IsActivelyCommitting(const Player& p, const Vec& ballPos, const Config& cfg) {
            if (p.isDemoed) return false;
            Vec toBall = ballPos - p.pos;
            if (toBall.Length() < 1e-3f) return true;
            float speed = p.vel.Length();
            if (speed < cfg.minCommitSpeed) return false;
            return p.vel.Normalized().Dot(toBall.Normalized()) > cfg.commitAlignCos;
        }

    public:
        static Metrics GetAndResetMetrics() {
            Metrics m;
            m.transitionCount = s_transitionCount.exchange(0);
            m.commitCountSum  = s_commitCountSum.exchange(0);
            long long pm      = s_penaltyMilli.exchange(0);
            m.penaltySum      = double(pm) / 1000.0;
            return m;
        }

        DoubleCommitTransitionPenalty() : config() {}
        explicit DoubleCommitTransitionPenalty(const Config& cfg) : config(cfg) {}

        virtual void Reset(const GameState& initialState) override {
            _states.clear();
            _pendingRewards.clear();
            _gameTime = 0.0f;
        }

        // PreStep runs exactly once per step — all double-commit detection and
        // transition-edge bookkeeping lives here. GetReward runs once per player
        // and must only read the precomputed result, same rationale as every
        // other transition reward in this file set (RoleCycleTransitionReward,
        // PossessionTransitionReward).
        virtual void PreStep(const GameState& state) override {
            _gameTime += (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            _pendingRewards.clear();

            for (const auto& player : state.players) {
                if (player.isDemoed) continue; // suspend, don't erase tracked state

                PlayerState& st = _states[player.carId];

                bool hasTeammate = false;
                for (const auto& p : state.players) {
                    if (p.team == player.team && p.carId != player.carId) { hasTeammate = true; break; }
                }

                float distToBall = (state.ball.pos - player.pos).Length();

                // Identify the team's true first man (closest live teammate to the
                // ball, possibly this player) — same definition as DoubleCommitPenalty.
                uint32_t firstManId = player.carId;
                float    firstManDist = distToBall;
                for (const auto& tm : state.players) {
                    if (tm.team != player.team || tm.carId == player.carId || tm.isDemoed) continue;
                    float tmDist = (tm.pos - state.ball.pos).Length();
                    if (tmDist < firstManDist) { firstManDist = tmDist; firstManId = tm.carId; }
                }
                bool isFirstManNow = (firstManId == player.carId);

                // Record the instant this player stops being first man — the anchor
                // for the handoff grace period below.
                if (st.wasFirstMan && !isFirstManNow)
                    st.lastFirstManLossTime = _gameTime;
                st.wasFirstMan = isFirstManNow;

                bool flagged = false;
                int  commitCount = 0;

                if (hasTeammate && !isFirstManNow && distToBall >= config.minDistBeforePenalty) {
                    Vec ownGoal = (player.team == Team::BLUE)
                        ? CommonValues::BLUE_GOAL_CENTER
                        : CommonValues::ORANGE_GOAL_CENTER;
                    bool emergencyGate = (state.ball.pos - ownGoal).Length() < config.emergencyGoalDist
                                       && state.ball.pos.z < config.emergencyGoalHeight;

                    if (!emergencyGate && distToBall < config.commitDistance &&
                        IsActivelyCommitting(player, state.ball.pos, config)) {
                        for (const auto& tm : state.players) {
                            if (tm.team != player.team || tm.carId == firstManId || tm.isDemoed) continue;
                            float tmDist = (state.ball.pos - tm.pos).Length();
                            if (tmDist < config.commitDistance && IsActivelyCommitting(tm, state.ball.pos, config))
                                commitCount++;
                        }
                        flagged = (commitCount >= 1);
                    }
                }

                // Handoff grace: don't even start the sustain timer while within
                // firstManGraceSec of losing first-man status, so momentum from a
                // read that was correct when made can't itself become a "mistake."
                bool inHandoffGrace = (_gameTime - st.lastFirstManLossTime) < config.firstManGraceSec;

                if (flagged && !inHandoffGrace) {
                    if (st.committingSince < 0.0f)
                        st.committingSince = _gameTime;
                    st.lastCommitCount = commitCount;
                } else {
                    st.committingSince = -1.0f;
                }

                bool sustained = (st.committingSince >= 0.0f) &&
                                 (_gameTime - st.committingSince >= config.commitThreshold);
                bool risingEdge = sustained && !st.wasSustained;

                if (risingEdge) {
                    float penalty = config.penaltyPerBot * st.lastCommitCount;

                    s_transitionCount.fetch_add(1);
                    s_commitCountSum.fetch_add(st.lastCommitCount);
                    s_penaltyMilli.fetch_add(static_cast<long long>(penalty * 1000.0f));

                    if (!config.shadowMode)
                        _pendingRewards[player.carId] = -penalty;
                }

                st.wasSustained = sustained;
            }
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            if (player.isDemoed) return 0.0f;
            auto it = _pendingRewards.find(player.carId);
            return (it != _pendingRewards.end()) ? it->second : 0.0f;
        }
    };

} // namespace RLGC
