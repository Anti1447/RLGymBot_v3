#pragma once
#include "../Framework.h"
#include "Reward.h"
#include "../CommonValues.h"
#include <map>

namespace RLGC {

// ============================================================================
// DemoAdvantageReward
// ============================================================================
// Rewards demoing/bumping opponents that the bot holds a clear ADVANTAGE over:
// faster (more momentum) AND carrying more boost than the victim. This is the
// high-value demo — taking out a slow, boost-starved opponent with little risk,
// as opposed to trading a coin-flip 50/50 with a fast, full-boost opponent.
//
// The reward is a pure DIFFERENTIAL bonus: value scales with how much faster and
// higher-boost the attacker is than the victim, and is ~0 when there is no
// advantage. This deliberately does NOT duplicate the flat, always-on demo
// rewards (DemoReward / RolvDemoReward / BumpReward) or the absolute-threshold
// StrategicDemoReward (opponent boost < 25). No existing reward keys on the
// attacker-vs-victim differential.
//
// VICTIM STATE IS READ PRE-CONTACT (target->prev): a bump launches the victim,
// so post-contact velocity is misleading. "Was the opponent slow?" must be
// answered from the frame before the hit.
//
// ANTI-FARMING:
//   - Value is 0 unless the attacker is genuinely faster than the victim.
//   - Min attacker speed gate so idle nudges on a parked opponent don't pay.
//   - Per-opponent cooldown keyed by victim carId — no repeat-bump farming
//     during a respawn cycle. (Mirrors PossessionSideDemoReward.)
//   - Bumps pay a fraction of demos.
//
// Event-based; intended to be ZeroSum-wrapped at integration (a demo is a direct
// opponent loss). No shadow wrapper.
//
// 2026-07-06 | Rev 1.0 | NJH + Claude
// ============================================================================
class DemoAdvantageReward : public Reward {
public:
	struct Config {
		float speedNormUU     = 1500.0f; // Speed advantage that maps to full speed term
		float minAttackerSpeed = 500.0f; // Below this, attacker isn't really committing
		float wSpeed          = 0.6f;    // Weight of the speed-advantage term
		float wBoost          = 0.4f;    // Weight of the boost-advantage term
		float bumpMultiplier  = 0.55f;   // Bumps pay this fraction of a demo
		float contactRadius   = 300.0f;  // Victim must be within this of attacker on event frame
		float perOpponentCooldown = 4.5f; // Seconds before same victim can reward again
	};

private:
	Config config;

	struct PlayerMem {
		int prevDemoCount = 0;
		int prevBumpCount = 0;
		std::map<uint32_t, float> opponentCooldowns; // victimCarId -> seconds remaining
	};

	mutable std::map<uint32_t, PlayerMem> mem;
	mutable float lastDt = 0.016f;

	// Resolve which opponent was contacted this frame. Prefer a demoed opponent
	// (strongest signal), fall back to a bumped one, both within contact radius.
	const Player* FindContactedOpponent(const Player& player, const GameState& state,
										bool& outWasDemo) const {
		outWasDemo = false;
		const Player* hit = nullptr;
		float closestDist = 1e9f;

		for (const auto& opp : state.players) {
			if (opp.team == player.team) continue;
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

public:
	DemoAdvantageReward() = default;
	explicit DemoAdvantageReward(const Config& cfg) : config(cfg) {}

	virtual void Reset(const GameState& initialState) override {
		mem.clear();
		lastDt = 0.016f;
	}

	virtual void PreStep(const GameState& state) override {
		lastDt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
	}

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (player.isDemoed) return 0.0f;

		auto& m = mem[player.carId];

		// ---- Cooldown decay (every step, regardless of contact) ----
		for (auto it = m.opponentCooldowns.begin(); it != m.opponentCooldowns.end(); ) {
			it->second -= lastDt;
			if (it->second <= 0.0f) it = m.opponentCooldowns.erase(it);
			else ++it;
		}

		// ---- Event edge detection: demo/bump counter increased this frame? ----
		int curDemo = player.eventState.demo;
		int curBump = player.eventState.bump;
		bool demoEvent = (curDemo > m.prevDemoCount);
		bool bumpEvent = (curBump > m.prevBumpCount);
		m.prevDemoCount = curDemo;
		m.prevBumpCount = curBump;

		if (!demoEvent && !bumpEvent) return 0.0f;

		// ---- Min-speed gate: idle nudges don't count ----
		float attackerSpeed = player.vel.Length();
		if (attackerSpeed < config.minAttackerSpeed) return 0.0f;

		// ---- Resolve contacted opponent ----
		bool wasDemo = false;
		const Player* target = FindContactedOpponent(player, state, wasDemo);
		if (!target) return 0.0f;

		// ---- Per-opponent cooldown ----
		auto cd = m.opponentCooldowns.find(target->carId);
		if (cd != m.opponentCooldowns.end() && cd->second > 0.0f) return 0.0f;

		// ---- Advantage differential (victim read PRE-contact) ----
		const Player* vp = target->prev ? target->prev : target;
		float speedAdv = attackerSpeed - vp->vel.Length();
		float boostAdv = player.boost - vp->boost; // boost is 0..100
		if (speedAdv <= 0.0f) return 0.0f;          // must hold momentum

		float speedNorm = RS_CLAMP(speedAdv / config.speedNormUU, 0.0f, 1.0f);
		float boostNorm = RS_CLAMP(boostAdv / 100.0f, 0.0f, 1.0f);

		float value = config.wSpeed * speedNorm + config.wBoost * boostNorm;
		if (!wasDemo) value *= config.bumpMultiplier;

		if (value <= 0.0f) return 0.0f;

		// ---- Arm cooldown, return ----
		m.opponentCooldowns[target->carId] = config.perOpponentCooldown;
		return value;
	}
};

} // namespace RLGC
