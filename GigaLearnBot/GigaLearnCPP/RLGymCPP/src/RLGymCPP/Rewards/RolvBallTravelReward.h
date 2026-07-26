#pragma once
// ============================================================================
// RolvBallTravelReward — Rev 1.0 (2026-06-27, NJH)
// Ported from rolv's BallTravelReward (rlgym-tools)
// Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/ball_travel_reward.py
//
// Rewards based on ball distance traveled between touches. Each touch-to-touch
// transition is weighted by who was involved:
//   selfToSelf    — same player touches again (dribble)
//   selfToTm8     — sender side of a pass
//   tm8ToSelf     — receiver side of a pass
//   selfToOpp     — last toucher gave ball to opponent (turnover penalty)
//   oppToSelf     — interception from opponent
//   selfToOppGoal — last toucher's team scored
//   selfToOwnGoal — last toucher's team conceded
//
// doIntegral=true: accumulates (distance × avg z-height) instead of raw 3D
// distance — weights aerial play more heavily than ground play.
//
// Distance normalization defaults:
//   doIntegral=false: 1 / (2 * BACK_WALL_Y)          → full field length = 1.0
//   doIntegral=true:  4 / (2 * BACK_WALL_Y * CEILING_Z) → half-field × half-ceiling area = 1.0
//
// Architecture note: all cross-touch reward computation happens in PreStep()
// (called once per step). GetReward() only looks up the cached result.
// This prevents per-player inflation in 2v2/3v3 games.
// ============================================================================
#include "Reward.h"
#include <vector>
#include <unordered_map>
#include <cmath>

namespace RLGC {

class RolvBallTravelReward : public Reward {
    float selfToSelfWeight;
    float selfToTm8Weight;
    float tm8ToSelfWeight;
    float selfToOppWeight;
    float oppToSelfWeight;
    float selfToOppGoalWeight;
    float selfToOwnGoalWeight;
    float normalizationFactor;
    bool doIntegral;

    mutable Vec prevBallPos = Vec(0, 0, 0);
    mutable float distanceSinceTouch = 0.0f;
    mutable std::vector<uint32_t> lastTouchIds;
    mutable std::unordered_map<uint32_t, float> pendingRewards;

    const Player* FindPlayer(const GameState& state, uint32_t carId) const {
        for (const auto& p : state.players)
            if (p.carId == carId) return &p;
        return nullptr;
    }

public:
    RolvBallTravelReward(
        float selfToSelf    = 0.0f,
        float selfToTm8     = 0.0f,
        float tm8ToSelf     = 0.0f,
        float selfToOpp     = 0.0f,
        float oppToSelf     = 0.0f,
        float selfToOppGoal = 0.0f,
        float selfToOwnGoal = 0.0f,
        bool  doIntegral    = false,
        float distNorm      = -1.0f  // -1 = use default based on doIntegral
    ) : selfToSelfWeight(selfToSelf),
        selfToTm8Weight(selfToTm8),
        tm8ToSelfWeight(tm8ToSelf),
        selfToOppWeight(selfToOpp),
        oppToSelfWeight(oppToSelf),
        selfToOppGoalWeight(selfToOppGoal),
        selfToOwnGoalWeight(selfToOwnGoal),
        doIntegral(doIntegral)
    {
        if (distNorm < 0.0f) {
            if (doIntegral)
                normalizationFactor = 4.0f / (2.0f * CommonValues::BACK_WALL_Y * CommonValues::CEILING_Z);
            else
                normalizationFactor = 1.0f / (2.0f * CommonValues::BACK_WALL_Y);
        } else {
            normalizationFactor = distNorm;
        }
    }

    virtual void Reset(const GameState& initialState) override {
        prevBallPos = initialState.ball.pos;
        distanceSinceTouch = 0.0f;
        lastTouchIds.clear();
        pendingRewards.clear();
        for (const auto& p : initialState.players)
            if (p.ballTouchedStep) lastTouchIds.push_back(p.carId);
    }

    virtual void PreStep(const GameState& state) override {
        // Accumulate ball distance (or height-weighted integral) since last touch
        Vec bp = state.ball.pos;
        float dx = bp.x - prevBallPos.x;
        float dy = bp.y - prevBallPos.y;
        float dz = bp.z - prevBallPos.z;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
        if (doIntegral) {
            float avgZ = (bp.z + prevBallPos.z) * 0.5f;
            dist *= avgZ;
        }
        prevBallPos = bp;
        distanceSinceTouch += dist;

        // Collect players touching the ball this step
        std::vector<uint32_t> touchingIds;
        for (const auto& p : state.players)
            if (p.ballTouchedStep) touchingIds.push_back(p.carId);

        // Reset pending rewards
        pendingRewards.clear();
        for (const auto& p : state.players) pendingRewards[p.carId] = 0.0f;

        float normDist = distanceSinceTouch * normalizationFactor;

        // Assign rewards for each last-toucher → current-toucher pair
        for (uint32_t id1 : lastTouchIds) {
            for (uint32_t id2 : touchingIds) {
                const Player* p1 = FindPlayer(state, id1);
                const Player* p2 = FindPlayer(state, id2);
                if (!p1 || !p2) continue;

                if (id1 == id2) {
                    pendingRewards[id1] += normDist * selfToSelfWeight;
                } else if (p1->team == p2->team) {
                    pendingRewards[id1] += normDist * selfToTm8Weight;
                    pendingRewards[id2] += normDist * tm8ToSelfWeight;
                } else {
                    pendingRewards[id1] += normDist * selfToOppWeight;
                    pendingRewards[id2] += normDist * oppToSelfWeight;
                }
            }
        }

        // Goal reward: fires on the step the ball enters the net
        if (state.goalScored) {
            // RS_TEAM_FROM_Y returns the team whose net the ball is in (conceding team)
            Team concedingTeam = RS_TEAM_FROM_Y(state.ball.pos.y);
            for (uint32_t ltaId : lastTouchIds) {
                const Player* lta = FindPlayer(state, ltaId);
                if (!lta) continue;
                if (lta->team != concedingTeam)
                    pendingRewards[ltaId] += normDist * selfToOppGoalWeight;
                else
                    pendingRewards[ltaId] += normDist * selfToOwnGoalWeight;
            }
        }

        // Normalize when multiple players touched in the same step
        if (!touchingIds.empty()) {
            int normFactor = (int)touchingIds.size()
                           * (int)(lastTouchIds.empty() ? 1 : lastTouchIds.size());
            if (normFactor > 1) {
                for (auto& kv : pendingRewards)
                    kv.second /= (float)normFactor;
            }
            lastTouchIds = touchingIds;
            distanceSinceTouch = 0.0f;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        auto it = pendingRewards.find(player.carId);
        return (it != pendingRewards.end()) ? it->second : 0.0f;
    }
};

} // namespace RLGC
