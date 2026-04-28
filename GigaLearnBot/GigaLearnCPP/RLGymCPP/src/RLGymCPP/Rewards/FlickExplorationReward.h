#pragma once
#include "Reward.h"
#include "../Math.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>

// FlickVarietyReward — Phase 6+
// State machine scoring how *unusual* a flick was:
//   - Ball position in car-local space at flip moment (side, behind, underside)
//   - Car orientation at flip (sideways roll, inverted)
//   - Carry timing (flash <0.15s or patient >1.5s)
// No bonus for standard upright front flicks — covered by CompositeFlickReward.
// Rev 1.1 2026-04-22 NJH

namespace RLGC {

class FlickVarietyReward : public Reward {
public:
    static constexpr float CARRY_BALL_MAX_DIST     = 130.f;
    static constexpr float CARRY_ZVEL_MATCH        = 120.f;
    static constexpr float CARRY_MIN_HEIGHT        = 80.f;
    static constexpr float SEPARATE_DIST_THRESHOLD = 185.f;
    static constexpr float CONFIRM_WINDOW          = 0.3f;

    static constexpr float SIDE_FLICK_RIGHT_MIN    = 60.f;
    static constexpr float FRONT_FLICK_FWD_MIN     = 50.f;
    static constexpr float FRONT_FLICK_RIGHT_MAX   = 60.f;
    static constexpr float BEHIND_FLICK_FWD_MAX    = -50.f;
    static constexpr float UNDER_FLICK_UP_MAX      = -30.f;

    static constexpr float FLASH_FLICK_MAX         = 0.15f;
    static constexpr float PATIENT_FLICK_MIN       = 1.5f;

    static constexpr float SIDEWAYS_UP_Z_MAX       = 0.35f;
    static constexpr float INVERTED_UP_Z_MAX       = -0.5f;
    static constexpr float UPRIGHT_UP_Z_MIN        = 0.7f;

    static constexpr float FLICK_COOLDOWN          = 3.0f;

private:
    enum class State { IDLE, CARRYING, FLICK_DETECTED };

    struct PlayerState {
        State  phase          = State::IDLE;
        float  carryStartTime = 0.f;
        float  flickTime      = 0.f;
        float  ballFwd        = 0.f;
        float  ballRight      = 0.f;
        float  ballUp         = 0.f;
        float  carUpZ         = 0.f;
        float  carryDuration  = 0.f;
        bool   wasFlipping    = false;
        float  cooldown       = 0.f;
    };

    std::unordered_map<uint32_t, PlayerState> _states;
    float _time = 0.f;

    float ScoreFlick(const PlayerState& s) const {
        float score = 0.5f;

        bool isFrontFlick = (s.ballFwd > FRONT_FLICK_FWD_MIN
                          && std::abs(s.ballRight) < FRONT_FLICK_RIGHT_MAX);

        if (!isFrontFlick) {
            if (std::abs(s.ballRight) > SIDE_FLICK_RIGHT_MIN) score += 0.4f;
            if (s.ballFwd < BEHIND_FLICK_FWD_MAX)             score += 0.6f;
            if (s.ballUp  < UNDER_FLICK_UP_MAX)               score += 0.5f;
        }

        if (s.carUpZ < INVERTED_UP_Z_MAX)
            score += 0.5f;
        else if (std::abs(s.carUpZ) < SIDEWAYS_UP_Z_MAX)
            score += 0.3f;

        if (s.carryDuration < FLASH_FLICK_MAX)
            score += 0.3f;
        else if (s.carryDuration > PATIENT_FLICK_MIN)
            score += 0.4f;

        return score;
    }

public:
    void Reset(const GameState& state) override {
        _states.clear();
        _time = 0.f;
        for (const auto& p : state.players)
            _states[p.carId] = PlayerState{};
    }

    void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.f) ? state.deltaTime : (1.f / 120.f);
        _time += dt;

        for (const auto& p : state.players) {
            auto& s = _states[p.carId];
            s.cooldown = std::max(0.f, s.cooldown - dt);

            bool flipStartedThisTick = (!s.wasFlipping && p.isFlipping);
            s.wasFlipping = p.isFlipping;

            Vec   diff        = state.ball.pos - p.pos;
            float dist        = diff.Length();
            float ballZVelDiff = std::abs(state.ball.vel.z - p.vel.z);
            float ballZ       = state.ball.pos.z;

            switch (s.phase) {
            case State::IDLE:
                if (dist < CARRY_BALL_MAX_DIST
                    && ballZVelDiff < CARRY_ZVEL_MATCH
                    && ballZ > CARRY_MIN_HEIGHT)
                {
                    s.phase          = State::CARRYING;
                    s.carryStartTime = _time;
                }
                break;

            case State::CARRYING:
                if (dist > CARRY_BALL_MAX_DIST + 30.f || ballZ < CARRY_MIN_HEIGHT - 30.f) {
                    s.phase = State::IDLE;
                    break;
                }
                if (flipStartedThisTick) {
                    s.ballFwd       = diff.Dot(p.rotMat.forward);
                    s.ballRight     = diff.Dot(p.rotMat.right);
                    s.ballUp        = diff.Dot(p.rotMat.up);
                    s.carUpZ        = p.rotMat.up.z;
                    s.carryDuration = _time - s.carryStartTime;
                    s.flickTime     = _time;
                    s.phase         = State::FLICK_DETECTED;
                }
                break;

            case State::FLICK_DETECTED:
                if ((_time - s.flickTime) > CONFIRM_WINDOW)
                    s.phase = State::IDLE;
                break;
            }
        }
    }

    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        auto& s = _states[player.carId];

        if (s.phase != State::FLICK_DETECTED) return 0.f;
        if (s.cooldown > 0.f)                return 0.f;

        Vec   diff = state.ball.pos - player.pos;
        float dist = diff.Length();

        if (dist < SEPARATE_DIST_THRESHOLD) return 0.f;

        bool isFrontFlick   = (s.ballFwd > FRONT_FLICK_FWD_MIN
                             && std::abs(s.ballRight) < FRONT_FLICK_RIGHT_MAX);
        bool isUprightFlick = (s.carUpZ > UPRIGHT_UP_Z_MIN);
        bool isStandardTime = (s.carryDuration >= FLASH_FLICK_MAX
                             && s.carryDuration <= PATIENT_FLICK_MIN);

        if (isFrontFlick && isUprightFlick && isStandardTime) {
            s.phase = State::IDLE;
            return 0.f;
        }

        float score = ScoreFlick(s);
        s.phase    = State::IDLE;
        s.cooldown = FLICK_COOLDOWN;

        return score;
    }
};

} // namespace RLGC
