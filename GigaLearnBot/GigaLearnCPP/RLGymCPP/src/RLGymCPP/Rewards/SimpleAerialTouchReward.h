#pragma once
// ============================================================================
// SimpleAerialTouchReward.h
//
// Rev 1.0 - 2026-08-28 (NJH)
//
// PURPOSE:
//   A discrete, immediate-payout sibling of AerialFirstTouchReward
//   (AdvancedRewards.h) — wired alongside it (not in place of it) in
//   PHASE_4_PLATINUM and PHASE_5_DIAMOND so W&B can compare the two before
//   any decision is made to replace or retire either one.
//
// WHY THIS EXISTS (weaknesses of AerialFirstTouchReward this addresses):
//   1. AerialFirstTouchReward defers payout to the END of the "aerial
//      session" (ball drops back to/below aerialZ, or a 5s hard cap) rather
//      than the step of the actual touch — unusual credit-assignment lag for
//      an RL signal. This class pays on the exact step of the touch.
//   2. AerialFirstTouchReward tracks eligibility via a full
//      unordered_map<carId,bool> snapshotted over every player in
//      state.players at session start, plus a second unordered_map<carId,bool>
//      ("paidThisAerial") marked true for EVERY eligible player the first
//      time GetReward() evaluates them (not just the scorer) to enforce
//      pay-once. This class replaces both maps with one Vec (ball position
//      at rearm, for the anti-farm distance check) + one per-player cooldown
//      float (replaces pay-once, since payout is immediate there's nothing
//      left to defer-and-flag).
//   3. Because AerialFirstTouchReward's payout is deferred, a ball dipping
//      back under aerialZ and popping back up loses the prior session's
//      eligibility snapshot entirely — a juggling sequence straddling the
//      threshold can produce several short, choppy sessions with lost state.
//      This class has no pending/deferred state to lose: every touch is
//      self-contained and pays immediately, so it's structurally immune to
//      that bug class.
//   4. Anti-farm gate (don't reward a player who raced in from across the
//      map after the aerial already started) is preserved with the same
//      3500uu default threshold as the original, checked against a single
//      snapshotted Vec instead of an iterate-and-map over all players.
//
//   Reward SHAPE (height/quickness blend, min/max bounds) is DELIBERATELY
//   unchanged from AerialFirstTouchReward — this is meant to be an
//   architecture comparison (simpler state machine, same payout curve), not
//   a behavior comparison. See Reward_Change_Log.md 2026-08-28 entry.
//
// W&B: SimpleAerialTouch/FireCount, SimpleAerialTouch/AvgReward
// ============================================================================

#include "Reward.h"
#include "../Math.h" // RS_CLAMP, RS_MAX
#include <unordered_map>

namespace RLGC {

class SimpleAerialTouchReward : public Reward {
public:
    struct Config {
        float aerialZ          = 400.0f;  // ball height (uu) to count as "airborne play" / rearm threshold
        float heightCap        = 2000.0f; // ball height (uu) at which the height component saturates to 1.0
        float timeScoreWindow  = 1.8f;    // seconds after rearm for full quickness credit
        float minReward        = 0.6f;
        float maxReward        = 2.2f;
        float touchCooldownSec = 1.5f;    // per-player anti-spam cooldown after claiming a touch
        float maxLaunchDist    = 3500.0f; // toucher must be within this of the ball's position at rearm
    };

    Config config;

    SimpleAerialTouchReward() : config() {}
    explicit SimpleAerialTouchReward(const Config& cfg) : config(cfg) {}

private:
    bool  _touchAvailable = true;
    float _timeSinceRearm = 0.0f;
    Vec   _ballPosAtRearm = Vec{ 0, 0, 0 };
    std::unordered_map<uint32_t, float> _cooldownRemaining;

    inline static std::atomic<int>   s_fireCount{ 0 };
    inline static std::atomic<float> s_rewardSum{ 0.0f };

public:
    struct Metrics { int fireCount = 0; float avgReward = 0.0f; };
    static Metrics GetAndResetMetrics() {
        int   count = s_fireCount.exchange(0);
        float sum   = s_rewardSum.exchange(0.0f);
        return { count, count > 0 ? sum / count : 0.0f };
    }

    virtual void Reset(const GameState& /*initialState*/) override {
        _touchAvailable = true;
        _timeSinceRearm = 0.0f;
        _ballPosAtRearm = Vec{ 0, 0, 0 };
        _cooldownRemaining.clear();
    }

    virtual void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        for (auto& pair : _cooldownRemaining)
            pair.second = RS_MAX(0.0f, pair.second - dt);

        bool isKickoff = state.ball.pos.Length() < 200.0f && state.ball.vel.Length() < 150.0f;

        if (!isKickoff && state.ball.pos.z <= config.aerialZ) {
            _touchAvailable = true;
            _ballPosAtRearm = state.ball.pos;
            _timeSinceRearm = 0.0f;
        } else if (_touchAvailable) {
            _timeSinceRearm += dt;
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool /*isFinal*/) override {
        if (!_touchAvailable) return 0.0f;
        if (state.ball.pos.z <= config.aerialZ) return 0.0f;
        if (!player.ballTouchedStep || player.isOnGround || !player.hasJumped) return 0.0f;

        float& cd = _cooldownRemaining[player.carId]; // lazy-init to 0.0f
        if (cd > 0.0f) return 0.0f;

        if ((player.pos - _ballPosAtRearm).Length() > config.maxLaunchDist) return 0.0f;

        _touchAvailable = false; // consumes the credit for this aerial, immediately
        cd = config.touchCooldownSec;

        float hFrac = RS_CLAMP((state.ball.pos.z - config.aerialZ) / (config.heightCap - config.aerialZ), 0.0f, 1.0f);
        float tFrac = RS_CLAMP(1.0f - (_timeSinceRearm / config.timeScoreWindow), 0.0f, 1.0f);
        float frac  = 0.55f * hFrac + 0.45f * tFrac;
        float reward = config.minReward + (config.maxReward - config.minReward) * frac;

        s_fireCount.fetch_add(1);
        float prev = s_rewardSum.load();
        while (!s_rewardSum.compare_exchange_weak(prev, prev + reward)) {}

        return reward;
    }
};

} // namespace RLGC
