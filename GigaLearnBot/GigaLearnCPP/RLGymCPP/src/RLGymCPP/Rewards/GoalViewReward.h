#pragma once
// GoalViewReward.h
//
// Port of GoalViewReward from rlgym-tools by Rolv.
// Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/goal_prob_reward.py
//
// Potential-based shaping (Ng et al. 1999):
//   F(s, s') = gamma * Phi(s') - Phi(s),  where Phi(s) = 2*p(s) - 1
//
// p(s) = probability that blue scores, estimated as the fraction of the
// full-sphere solid angle subtended by the orange goal mouth vs. both goals.
//
// Properties:
//   - Mathematically unfarmable: reward telescopes to Phi(s_T) - Phi(s_0) ~ 0
//   - No ZeroSumReward wrapper needed: GetReward already returns +/- by team
//   - gamma MUST match training discount (~0.99), not 1.0

#include "Reward.h"
#include "../Math.h"
#include <cmath>

namespace RLGC {

class GoalViewReward : public Reward {
private:
    float _gamma;
    float _prevProb;    // Phi(s_{t-1}), set in Reset(), updated each PreStep
    float _stepReward;  // Cached delta for current step; read in GetReward

    // Solid angle of spherical triangle ABC as seen from O
    // (Eriksson 1995 / Van Oosterom & Strackee 1983).
    //   numerator   = |a . (b x c)|  (scalar triple product, always >= 0)
    //   denominator = 1 + a.b + b.c + c.a
    // Note: denominator can be <= 0 when ball is very close to the goal plane.
    // atan2f handles this gracefully; probability is clamped to [0,1] downstream.
    static float SolidAngleEriksson(Vec O, Vec A, Vec B, Vec C) {
        Vec a = (A - O).Normalized();
        Vec b = (B - O).Normalized();
        Vec c = (C - O).Normalized();
        float numerator   = fabsf(a.Cross(b).Dot(c));
        float denominator = 1.0f + a.Dot(b) + b.Dot(c) + c.Dot(a);
        return 2.0f * atan2f(numerator, denominator);
    }

    // Fraction of the full sphere (4*pi sr) subtended by a goal mouth at goalY.
    // margin = BALL_RADIUS keeps the ball centre from clipping "inside" the boundary.
    static float ViewGoalRatio(Vec ballPos, float goalY,
                               float margin = CommonValues::BALL_RADIUS) {
        static constexpr float FOUR_PI = 4.0f * 3.14159265358979f;
        float halfW = CommonValues::GOAL_WIDTH_FROM_CENTER - margin;
        float minZ  = margin;
        float maxZ  = CommonValues::GOAL_HEIGHT - margin;

        //  tl ------- tr
        //  |   goal   |
        //  bl ------- br
        Vec bl(-halfW, goalY, minZ);
        Vec br( halfW, goalY, minZ);
        Vec tl(-halfW, goalY, maxZ);
        Vec tr( halfW, goalY, maxZ);

        // Rectangle tiled as two triangles sharing diagonal br-tl
        float sa1 = SolidAngleEriksson(ballPos, bl, br, tl);
        float sa2 = SolidAngleEriksson(ballPos, br, tr, tl);
        return (sa1 + sa2) / FOUR_PI;
    }

    // Probability in [0,1] that blue scores from the given ball position.
    // Orange goal is at +Y (blue attacks it); blue goal is at -Y.
    float CalcBlueGoalProb(Vec ballPos) const {
        float viewOrange = ViewGoalRatio(ballPos,  CommonValues::BACK_WALL_Y);  // blue scoring
        float viewBlue   = ViewGoalRatio(ballPos, -CommonValues::BACK_WALL_Y);  // orange scoring
        float total = viewOrange + viewBlue;
        if (total < 1e-9f) return 0.5f;
        // Clamp to [0,1]: guards against negative-denominator edge case near goal plane
        return RS_CLAMP(viewOrange / total, 0.0f, 1.0f);
    }

public:
    // gamma: MUST match training discount factor (typically 0.99), not 1.0.
    // Using gamma=1.0 with a training discount of 0.99 introduces a systematic
    // per-step bias (0.01 * Phi(s)) that accumulates to ~9 reward units over a
    // 300-step episode, violating the Ng et al. shaping guarantee.
    GoalViewReward(float gamma = 0.99f)
        : _gamma(gamma), _prevProb(0.5f), _stepReward(0.0f) {}

    virtual void Reset(const GameState& initialState) override {
        _prevProb   = CalcBlueGoalProb(initialState.ball.pos);
        _stepReward = 0.0f;
    }

    // Called once per physics step before any GetReward calls.
    // Computes the potential-shaping delta and caches it for all players this step.
    virtual void PreStep(const GameState& state) override {
        float newProb;

        if (state.goalScored) {
            // Terminal step: force probability to certainty.
            // Primary: find the player whose eventState.goal fired.
            bool blueScored = false;
            bool found      = false;
            for (const auto& p : state.players) {
                if (p.eventState.goal) {
                    blueScored = (p.team == Team::BLUE);
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Fallback: orange goal is at +Y, so ball.pos.y > 0 => blue scored
                blueScored = (state.ball.pos.y > 0.0f);
            }
            newProb = blueScored ? 1.0f : 0.0f;
        } else {
            newProb = CalcBlueGoalProb(state.ball.pos);
        }

        // Ng et al. (1999) shaping: F(s, s') = gamma * Phi(s') - Phi(s)
        _stepReward = _gamma * (2.0f * newProb  - 1.0f)
                    -          (2.0f * _prevProb - 1.0f);
        _prevProb = newProb;
    }

    // Blue players receive +stepReward; orange receive -stepReward.
    // The class is already zero-sum by team — do NOT wrap in ZeroSumReward.
    // In 2v2/3v3 all teammates automatically receive the same team-level value.
    virtual float GetReward(const Player& player, const GameState& state,
                            bool isFinal) override {
        return (player.team == Team::BLUE) ? _stepReward : -_stepReward;
    }
};

} // namespace RLGC
