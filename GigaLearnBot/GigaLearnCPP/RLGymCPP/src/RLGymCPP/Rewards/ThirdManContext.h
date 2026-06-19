// ============================================================================
// ThirdManContext.h
// Rev 1.0 (2026-06-12, NJH)
//
// Shared per-step context object for ThirdManStallReward and
// CounterLaunchDiagonalReward. Computed ONCE per PreStep call; both reward
// classes read from it without re-deriving teammate positions.
//
// USAGE:
//   Declare one ThirdManContext as a member of a wrapper class or in a shared
//   header, then call Update() from whichever reward's PreStep runs first.
//   Both reward classes hold a pointer/reference to the same instance.
//
//   class ThirdManStallReward : public Reward {
//       ThirdManContext& _ctx;
//   public:
//       ThirdManStallReward(ThirdManContext& ctx, ...) : _ctx(ctx) {}
//       void PreStep(const GameState& s) override { _ctx.Update(s); }
//       ...
//   };
//
//   class CounterLaunchDiagonalReward : public Reward {
//       ThirdManContext& _ctx;
//   public:
//       CounterLaunchDiagonalReward(ThirdManContext& ctx, ...) : _ctx(ctx) {}
//       void PreStep(const GameState& s) override { /* ctx already updated */ }
//       ...
//   };
//
//   // In RewardConfig.cpp:
//   auto* ctx = new ThirdManContext();
//   rewards.push_back(new ThirdManStallReward(*ctx, cfg));          // updates ctx
//   rewards.push_back(new CounterLaunchDiagonalReward(*ctx, cfg));  // reads ctx
//
// MUTUAL EXCLUSION:
//   teammatesRecovering and teammatesUpfield are designed to be mutually
//   exclusive:
//     - recovering: at least one teammate in opponent half
//     - upfield:    ALL teammates in opponent half, moving forward
//   They can both be false (neutral / mixed state). They cannot both be true
//   simultaneously (upfield requires ALL; recovering requires at least one).
//
// THREAD SAFETY:
//   Update() is called once per step from a single PreStep. GetReward() is
//   read-only after that. No locking required in the standard RLGymCPP loop.
// ============================================================================

#pragma once
#include "../Framework.h"
#include "../CommonValues.h"
#include "RotationRewards.h"   // DetermineRoleCommon, RoleType
#include <unordered_map>

namespace RLGC {

struct ThirdManContext {
    // ── Team-level state ─────────────────────────────────────────────────────

    // At least one teammate is currently in the opponent half.
    // Used by StallReward: someone still needs time to recover.
    // Keyed by team (Blue=0, Orange=1).
    bool teammatesRecovering[2] = { false, false };

    // ALL living teammates are in opponent half AND moving toward opponent goal.
    // Used by CounterLaunch: runners are fully committed upfield, launch now.
    bool teammatesUpfield[2] = { false, false };

    // Per-player role cache — FURTHEST role = third man.
    // Keyed by carId.
    std::unordered_map<uint32_t, bool> isThirdMan;

    // Nearest opponent closing speed toward the ball (UU/s).
    // Used by StallReward to detect "under pressure".
    // Keyed by team (index of the team whose third man is stalling).
    float opponentClosingOnBall[2] = { 0.0f, 0.0f };

    // BallPred open-field score for counter-launch direction quality.
    // 1.0 = landing zone has no opponents; 0.0 = fully covered.
    // Set by CounterLaunchDiagonalReward after its BallPred cache update.
    float openFieldScore = 0.0f;

    // ── Mutual exclusion flags ────────────────────────────────────────────────

    // Set to true by ThirdManStallReward when it pays a reward this step.
    // CounterLaunchDiagonalReward checks this: if stall is active, don't launch.
    bool stallActiveThisStep[2] = { false, false };

    // Set to true by CounterLaunchDiagonalReward on a qualifying touch frame.
    // BigClearReward should check this flag and return 0.0f if set, to prevent
    // double-paying the same contact. (See note in CounterLaunchDiagonalReward.)
    bool counterLaunchFiredThisStep[2] = { false, false };

    // ── Update ────────────────────────────────────────────────────────────────

    void Update(const GameState& state) {
        const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        // Reset per-step mutual exclusion flags
        stallActiveThisStep[0]      = stallActiveThisStep[1]      = false;
        counterLaunchFiredThisStep[0] = counterLaunchFiredThisStep[1] = false;
        openFieldScore              = 0.0f;

        // Reset team state
        bool anyInOppHalf[2]    = { false, false };
        bool allInOppHalf[2]    = { true,  true  };
        bool anyMovingBack[2]   = { false, false };
        bool allMovingFwd[2]    = { true,  true  };
        int  livingCount[2]     = { 0, 0 };
        opponentClosingOnBall[0] = opponentClosingOnBall[1] = 0.0f;

        isThirdMan.clear();

        for (const auto& p : state.players) {
            if (p.isDemoed) {
                isThirdMan[p.carId] = false;
                continue;
            }

            int ti = (p.team == Team::BLUE) ? 0 : 1;
            livingCount[ti]++;

            // Opponent half detection:
            //   BLUE attacks +Y, defends -Y  →  opp half = Y > 0
            //   ORANGE attacks -Y, defends +Y →  opp half = Y < 0
            bool inOppHalf = (p.team == Team::BLUE)
                ? (p.pos.y > 0.0f)
                : (p.pos.y < 0.0f);

            if (inOppHalf)  anyInOppHalf[ti] = true;
            else            allInOppHalf[ti] = false;

            // Moving forward = velocity in own attack direction
            float fwdVel = (p.team == Team::BLUE) ? p.vel.y : -p.vel.y;
            if (fwdVel > 200.0f)  { /* moving forward — fine */ }
            else                   { allMovingFwd[ti] = false; }

            if (fwdVel < -200.0f) anyMovingBack[ti] = true; // actively retreating

            // Role: FURTHEST = third man
            isThirdMan[p.carId] = (DetermineRoleCommon(p, state) == RoleType::FURTHEST);
        }

        // Opponent closing speed on ball — compute for each team's perspective
        for (int ti = 0; ti < 2; ++ti) {
            Team myTeam = (ti == 0) ? Team::BLUE : Team::ORANGE;
            float bestClosing = 0.0f;

            for (const auto& p : state.players) {
                if (p.team == myTeam || p.isDemoed) continue;
                Vec toBall = state.ball.pos - p.pos;
                float dist = toBall.Length();
                if (dist < 1.0f) { bestClosing = CommonValues::CAR_MAX_SPEED; break; }
                float closing = (p.vel - state.ball.vel).Dot(toBall / dist);
                if (closing > bestClosing) bestClosing = closing;
            }
            opponentClosingOnBall[ti] = bestClosing;
        }

        // Final team-level flags
        for (int ti = 0; ti < 2; ++ti) {
            // Need at least 2 alive for second/third man semantics to exist
            if (livingCount[ti] < 2) {
                teammatesRecovering[ti] = false;
                teammatesUpfield[ti]    = false;
                continue;
            }

            // Recovering: at least one teammate in opp half
            // (they committed and need time to get back)
            teammatesRecovering[ti] = anyInOppHalf[ti];

            // Upfield: ALL alive teammates in opp half AND all moving forward
            // (full press — safe to counter-launch)
            teammatesUpfield[ti] = allInOppHalf[ti] && allMovingFwd[ti];
        }
    }
};

} // namespace RLGC
