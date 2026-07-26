#pragma once
#include "Reward.h"
#include "../Gamestates/GameState.h"
#include "../CommonValues.h"
#include <atomic>

namespace RLGC {

// ============================================================================
// CounterAttackReward
// ============================================================================
// Rewards driving the ball hard toward the opponent's goal during the window
// right after a turnover, but ONLY if the turnover caught opponents out of
// position (i.e. more opponents were committed forward of the ball than were
// covering their own goal at the moment possession changed).
//
// This is the "fast break" signal: winning the ball back when the other team
// has nobody home. It's a conditional bonus layered on top of ordinary ball-
// to-goal progress — it does NOT replace EnhancedVelocityBallToGoalReward,
// which should keep rewarding forward ball progress unconditionally. This
// reward only adds extra credit during genuine break windows.
//
// MECHANIC:
//   1. PreStep watches for the first team-touch-flip each step (team A
//      touches, then team B touches) — that's a turnover for team B.
//   2. At that instant, count team A's live players who are NOT goal-side of
//      the ball (same concept as BallSideNumericalAdvantageReward::IsBehindBall).
//      advantage = uncovered / total live opponents, in [0, 1].
//   3. If advantage > 0, open a windowDuration-second window for team B.
//   4. During the window, every team B player is paid progress * advantage,
//      where progress = ball's velocity component toward team B's target
//      goal, normalized by BALL_MAX_SPEED, gated on a minimum ball speed so
//      a slow dribble can't farm the window.
//
// ANTI-FARMING:
//   - Window only opens on a genuine cross-team touch flip (not re-touches by
//     the same team), so juggling the ball to yourself can't retrigger it.
//   - Window only opens if the advantage gate is nonzero (opponents actually
//     caught out), so normal back-and-forth midfield play never fires this.
//   - advantage is snapshotted at turnover time, not re-evaluated every
//     frame, so recovering defenders mid-window don't unfairly cut the
//     reward off, but they also can't be gamed into re-opening it.
//   - Per-frame payout requires real ball speed toward goal, so parking on
//     the ball during the window pays nothing.
//
// OVERLAP: Related to BallSideNumericalAdvantageReward (same goal-side
// concept) but that reward is continuous/positional and fires every step
// regardless of a turnover. This reward is event-gated on the turnover
// itself and only pays for actually capitalizing on the break, not for
// standing shape.
//
// SUGGESTED ROLLOUT: shadowMode=true on Machine 2 first. Watch WindowsOpened
// (does the situation even arise at the current phase?) and
// WindowsCapitalized / WindowsOpened (is the bot converting the breaks it
// gets, or getting them and doing nothing?). If capitalization stays near
// zero after a few hundred million steps while windows open at a healthy
// rate, the bot sees the break but isn't acting on it — check RocketSimVis
// for hesitation/rotating-away instead of pushing forward.
//
// Suggested weight: start low (5-10f), shadow mode, Phase 5+ — this is a
// conditional bonus on top of an already-active primary signal, not a
// replacement for one.
//
// 2026-07-06 | Rev 1.0 | NJH + Claude
// ============================================================================
class CounterAttackReward : public Reward {
public:
    struct Config {
        float windowDuration = 4.0f;   // seconds the break window stays open after a qualifying turnover
        float minBallSpeed   = 500.0f; // UU/s toward goal required to pay out (blocks slow-crawl farming)
        bool  shadowMode     = false;
    };

    struct Metrics {
        int    windowsOpened      = 0; // qualifying turnovers (advantage > 0) this interval
        int    windowsCapitalized = 0; // of those, how many paid out at least once
        double avgAdvantage       = 0.0; // mean uncovered-opponent fraction at window open
        double rewardSum          = 0.0; // sum of raw (pre-weight) reward paid
    };

private:
    Config config;

    struct TeamWindow {
        float timeRemaining = 0.0f;
        float advantage     = 0.0f;
        bool  capitalized   = false;
    };
    mutable TeamWindow windows[2]; // 0 = BLUE, 1 = ORANGE

    mutable bool anyTouchYet   = false;
    mutable Team lastTouchTeam = Team::BLUE;

    inline static std::atomic<int>       s_windowsOpened      { 0 };
    inline static std::atomic<int>       s_windowsCapitalized { 0 };
    inline static std::atomic<long long> s_advantageSum       { 0 }; // ×1000
    inline static std::atomic<long long> s_rewardMilli        { 0 };

    static int TeamIdx(Team t) { return (t == Team::BLUE) ? 0 : 1; }

    // Same concept as BallSideNumericalAdvantageReward::IsBehindBall.
    static bool IsBehindBall(const Player& p, const GameState& state) {
        return (p.team == Team::BLUE)
            ? (p.pos.y < state.ball.pos.y)
            : (p.pos.y > state.ball.pos.y);
    }

    // Fraction of scoringTeam's live opponents who are NOT goal-side of the
    // ball right now — i.e. caught upfield with nobody home.
    static float ComputeAdvantage(const GameState& state, Team scoringTeam) {
        int total = 0, uncovered = 0;
        for (const auto& p : state.players) {
            if (p.team == scoringTeam) continue;
            if (p.isDemoed) continue;
            total++;
            if (!IsBehindBall(p, state)) uncovered++;
        }
        return (total > 0) ? float(uncovered) / float(total) : 0.0f;
    }

public:
    CounterAttackReward() : config() {}
    explicit CounterAttackReward(const Config& cfg) : config(cfg) {}

    static Metrics GetAndResetMetrics() {
        Metrics m;
        m.windowsOpened      = s_windowsOpened.exchange(0);
        m.windowsCapitalized = s_windowsCapitalized.exchange(0);
        long long advRaw = s_advantageSum.exchange(0);
        long long rewRaw = s_rewardMilli.exchange(0);
        m.rewardSum = double(rewRaw) / 1000.0;
        if (m.windowsOpened > 0)
            m.avgAdvantage = double(advRaw) / (1000.0 * m.windowsOpened);
        return m;
    }

    virtual void Reset(const GameState& initialState) override {
        windows[0] = TeamWindow{};
        windows[1] = TeamWindow{};
        anyTouchYet = false;
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        for (int i = 0; i < 2; i++) {
            if (windows[i].timeRemaining > 0.0f) {
                windows[i].timeRemaining -= dt;
                if (windows[i].timeRemaining <= 0.0f) {
                    if (windows[i].capitalized) s_windowsCapitalized.fetch_add(1);
                    windows[i] = TeamWindow{};
                }
            }
        }

        Team touchingTeam = Team::BLUE;
        bool touched = false;
        for (const auto& p : state.players) {
            if (p.ballTouchedStep) { touchingTeam = p.team; touched = true; break; }
        }
        if (!touched) return;

        if (anyTouchYet && touchingTeam != lastTouchTeam) {
            float advantage = ComputeAdvantage(state, touchingTeam);
            if (advantage > 0.0f) {
                TeamWindow& w = windows[TeamIdx(touchingTeam)];
                w.timeRemaining = config.windowDuration;
                w.advantage     = advantage;
                w.capitalized   = false;
                s_windowsOpened.fetch_add(1);
                s_advantageSum.fetch_add(static_cast<long long>(advantage * 1000.0f));
            }
        }
        anyTouchYet   = true;
        lastTouchTeam = touchingTeam;
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (player.isDemoed) return 0.0f;

        TeamWindow& w = windows[TeamIdx(player.team)];
        if (w.timeRemaining <= 0.0f) return 0.0f;

        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_BACK
            : CommonValues::BLUE_GOAL_BACK;

        Vec toGoal = (oppGoal - state.ball.pos).Normalized();
        float ballSpeedToGoal = state.ball.vel.Dot(toGoal);
        if (ballSpeedToGoal < config.minBallSpeed) return 0.0f;

        float progress = RS_MIN(ballSpeedToGoal / CommonValues::BALL_MAX_SPEED, 1.0f);
        float reward   = progress * w.advantage;

        w.capitalized = true;
        s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

        return config.shadowMode ? 0.0f : reward;
    }
};

} // namespace RLGC
