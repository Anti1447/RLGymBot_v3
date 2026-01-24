#pragma once

#include "ModelConfig.h"
#include <torch/torch.h>

namespace GGL {
	struct RG_IMEXPORT InferUnit {
		int obsSize;
		RLGC::ObsBuilder* obsBuilder;
		RLGC::ActionParser* actionParser;
		struct ModelSet* models;
		bool useGPU;

		// Preallocated buffers for batch inference (avoid reallocation overhead)
		// These provide 1.3-2x speedup for batch operations
		torch::Tensor obsBuffer;          // GPU buffers (or CPU if !useGPU)
		torch::Tensor actionMaskBuffer;
		torch::Tensor cpuObsBuffer;       // CPU staging buffers (pinned memory for async GPU transfers)
		torch::Tensor cpuActionMaskBuffer;
		int maxBatchSize = 256;

		// NOTE: Reset() will never be called on your obs 
		InferUnit(
			RLGC::ObsBuilder* obsBuilder, int obsSize, RLGC::ActionParser* actionParser,
			PartialModelConfig sharedHeadConfig, PartialModelConfig policyConfig,
			std::filesystem::path modelsFolder, bool useGPU);


		RLGC::Action InferAction(const RLGC::Player& player, const RLGC::GameState& state, bool deterministic, float temperature = 1);
		std::vector<RLGC::Action> BatchInferActions(const std::vector<RLGC::Player>& players, const std::vector<RLGC::GameState>& states, bool deterministic, float temperature = 1);

		// TODO: Add deconstructor (make sure to free models too)
	};
}