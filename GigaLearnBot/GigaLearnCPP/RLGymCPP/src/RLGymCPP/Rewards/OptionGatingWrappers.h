#pragma once
#include "Reward.h"
#include "OptionControlReward.h"

namespace RLGC {

	// ========================================================================
	// SCORING REWARD GATING WRAPPER
	// ========================================================================
	// Wraps any scoring-type reward and applies option-based gating multiplier
	// Formula: reward = baseReward * scoringMultiplier(availableScoringOptions)
	//
	// This automatically reduces rewards when few scoring options are available
	// and increases them (bonus) when many options exist.
	// ========================================================================

	class ScoringRewardWrapper : public RewardWrapper {
	private:
		OptionControlReward* optionControl;
		bool ownsOptionControl;

	public:
		ScoringRewardWrapper(Reward* scoringReward, OptionControlReward* optCtrl = nullptr,
		                      bool ownsControl = false)
		    : RewardWrapper(scoringReward), optionControl(optCtrl), ownsOptionControl(ownsControl) {
			if (!optionControl) {
				// Create our own instance if not provided
				optionControl = new OptionControlReward();
				ownsOptionControl = true;
			}
		}

		~ScoringRewardWrapper() override {
			if (ownsOptionControl) {
				delete optionControl;
			}
		}

		void Reset(const GameState& initialState) override {
			RewardWrapper::Reset(initialState);
			optionControl->Reset(initialState);
		}

		void PreStep(const GameState& state) override {
			RewardWrapper::PreStep(state);
			optionControl->PreStep(state);
		}

		float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Get base reward from wrapped reward
			float baseReward = child->GetReward(player, state, isFinal);

			// Get multiplier based on available scoring options
			int scoringOptions = optionControl->CountScoringOptions(player, state);
			float multiplier = optionControl->GetScoringMultiplier(scoringOptions);

			// Apply gating
			return baseReward * multiplier;
		}
	};

	// ========================================================================
	// PASSING REWARD GATING WRAPPER
	// ========================================================================
	// Wraps any passing-type reward and applies option-based gating multiplier
	// Formula: reward = baseReward * passingMultiplier(availablePassingOptions)
	//
	// This automatically reduces rewards when few passing options are available
	// and increases them (bonus) when many options exist.
	// ========================================================================

	class PassingRewardWrapper : public RewardWrapper {
	private:
		OptionControlReward* optionControl;
		bool ownsOptionControl;

	public:
		PassingRewardWrapper(Reward* passingReward, OptionControlReward* optCtrl = nullptr,
		                      bool ownsControl = false)
		    : RewardWrapper(passingReward), optionControl(optCtrl), ownsOptionControl(ownsControl) {
			if (!optionControl) {
				// Create our own instance if not provided
				optionControl = new OptionControlReward();
				ownsOptionControl = true;
			}
		}

		~PassingRewardWrapper() override {
			if (ownsOptionControl) {
				delete optionControl;
			}
		}

		void Reset(const GameState& initialState) override {
			RewardWrapper::Reset(initialState);
			optionControl->Reset(initialState);
		}

		void PreStep(const GameState& state) override {
			RewardWrapper::PreStep(state);
			optionControl->PreStep(state);
		}

		float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Get base reward from wrapped reward
			float baseReward = child->GetReward(player, state, isFinal);

			// Get multiplier based on available passing options
			float passingOptions = optionControl->CountPassOptions(player, state);
			float multiplier = optionControl->GetPassingMultiplier(passingOptions);

			// Apply gating
			return baseReward * multiplier;
		}
	};

	// ========================================================================
	// FLEXIBLE OPTION GATING WRAPPER
	// ========================================================================
	// Generic wrapper that can gate any reward with either scoring or passing options
	// Useful for rewards that don't fit cleanly into "scoring" or "passing" categories
	// ========================================================================

	enum class OptionGateType {
		SCORING,   // Gate with scoring option count
		PASSING,   // Gate with passing option count
		COMBINED   // Gate with average of both
	};

	class OptionGatingWrapper : public RewardWrapper {
	private:
		OptionControlReward* optionControl;
		bool ownsOptionControl;
		OptionGateType gateType;

	public:
		OptionGatingWrapper(Reward* baseReward, OptionGateType type,
		                     OptionControlReward* optCtrl = nullptr, bool ownsControl = false)
		    : RewardWrapper(baseReward), optionControl(optCtrl), ownsOptionControl(ownsControl),
		      gateType(type) {
			if (!optionControl) {
				optionControl = new OptionControlReward();
				ownsOptionControl = true;
			}
		}

		~OptionGatingWrapper() override {
			if (ownsOptionControl) {
				delete optionControl;
			}
		}

		void Reset(const GameState& initialState) override {
			RewardWrapper::Reset(initialState);
			optionControl->Reset(initialState);
		}

		void PreStep(const GameState& state) override {
			RewardWrapper::PreStep(state);
			optionControl->PreStep(state);
		}

		float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Get base reward from wrapped reward
			float baseReward = child->GetReward(player, state, isFinal);

			// Get multiplier based on gate type
			float multiplier = 1.0f;

			if (gateType == OptionGateType::SCORING) {
				int scoringOptions = optionControl->CountScoringOptions(player, state);
				multiplier = optionControl->GetScoringMultiplier(scoringOptions);
			} else if (gateType == OptionGateType::PASSING) {
				float passingOptions = optionControl->CountPassOptions(player, state);
				multiplier = optionControl->GetPassingMultiplier(passingOptions);
			} else if (gateType == OptionGateType::COMBINED) {
				int scoringOptions = optionControl->CountScoringOptions(player, state);
				float passingOptions = optionControl->CountPassOptions(player, state);
				float scoringMult = optionControl->GetScoringMultiplier(scoringOptions);
				float passingMult = optionControl->GetPassingMultiplier(passingOptions);
				multiplier = (scoringMult + passingMult) / 2.0f;
			}

			// Apply gating
			return baseReward * multiplier;
		}
	};

} // namespace RLGC
