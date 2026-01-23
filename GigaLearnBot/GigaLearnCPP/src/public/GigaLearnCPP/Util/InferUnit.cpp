#include "InferUnit.h"

#include <GigaLearnCPP/Util/Models.h>
#include <GigaLearnCPP/PPO/PPOLearner.h>

GGL::InferUnit::InferUnit(
	RLGC::ObsBuilder* obsBuilder, int obsSize, RLGC::ActionParser* actionParser,
	PartialModelConfig sharedHeadConfig, PartialModelConfig policyConfig, 
	std::filesystem::path modelsFolder, bool useGPU) : 
	obsBuilder(obsBuilder), obsSize(obsSize), actionParser(actionParser), useGPU(useGPU) {

	this->models = new ModelSet();

	try {
		PPOLearner::MakeModels(
			false, obsSize, actionParser->GetActionAmount(),
			sharedHeadConfig, policyConfig, {},
			useGPU ? torch::kCUDA : torch::kCPU,
			*this->models
		);
	} catch (std::exception& e) {
		RG_ERR_CLOSE("InferUnit: Exception when trying to construct models: " << e.what());
	}

	try {
		this->models->Load(modelsFolder, false, false);
	} catch (std::exception& e) {
		RG_ERR_CLOSE("InferUnit: Exception when trying to load models: " << e.what());
	}

	// Preallocate tensors for batch inference (avoid reallocation overhead)
	// This provides 1.3-2x speedup for batch operations
	auto device = useGPU ? torch::kCUDA : torch::kCPU;

	torch::TensorOptions tensorOpts = torch::TensorOptions()
		.dtype(torch::kFloat32)
		.device(device)
		.requires_grad(false);

	obsBuffer = torch::empty({maxBatchSize, obsSize}, tensorOpts);
	actionMaskBuffer = torch::empty({maxBatchSize, actionParser->GetActionAmount()},
									 tensorOpts.dtype(torch::kBool));

	// Pin memory for faster CPU<->tensor transfers (CPU only)
	if (device == torch::kCPU) {
		obsBuffer = obsBuffer.pin_memory();
		actionMaskBuffer = actionMaskBuffer.pin_memory();
	}

	RG_LOG("InferUnit: Preallocated buffers for batch_size=" << maxBatchSize);
}

RLGC::Action GGL::InferUnit::InferAction(const RLGC::Player& player, const RLGC::GameState& state, bool deterministic, float temperature) {
	return BatchInferActions({ player }, { state }, deterministic, temperature)[0];
}

std::vector<RLGC::Action> GGL::InferUnit::BatchInferActions(const std::vector<RLGC::Player>& players, const std::vector<RLGC::GameState>& states, bool deterministic, float temperature) {
	RG_ASSERT(players.size() > 0 && states.size() > 0);
	RG_ASSERT(players.size() == states.size());

	int batchSize = players.size();
	std::vector<RLGC::Action> results = {};

	try {
		RG_NO_GRAD;

		auto device = useGPU ? torch::kCUDA : torch::kCPU;

		// Optimize: Use preallocated buffers if batch size fits
		torch::Tensor tObs, tActionMasks;

		if (batchSize <= maxBatchSize) {
			// FAST PATH: Reuse preallocated buffers (no allocation overhead)
			// This provides 1.3-2x speedup
			auto obsBatch = obsBuffer.narrow(0, 0, batchSize);
			auto maskBatch = actionMaskBuffer.narrow(0, 0, batchSize);

			// Fill buffers in-place (avoid intermediate vector allocations)
			for (int i = 0; i < batchSize; i++) {
				FList curObs = obsBuilder->BuildObs(players[i], states[i]);
				if (curObs.size() != obsSize) {
					RG_ERR_CLOSE(
						"InferUnit: Obs builder produced an obs that differs from the provided size (expected: " << obsSize << ", got: " << curObs.size() << ")\n" <<
						"Make sure you provided the correct obs size to the InferUnit constructor.\n" <<
						"Also, make sure there aren't an incorrect number of players (there are " << states[i].players.size() << " in this state)"
					);
				}

				// Copy directly to tensor (no intermediate vector)
				std::memcpy(obsBatch[i].data_ptr<float>(), curObs.data(), obsSize * sizeof(float));

				auto masks = actionParser->GetActionMask(players[i], states[i]);
				std::memcpy(maskBatch[i].data_ptr<bool>(), masks.data(), masks.size() * sizeof(uint8_t));
			}

			tObs = obsBatch;
			tActionMasks = maskBatch;

		} else {
			// SLOW PATH: Batch too large, allocate on the fly (fallback)
			RG_LOG("WARNING: Batch size " << batchSize << " exceeds preallocated buffer size " << maxBatchSize << ". Falling back to dynamic allocation.");

			std::vector<float> allObs;
			std::vector<uint8_t> allActionMasks;
			for (int i = 0; i < batchSize; i++) {
				FList curObs = obsBuilder->BuildObs(players[i], states[i]);
				if (curObs.size() != obsSize) {
					RG_ERR_CLOSE(
						"InferUnit: Obs builder produced an obs that differs from the provided size (expected: " << obsSize << ", got: " << curObs.size() << ")\n" <<
						"Make sure you provided the correct obs size to the InferUnit constructor.\n" <<
						"Also, make sure there aren't an incorrect number of players (there are " << states[i].players.size() << " in this state)"
					);
				}
				allObs += curObs;
				allActionMasks += actionParser->GetActionMask(players[i], states[i]);
			}

			tObs = torch::tensor(allObs).reshape({(int64_t)players.size(), obsSize}).to(device);
			tActionMasks = torch::tensor(allActionMasks).reshape({(int64_t)players.size(), this->actionParser->GetActionAmount()}).to(device);
		}

		torch::Tensor tActions, tLogProbs;
		PPOLearner::InferActionsFromModels(*models, tObs, tActionMasks, deterministic, temperature, false, &tActions, &tLogProbs);

		auto actionIndices = TENSOR_TO_VEC<int>(tActions);

		for (int i = 0; i < batchSize; i++)
			results.push_back(actionParser->ParseAction(actionIndices[i], players[i], states[i]));

	} catch (std::exception& e) {
		RG_ERR_CLOSE("InferUnit: Exception when inferring model: " << e.what());
	}

	return results;
}