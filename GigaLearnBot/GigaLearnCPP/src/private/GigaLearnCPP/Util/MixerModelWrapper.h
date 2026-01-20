#pragma once
#include <torch/torch.h>
#include "MixerBlock.h"
#include "RLTokenizer.h"
#include "Models.h"
#include <GigaLearnCPP/Util/ModelConfig.h>
#include <filesystem>
#include <memory>

namespace GGL {

	// ============================================================================
	// MIXER MODEL WRAPPER - Adapter to make MLP-Mixer work with Model interface
	// ============================================================================
	// Wraps MLP-Mixer in a Model-compatible interface for PPO pipeline integration.
	// This is a container class (not inheriting from Model) that provides the
	// same public interface as Model for use with ModelSet.
	//
	// Handles:
	//   1. RLTokenizer - converts flat obs to structured tokens
	//   2. MixerNetwork - performs relational learning
	//   3. Model interface compatibility - Forward, Save, Load, Clone, etc.
	//
	// Rev 1.0 (2025-01-17, NJH) - Fixed version with proper config storage

	class MixerModelWrapper : public IModel {
	public:
		const char* modelName;
		torch::Device device;
		ModelConfig config;
		torch::optim::Optimizer* optim;

		// Core components
		MixerNetwork mixerNet;
		RLTokenizer tokenizer;
		MixerConfig mixerConfig;

		// Half-precision cache
		bool _mixerHalfOutdated = true;
		std::shared_ptr<MixerNetwork> mixerNetHalf;

		MixerModelWrapper() = delete;

		MixerModelWrapper(
			const char* modelName,
			int obsSize,
			int numPlayers,
			int numOutputs,
			const MixerConfig& mixerCfg,
			torch::Device device,
			ModelOptimType optimType = ModelOptimType::ADAMW
		);

		// Main forward pass: tokenize observation, pass through mixer network
		torch::Tensor Forward(torch::Tensor input, bool halfPrec = false) override;

		void SetOptimLR(float newLR) override;
		void StepOptim() override;

		std::filesystem::path GetSuffixedSavePath(std::filesystem::path folder, std::string suffix) const {
			std::string filename = std::string(modelName) + suffix;
			for (char& c : filename)
				c = toupper(c);
			filename += ".lt";
			return folder / filename;
		}

		std::filesystem::path GetSavePath(std::filesystem::path folder) const {
			return GetSuffixedSavePath(folder, "");
		}

		std::filesystem::path GetOptimSavePath(std::filesystem::path folder) const {
			return GetSuffixedSavePath(folder, "_optim");
		}

		void Save(std::filesystem::path folder, bool saveOptim = true) override;
		void Load(std::filesystem::path folder, bool allowNotExist, bool loadOptim = true) override;

		torch::Tensor CopyParams() const override;

		MixerModelWrapper* MakeEmptyClone() const {
			return new MixerModelWrapper(modelName, config.numInputs,
				tokenizer.config().numPlayers, config.numOutputs,
				mixerConfig, device, config.optimType);
		}

		MixerModelWrapper* MakeClone() const {
			torch::NoGradGuard no_grad;
			MixerModelWrapper* clone = MakeEmptyClone();

			// Copy mixer network parameters
			auto fromParams = mixerNet->parameters();
			auto toParams = clone->mixerNet->parameters();
			for (size_t i = 0; i < fromParams.size(); i++) {
				toParams[i].copy_(fromParams[i], true);
			}
			return clone;
		}

		uint64_t GetParamCount() const override {
			uint64_t total = 0;
			for (auto& param : mixerNet->parameters()) {
				total += param.numel();
			}
			return total;
		}

		const char* getModelName() const override {
			return modelName;
		}

		std::vector<torch::Tensor> parameters() const override {
			return mixerNet->parameters();
		}

		virtual ~MixerModelWrapper() = default;
	};

} // namespace GGL
