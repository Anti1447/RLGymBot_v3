#pragma once
// KickoffNoChaseTeamPenalty.h — Rev 1.0 (2026-02-23, NJH)
//
// Team-level kickoff penalty. If NO player on a team commits to the ball
// during a kickoff (defined as: touching it OR closing within 600 UU),
// ALL players on that team receive a per-second penalty after the grace period.
//
// Key design difference from KickoffContactPunishmentReward:
//   - That reward: penalizes each individual player who doesn't personally touch.
//   - This reward: team-cleared as soon as ANY ONE teammate commits.
//     Support players rotating to net are safe if their teammate is challenging.
//
// Architecture: PreStep() computes per-team chase status each frame.
//   GetReward() is read-only — reads PreStep results and applies penalty.
//   No mutable state in GetReward.
//
// Pairs with: ClosestPlayerSpeedReward (positive kickoff incentive).
// No ZeroSum wrapper needed — self-contained, applies only to non-chasing team.
//
// Constants:
//   KICKOFF_BALL_RADIUS  = 500 UU  — ball within this of center = kickoff active
//   CHASE_DIST_THRESHOLD = 600 UU  — any player this close = team committed
//   GRACE_PERIOD_SEC     = 1.5s    — corner players need time to get there
//   PENALTY_PER_SECOND   = -0.8    — per-second penalty * weight in RewardConfig
//
// Max penalty exposure: ~0.8 * weight * 0.5s (if kickoff lasts 2s, 1.5s grace)
// At weight 20 in Phase 6: 0.8 * 20 * 0.5 = 8 units per "nobody went for it"
// ============================================================================

#include "Reward.h"
#include "../Math.h"
#include <unordered_map>

namespace RLGC {

    class KickoffNoChaseTeamPenalty : public Reward {
    private:
        // ----------------------------------------------------------------
        // Constants
        // ----------------------------------------------------------------

        // Ball distance from center below which a kickoff is considered active
        static constexpr float KICKOFF_BALL_RADIUS  = 500.0f;
        // Any player on a team within this distance = that team "committed"
        static constexpr float CHASE_DIST_THRESHOLD = 600.0f;
        // Grace period after kickoff start before penalty activates.
        // Corner spawn (~3279 UU) at supersonic needs ~1.4s — 1.5s gives margin.
        static constexpr float GRACE_PERIOD_SEC     = 1.5f;
        // Penalty applied per second to all players on team that hasn't committed.
        // Scaled by deltaTime in GetReward for framerate independence.
        static constexpr float PENALTY_PER_SECOND   = -0.8f;

        // ----------------------------------------------------------------
        // Per-team state
        // Keyed by uint8_t cast of Team enum (BLUE=0, ORANGE=1).
        // All mutation happens in PreStep. GetReward is read-only.
        // ----------------------------------------------------------------
        std::unordered_map<uint8_t, bool>  teamHasChased;    // Has any teammate committed?
        std::unordered_map<uint8_t, float> teamKickoffTimer; // Seconds since kickoff start

        bool kickoffActive = false; // Whether a kickoff is currently in progress

        // ----------------------------------------------------------------
        // Helpers
        // ----------------------------------------------------------------
        bool IsKickoffFrame(const GameState& state) const {
            return state.ball.pos.Length() < KICKOFF_BALL_RADIUS;
        }

        static uint8_t TeamIndex(Team t) {
            return static_cast<uint8_t>(t);
        }

    public:
        // ----------------------------------------------------------------
        // Reset — called at episode start
        // ----------------------------------------------------------------
        virtual void Reset(const GameState&) override {
            teamHasChased.clear();
            teamKickoffTimer.clear();
            kickoffActive = false;
        }

        // ----------------------------------------------------------------
        // PreStep — runs once per step (before all GetReward calls)
        // Detects kickoff start/end, advances timers, checks chase status.
        // ----------------------------------------------------------------
        virtual void PreStep(const GameState& state) override {
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            bool isKickoffNow = IsKickoffFrame(state);

            // --- Kickoff START: initialize per-team tracking ---
            if (isKickoffNow && !kickoffActive) {
                kickoffActive = true;
                teamHasChased.clear();
                teamKickoffTimer.clear();
                // Initialize both teams from the actual player list
                // (handles 1v1, 2v2, 3v3 automatically)
                for (const auto& p : state.players) {
                    uint8_t idx = TeamIndex(p.team);
                    teamHasChased[idx]    = false;
                    teamKickoffTimer[idx] = 0.0f;
                }
            }

            // --- Kickoff END: deactivate ---
            // Maps are left stale — kickoffActive = false prevents GetReward
            // from firing. Maps will be cleared on next kickoff start.
            if (!isKickoffNow && kickoffActive) {
                kickoffActive = false;
                return;
            }

            if (!kickoffActive) return;

            // --- Advance per-team timers ---
            for (auto& kv : teamKickoffTimer)
                kv.second += dt;

            // --- Check each player for chase commitment ---
            for (const auto& p : state.players) {
                uint8_t idx = TeamIndex(p.team);
                // Once committed, team stays committed for this kickoff
                if (teamHasChased[idx]) continue;

                bool touched     = p.ballTouchedStep;
                bool closeEnough = p.pos.Dist(state.ball.pos) < CHASE_DIST_THRESHOLD;
                if (touched || closeEnough) {
                    teamHasChased[idx] = true;
                }
            }
        }

        // ----------------------------------------------------------------
        // GetReward — read-only, called once per player per step
        // Returns per-frame penalty if team has not committed post-grace.
        // ----------------------------------------------------------------
        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            // Only active during kickoffs
            if (!kickoffActive) return 0.0f;

            uint8_t idx = TeamIndex(player.team);

            auto itC = teamHasChased.find(idx);
            auto itT = teamKickoffTimer.find(idx);
            // Edge case: maps not yet populated (first tick of kickoff)
            if (itC == teamHasChased.end() || itT == teamKickoffTimer.end())
                return 0.0f;

            // Team has committed — no penalty for anyone on this team
            if (itC->second) return 0.0f;

            // Still within grace period — give corner players time to arrive
            if (itT->second <= GRACE_PERIOD_SEC) return 0.0f;

            // No commitment after grace: penalize all players on this team
            float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
            return PENALTY_PER_SECOND * dt;
        }
    };

} // namespace RLGC
