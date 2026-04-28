#pragma once
#include "../Framework.h"
#include "Reward.h"
#include "../CommonValues.h"

namespace RLGC {

// ============================================================================
// PossessionSideDemoReward
// ============================================================================
// Rewards demos/bumps on the LAST DEFENDER when YOUR team has possession and
// has a numerical advantage near the play. The tactical pattern: you control
// the ball, the opposing defender is all that stands between you (or your
// teammate) and a goal, and you take them out of the play.
//
// Two modes are evaluated within a single class:
//
//   Mode A (1v1 possession):
//     - You individually have possession
//     - No teammate exists to finish, so YOU must still be near the ball
//       after the demo (no ball-abandon chasing)
//
//   Mode B (numerical advantage possession, 2v1 / 2v2+ with edge):
//     - Your team has possession
//     - A teammate is positioned to finish (within 1500uu of ball AND
//       closer to opponent goal than the ball is)
//     - You ARE allowed to abandon the ball to remove the defender
//
// ANTI-FARMING GATES (all must hold on the event frame):
//   - Target must be the closest opponent to the ball (the real defender)
//   - Ball must be on the attacking half OR moving toward opponent goal
//   - Not during a kickoff (ball near center + low speed)
//   - Teammate is not the bump target
//   - Per-opponent cooldown (no repeat-target farming during respawn cycles)
//   - Qualifying possession state (recent touch by our team, or current
//     dribble-style control)
//
// SCALING:
//   - Demo pays ~1.0x base, bump pays 0.75x (matches AirBumpDemoReward ratio)
//   - Depth bonus: deeper in opponent half = more reward (linear ramp)
//   - Speed bonus: faster car = more reward (soft ramp above 1200uu)
//
// No shadow wrapper. Intended for direct integration at low initial weight.
//
// 2026-04-18 | Rev 1.0 | NJH
// ============================================================================
class PossessionSideDemoReward : public Reward {
public:
    struct Config {
        // ---- Reward magnitudes ----
        float baseDemoReward        = 1.0f;     // Base event value for a qualifying demo
        float bumpMultiplier        = 0.75f;    // Bump pays 75% of demo
        float depthBonusMax         = 0.6f;     // Max additional reward for demos deep in opp half
        float speedBonusMax         = 0.4f;     // Max additional reward for high-speed demos

        // ---- Possession qualification ----
        float teamTouchWindow       = 1.5f;     // Seconds since last team touch to still count as possession
        float possessionBallRadius  = 350.0f;   // Distance for "currently in possession" dribble state
        float possessionVelAlign    = 0.3f;     // Player/ball velocity alignment threshold

        // ---- Target verification ----
        float contactRadius         = 300.0f;   // Must be within this of target at event frame
        float lastDefenderMaxDist   = 400.0f;   // Target must be within this of the "closest to ball" slot

        // ---- Numerical advantage / teammate finisher ----
        float teammateFinisherRadius = 1500.0f; // Teammate must be within this of ball to be a "finisher"
        // Finisher must also be closer to opponent goal than the ball itself.

        // ---- Ball-abandon check (Mode A only, 1v1) ----
        float maxAbandonDistance    = 2000.0f;  // In 1v1, must be within this of ball at demo time

        // ---- Field gating ----
        float attackingHalfSign     = 1.0f;     // Set by code; ball must be on opponent side OR moving toward opp goal
        float ballTowardGoalMinDot  = 0.0f;     // Ball velocity alignment with toward-opp-goal direction

        // ---- Kickoff mask ----
        float kickoffBallDistMax    = 500.0f;   // Ball within this of center = possible kickoff
        float kickoffBallSpeedMax   = 200.0f;   // AND ball slow = kickoff
        float kickoffMaskDuration   = 3.0f;     // Mask first N seconds of a kickoff

        // ---- Cooldown ----
        float perOpponentCooldown   = 5.0f;     // Seconds before same opponent can reward again

        // ---- Quality ramps ----
        float minCarSpeedForBonus   = 1200.0f;  // Below this = no speed bonus
        float maxCarSpeedForBonus   = 2300.0f;  // Full speed bonus at supersonic
        float enemyGoalDepthRange   = 4000.0f;  // Normalization for depth bonus (roughly field half-length)
    };

private:
    Config config;

    struct PlayerMem {
        float lastTeamTouchTime = -1000.0f;
        float timeSinceKickoff  = 1000.0f;
        std::map<uint32_t, float> opponentCooldowns; // opponentCarId -> seconds remaining
        // Prev-frame demo/bump counters for edge detection (match eventState pattern)
        int   prevDemoCount = 0;
        int   prevBumpCount = 0;
    };

    mutable std::map<uint32_t, PlayerMem> mem;
    mutable float gameTime = 0.0f;
    mutable float lastDt = 0.016f;

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    Vec OpponentGoalBack(Team team) const {
        return (team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;
    }

    // Sign multiplier: +1 if "toward opponent goal" is +Y for this team, -1 otherwise
    float AttackingSign(Team team) const {
        return (team == Team::BLUE) ? 1.0f : -1.0f;
    }

    bool IsKickoffState(const GameState& state) const {
        float ballDist = state.ball.pos.Length();
        float ballSpd  = state.ball.vel.Length();
        return (ballDist < config.kickoffBallDistMax) && (ballSpd < config.kickoffBallSpeedMax);
    }

    // Find the opponent closest to the ball (the "last defender" candidate).
    const Player* FindLastDefender(const Player& player, const GameState& state) const {
        const Player* best = nullptr;
        float bestDist = 1e9f;
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            if (opp.isDemoed) continue;
            float d = (opp.pos - state.ball.pos).Length();
            if (d < bestDist) {
                bestDist = d;
                best = &opp;
            }
        }
        return best;
    }

    // Count opponents still alive (not demoed) for numerical-advantage check.
    int CountLiveOpponents(const Player& player, const GameState& state) const {
        int n = 0;
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            if (opp.isDemoed) continue;
            n++;
        }
        return n;
    }

    // Count live teammates (excluding self).
    int CountLiveTeammates(const Player& player, const GameState& state) const {
        int n = 0;
        for (const auto& tm : state.players) {
            if (tm.team != player.team) continue;
            if (tm.carId == player.carId) continue;
            if (tm.isDemoed) continue;
            n++;
        }
        return n;
    }

    // Does our team have possession? Returns true if any of:
    //   - I currently have dribble-style possession (close ball, aligned)
    //   - I touched the ball within teamTouchWindow
    //   - A live teammate currently has possession
    //   - A live teammate touched the ball within teamTouchWindow (tracked via player.ballTouchedStep
    //     + memory; simplified here to "any teammate is closest to ball AND close")
    bool TeamHasPossession(const Player& player, const GameState& state) const {
        auto& m = mem[player.carId];

        // Recent team touch
        if (gameTime - m.lastTeamTouchTime < config.teamTouchWindow)
            return true;

        // Current dribble-style possession check for any teammate or self
        auto hasDribbleControl = [&](const Player& p) -> bool {
            float d = (p.pos - state.ball.pos).Length();
            if (d > config.possessionBallRadius) return false;
            if (p.vel.Length() < 100.0f || state.ball.vel.Length() < 100.0f) {
                return d < config.possessionBallRadius * 0.6f; // slow ball, must be very close
            }
            Vec pDir = p.vel.Normalized();
            Vec bDir = state.ball.vel.Normalized();
            return pDir.Dot(bDir) > config.possessionVelAlign;
        };

        for (const auto& p : state.players) {
            if (p.team != player.team) continue;
            if (p.isDemoed) continue;
            if (hasDribbleControl(p)) return true;
        }

        return false;
    }

    // Does a finisher teammate exist near the ball and in attacking position?
    // Used by Mode B (numerical-advantage) to allow ball-abandon demos.
    bool HasFinisherTeammate(const Player& player, const GameState& state) const {
        Vec oppGoal = OpponentGoalBack(player.team);
        float ballDistToOppGoal = (state.ball.pos - oppGoal).Length();

        for (const auto& tm : state.players) {
            if (tm.team != player.team) continue;
            if (tm.carId == player.carId) continue;
            if (tm.isDemoed) continue;

            float tmDistToBall    = (tm.pos - state.ball.pos).Length();
            float tmDistToOppGoal = (tm.pos - oppGoal).Length();

            if (tmDistToBall > config.teammateFinisherRadius) continue;
            if (tmDistToOppGoal >= ballDistToOppGoal) continue; // must be closer to opp goal than ball

            return true;
        }
        return false;
    }

    // Is the ball in attacking territory OR moving toward opponent goal?
    bool BallInAttackingContext(const Player& player, const GameState& state) const {
        float sideSign = AttackingSign(player.team);
        bool  ballInOppHalf = (sideSign * state.ball.pos.y) > 0.0f;

        Vec oppGoal = OpponentGoalBack(player.team);
        Vec toOppGoal = (oppGoal - state.ball.pos);
        if (toOppGoal.Length() < 1e-3f) return ballInOppHalf;
        toOppGoal = toOppGoal.Normalized();
        float ballTowardGoal = state.ball.vel.Normalized().Dot(toOppGoal);

        return ballInOppHalf || (ballTowardGoal > config.ballTowardGoalMinDot && state.ball.vel.Length() > 300.0f);
    }

    // Identify which opponent the player demoed/bumped this frame, if any.
    // Returns nullptr if no opponent was demoed/bumped within contact radius.
    const Player* FindContactedOpponent(const Player& player, const GameState& state,
                                        bool& outWasDemo) const {
        outWasDemo = false;
        const Player* hit = nullptr;
        float closestDist = 1e9f;

        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;

            // Prefer demoed opponents (strongest signal)
            if (opp.eventState.demoed) {
                float d = (opp.pos - player.pos).Length();
                if (d < config.contactRadius && d < closestDist) {
                    closestDist = d;
                    hit = &opp;
                    outWasDemo = true;
                }
            }
        }
        if (hit) return hit;

        // Fall back to bumped opponents
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            if (opp.eventState.bumped) {
                float d = (opp.pos - player.pos).Length();
                if (d < config.contactRadius && d < closestDist) {
                    closestDist = d;
                    hit = &opp;
                    outWasDemo = false;
                }
            }
        }
        return hit;
    }

    // Compute the final scaled reward value given a qualifying hit.
    float ComputeHitValue(const Player& player, const GameState& state,
                          const Player& target, bool wasDemo) const {
        float base = wasDemo
            ? config.baseDemoReward
            : (config.baseDemoReward * config.bumpMultiplier);

        // Depth bonus: how deep in opponent half is the target?
        Vec oppGoal = OpponentGoalBack(player.team);
        float targetDistToGoal = (target.pos - oppGoal).Length();
        float depthRatio = 1.0f - std::min(1.0f, targetDistToGoal / config.enemyGoalDepthRange);
        float depthBonus = std::max(0.0f, depthRatio) * config.depthBonusMax;

        // Speed bonus: soft ramp from minCarSpeedForBonus -> maxCarSpeedForBonus
        float carSpeed = player.vel.Length();
        float speedBonus = 0.0f;
        if (carSpeed > config.minCarSpeedForBonus) {
            float t = (carSpeed - config.minCarSpeedForBonus) /
                      (config.maxCarSpeedForBonus - config.minCarSpeedForBonus);
            speedBonus = std::min(1.0f, std::max(0.0f, t)) * config.speedBonusMax;
        }

        return base + depthBonus + speedBonus;
    }

public:
    PossessionSideDemoReward() = default;
    explicit PossessionSideDemoReward(const Config& cfg) : config(cfg) {}

    virtual void Reset(const GameState& initialState) override {
        mem.clear();
        gameTime = 0.0f;
        lastDt = 0.016f;
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
        lastDt = dt;
        gameTime += dt;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        auto& m = mem[player.carId];

        // ---- Cooldown decay ----
        for (auto it = m.opponentCooldowns.begin(); it != m.opponentCooldowns.end(); ) {
            it->second -= lastDt;
            if (it->second <= 0.0f) it = m.opponentCooldowns.erase(it);
            else ++it;
        }

        // ---- Track team touch time for possession window ----
        if (player.ballTouchedStep) {
            // Update our own team's touch time for every teammate's memory block
            for (auto& p : state.players) {
                if (p.team != player.team) continue;
                mem[p.carId].lastTeamTouchTime = gameTime;
            }
        }

        // ---- Kickoff mask ----
        if (IsKickoffState(state)) {
            m.timeSinceKickoff = 0.0f;
            return 0.0f;
        } else {
            m.timeSinceKickoff += lastDt;
        }
        if (m.timeSinceKickoff < config.kickoffMaskDuration) return 0.0f;

        // ---- Event edge detection: demo/bump counter increased this frame? ----
        int curDemo = player.eventState.demo;
        int curBump = player.eventState.bump;
        bool demoEvent = (curDemo > m.prevDemoCount);
        bool bumpEvent = (curBump > m.prevBumpCount);
        m.prevDemoCount = curDemo;
        m.prevBumpCount = curBump;

        if (!demoEvent && !bumpEvent) return 0.0f;

        // ---- Resolve contacted opponent ----
        bool wasDemo = false;
        const Player* target = FindContactedOpponent(player, state, wasDemo);
        if (!target) return 0.0f;

        // Teammate bump guard (defense in depth; FindContactedOpponent already filters)
        if (target->team == player.team) return 0.0f;

        // ---- Gate 1: Team possession ----
        if (!TeamHasPossession(player, state)) return 0.0f;

        // ---- Gate 2: Attacking context ----
        if (!BallInAttackingContext(player, state)) return 0.0f;

        // ---- Gate 3: Target is the last defender (closest opp to ball) ----
        const Player* lastDef = FindLastDefender(player, state);
        if (!lastDef) return 0.0f;
        if (lastDef->carId != target->carId) {
            // Allow some slack: target is "essentially" the last defender if within short distance
            float slack = (target->pos - lastDef->pos).Length();
            if (slack > config.lastDefenderMaxDist) return 0.0f;
        }

        // ---- Gate 4: Mode selection and ball-abandon check ----
        int liveOpps = CountLiveOpponents(player, state);
        int liveTms  = CountLiveTeammates(player, state);
        bool numericalAdvantage = (liveTms + 1) > liveOpps; // include self

        bool modeB_allowed = false;
        if (numericalAdvantage && HasFinisherTeammate(player, state)) {
            modeB_allowed = true;
        }

        if (!modeB_allowed) {
            // Mode A: 1v1 or no finisher teammate available.
            // Must not have abandoned the ball to chase the demo.
            float distToBall = (player.pos - state.ball.pos).Length();
            if (distToBall > config.maxAbandonDistance) return 0.0f;
        }

        // ---- Gate 5: Per-opponent cooldown ----
        auto cd = m.opponentCooldowns.find(target->carId);
        if (cd != m.opponentCooldowns.end() && cd->second > 0.0f) return 0.0f;

        // ---- All gates passed: compute value, arm cooldown, return ----
        float value = ComputeHitValue(player, state, *target, wasDemo);
        m.opponentCooldowns[target->carId] = config.perOpponentCooldown;
        return value;
    }
};

} // namespace RLGC
