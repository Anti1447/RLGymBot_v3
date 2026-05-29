#pragma once
#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"
#include "../CommonValues.h"
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace RLGC {

// ============================================================
// PLN — Passing Lane Network utilities (Slater methodology)
//
// Field is treated as a dynamic influence graph.
// Each teammate i has a Passing Lane Score:
//   L_i = α_i / (1 + I_ij)
// where:
//   α_i  = alignment × BoostFactor × SpeedScaling  (threat level)
//   I_ij = Σ exp(-k × dist_to_lane) × OppReachability  (coverage)
// ============================================================
namespace PLN {

    // k calibrated so exp(-k * 350) = 0.5  (350 UU ≈ half car-width of coverage)
    static constexpr float PLN_K_DEFAULT = 0.00198f;

    // Minimum perpendicular distance from point P to segment A→B
    inline float PointToSegmentDist(const Vec& P, const Vec& A, const Vec& B) {
        Vec AB = B - A;
        float lenSq = AB.x*AB.x + AB.y*AB.y + AB.z*AB.z;
        if (lenSq < 1e-8f) return (P - A).Length();
        float t = RS_CLAMP((P - A).Dot(AB) / lenSq, 0.0f, 1.0f);
        return (P - (A + AB * t)).Length();
    }

    // [0.5, 1.0] — full boost doubles effective threat
    inline float BoostFactor(float b) {
        return 0.5f + 0.5f * (RS_CLAMP(b, 0.f, 100.f) / 100.f);
    }

    // [0.4, 1.2] — supersonic = peak threat
    inline float SpeedScaling(float speed) {
        const float SUPERSONIC = 2200.0f;
        if (speed <= 0.f)        return 0.4f;
        if (speed >= SUPERSONIC) return 1.2f;
        return 0.4f + 0.6f * (speed / SUPERSONIC);
    }

    // Opponent's ability to contest the lane.
    // Reduced when: demoed, committed aerial (no jumps left), or facing away.
    inline float OppReachability(const Player& opp, const Vec& laneDir) {
        if (opp.isDemoed) return 0.0f;
        float facingDot    = opp.rotMat.forward.Dot(laneDir);
        float facingFactor = RS_CLAMP(0.55f + 0.45f * facingDot, 0.1f, 1.0f);
        if (!opp.isOnGround && opp.hasDoubleJumped && !opp.isFlipping)
            facingFactor *= 0.35f;  // committed aerial — can't contest ground lane
        return facingFactor;
    }

    // α_i — how threatening this player is as a passing target right now.
    // = velocity alignment toward ball × BoostFactor × SpeedScaling
    inline float ComputeAlpha(const Vec& tPos, const Vec& tVel,
                               const Vec& ballPos, float boost, bool isDemoed) {
        if (isDemoed) return 0.0f;
        Vec toBall = ballPos - tPos;
        if (toBall.Length() < 50.f) return 0.0f;   // already at ball
        float tSpeed       = tVel.Length();
        float dirAlignment = 0.0f;
        if (tSpeed > 50.f)
            dirAlignment = RS_CLAMP(SafeNorm(tVel).Dot(SafeNorm(toBall)), 0.f, 1.f);
        return dirAlignment * BoostFactor(boost) * SpeedScaling(tSpeed);
    }

    // I_ij — total opponent interference on the lane from ballPos → tPos
    inline float ComputeInterference(const Vec& ballPos, const Vec& tPos,
                                      const std::vector<Player>& allPlayers,
                                      Team friendlyTeam, float k) {
        Vec laneDir  = SafeNorm(tPos - ballPos);
        float totalI = 0.0f;
        for (const auto& opp : allPlayers) {
            if (opp.team == friendlyTeam) continue;
            float dist  = PointToSegmentDist(opp.pos, ballPos, tPos);
            totalI     += std::exp(-k * dist) * OppReachability(opp, laneDir);
        }
        return totalI;
    }

    // L_i — single passing lane score for one target
    inline float ComputeLaneScore(float alpha, float interference) {
        return alpha / (1.0f + interference);
    }

    // Σ L_i for all friendly non-self players (teammates as passing targets)
    inline float TotalTeamLaneScore(const Player& player, const GameState& state, float k) {
        float total = 0.0f;
        for (const auto& other : state.players) {
            if (other.carId == player.carId || other.team != player.team || other.isDemoed) continue;
            float alpha        = ComputeAlpha(other.pos, other.vel, state.ball.pos, other.boost, other.isDemoed);
            float interference = ComputeInterference(state.ball.pos, other.pos, state.players, player.team, k);
            total             += ComputeLaneScore(alpha, interference);
        }
        return total;
    }

    // Σ L_i for all opponent players (from their own team's perspective)
    inline float TotalOppLaneScore(const Player& player, const GameState& state, float k) {
        Team oppTeam = (player.team == Team::BLUE) ? Team::ORANGE : Team::BLUE;
        float total  = 0.0f;
        for (const auto& opp : state.players) {
            if (opp.team == player.team || opp.isDemoed) continue;
            float alpha        = ComputeAlpha(opp.pos, opp.vel, state.ball.pos, opp.boost, opp.isDemoed);
            float interference = ComputeInterference(state.ball.pos, opp.pos, state.players, oppTeam, k);
            total             += ComputeLaneScore(alpha, interference);
        }
        return total;
    }

} // namespace PLN


// ============================================================
// PassingLaneNetworkReward
//
// Per step: wOffensive * Σ(team lane scores) - wDefensive * Σ(opp lane scores)
//
// No ZeroSum wrapper needed — naturally competitive:
//   offensive term rewards open team lanes,
//   defensive term penalises open opponent lanes.
// Returns 0.0f in 1v1 (no teammates → no lanes to model).
// ============================================================
class PassingLaneNetworkReward : public Reward {
public:
    struct Config {
        float wOffensive = 1.0f;
        float wDefensive = 0.6f;
        float k          = PLN::PLN_K_DEFAULT;

        // Phase 4 — gentle introduction
        static Config Warm()     { return {0.5f, 0.3f, PLN::PLN_K_DEFAULT}; }
        // Phase 5 — balanced signal
        static Config Balanced() { return Config{}; }
        // Phase 6-7 — full emphasis
        static Config Full()     { return {1.5f, 0.9f, PLN::PLN_K_DEFAULT}; }
    };

private:
    Config cfg;

public:
    explicit PassingLaneNetworkReward(Config c = Config::Balanced()) : cfg(c) {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // 1v1 has no teammates → no passing lanes to model
        if ((int)state.players.size() < 4) return 0.0f;

        // Count living teammates and opponents for normalization.
        // Without this, the reward sums N-1 lanes in 3v3 vs 1 in 2v2, making the
        // effective weight 2× larger in 3v3 and causing inconsistent gradients across modes.
        int numTeammates = 0, numOpponents = 0;
        for (const auto& p : state.players) {
            if (p.isDemoed) continue;
            if (p.team == player.team && p.carId != player.carId) numTeammates++;
            else if (p.team != player.team) numOpponents++;
        }

        float offScore = (numTeammates > 0)
            ? PLN::TotalTeamLaneScore(player, state, cfg.k) / (float)numTeammates : 0.0f;
        float defScore = (numOpponents > 0)
            ? PLN::TotalOppLaneScore(player, state, cfg.k)  / (float)numOpponents : 0.0f;
        return cfg.wOffensive * offScore - cfg.wDefensive * defScore;
    }
};


// ============================================================
// SpatialFakeReward
//
// Rewards a player for successfully creating an open passing lane
// by making a fake run / bait move:
//   1. IDLE       → THREATENING: player's α_i rises above armThreshold
//   2. THREATENING → PENDING_EVAL: player's α_i drops below disengageThresh
//   3. PENDING_EVAL: if EMA(team lane score) rises by ≥ laneGainThreshold
//                    within windowSec, award wFake * laneGain
//
// EMA smoothing on lane score prevents frame-to-frame noise (lesson
// from TTI integration: delta-based terms cause entropy spikes).
// Returns 0.0f in 1v1.
// ============================================================
class SpatialFakeReward : public Reward {
public:
    struct Config {
        float wFake             = 1.0f;
        float armThreshold      = 0.30f; // α_i must exceed this to arm (was 0.5 — too strict, reward was dead at 0.0001 effective)
        float disengageThresh   = 0.15f; // α_i must drop below this to fire (was 0.2)
        float laneGainThreshold = 0.025f;// min EMA lane gain to award reward (was 0.05)
        float windowSec         = 0.40f; // evaluation window after disengage
        float emaAlpha          = 0.15f; // EMA smoothing factor (lower = smoother)
        float k                 = PLN::PLN_K_DEFAULT;
    };

private:
    struct FakeState {
        enum Phase { IDLE, THREATENING, PENDING_EVAL };
        Phase phase          = IDLE;
        float timeLeft       = 0.0f;
        float laneScoreAtArm = 0.0f;
        float emaLaneScore   = 0.0f;
        bool  initialized    = false;
        float rearmCooldown  = 0.0f;
    };

    Config cfg;
    mutable std::unordered_map<uint32_t, FakeState> states;

public:
    explicit SpatialFakeReward(Config c = Config()) : cfg(c) {}

    virtual void Reset(const GameState&) override { states.clear(); }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if ((int)state.players.size() < 4) return 0.0f;

        auto& fs = states[player.carId];
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        // Tick down re-arm cooldown every step
        fs.rearmCooldown = RS_MAX(0.0f, fs.rearmCooldown - dt);

        // Update EMA of current team lane score
        float currentLane = PLN::TotalTeamLaneScore(player, state, cfg.k);
        if (!fs.initialized) {
            fs.emaLaneScore = currentLane;
            fs.initialized  = true;
        } else {
            fs.emaLaneScore = cfg.emaAlpha * currentLane + (1.0f - cfg.emaAlpha) * fs.emaLaneScore;
        }

        // Player's own current threat level
        float myAlpha = PLN::ComputeAlpha(player.pos, player.vel,
                                           state.ball.pos, player.boost, player.isDemoed);

        switch (fs.phase) {
            case FakeState::IDLE:
                if (fs.rearmCooldown <= 0.0f && myAlpha >= cfg.armThreshold) {
                    fs.phase          = FakeState::THREATENING;
                    fs.timeLeft       = cfg.windowSec;
                    fs.laneScoreAtArm = fs.emaLaneScore;
                }
                break;

            case FakeState::THREATENING:
                fs.timeLeft -= dt;
                if (myAlpha < cfg.disengageThresh) {
                    // Fake triggered — player disengaged; open evaluation window
                    fs.phase    = FakeState::PENDING_EVAL;
                    fs.timeLeft = cfg.windowSec;
                } else if (fs.timeLeft <= 0.0f) {
                    // Threat window expired without faking — reset
                    fs.phase = FakeState::IDLE;
                }
                break;

            case FakeState::PENDING_EVAL:
                fs.timeLeft -= dt;
                {
                    float laneGain = fs.emaLaneScore - fs.laneScoreAtArm;
                    if (laneGain >= cfg.laneGainThreshold) {
                        // Lane opened up as a result of the fake — award reward
                        fs.rearmCooldown = 2.0f;
                        fs.phase = FakeState::IDLE;
                        return cfg.wFake * laneGain;
                    }
                }
                if (fs.timeLeft <= 0.0f) {
                    // Evaluation window expired — no lane gain detected
                    fs.phase = FakeState::IDLE;
                }
                break;
        }
        return 0.0f;
    }
};

} // namespace RLGC
