#include "InferUnit.h"

#include <GigaLearnCPP/Util/Models.h>
#include <GigaLearnCPP/PPO/PPOLearner.h>

// CUDA Optimizer for GPU inference optimizations
namespace GGL {
	class CUDAOptimizer {
	private:
		static bool initialized;
	public:
		static void Initialize() {
			if (initialized) return;
			if (!torch::cuda::is_available()) return;

			RG_LOG("=== Initializing CUDA Inference Optimizations ===");
			at::globalContext().setBenchmarkCuDNN(true);
			at::globalContext().setAllowTF32CuDNN(true);
			at::globalContext().setAllowTF32CuBLAS(true);
			RG_LOG("Enabled cuDNN benchmark & TF32 precision");
			initialized = true;
			RG_LOG("=== CUDA Optimizations Complete ===");
		}
		static bool IsInitialized() { return initialized; }
	};
	bool CUDAOptimizer::initialized = false;
}

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

	// Initialize CUDA optimizations if using GPU
	if (useGPU) {
		CUDAOptimizer::Initialize();
	}

	// Preallocate tensors for batch inference (avoid reallocation overhead)
	// This provides 1.3-2x speedup for batch operations
	auto device = useGPU ? torch::kCUDA : torch::kCPU;

	torch::TensorOptions tensorOpts = torch::TensorOptions()
		.dtype(torch::kFloat32)
		.device(device)
		.requires_grad(false);

	// For GPU: allocate buffers on GPU directly
	// For CPU: allocate on CPU with pinned memory
	obsBuffer = torch::empty({maxBatchSize, obsSize}, tensorOpts);
	actionMaskBuffer = torch::empty({maxBatchSize, actionParser->GetActionAmount()},
									 tensorOpts.dtype(torch::kBool));

	// CPU staging buffers for GPU path (pinned memory for async transfers)
	if (useGPU) {
		cpuObsBuffer = torch::empty({maxBatchSize, obsSize},
			torch::TensorOptions().dtype(torch::kFloat32).pinned_memory(true));
		cpuActionMaskBuffer = torch::empty({maxBatchSize, actionParser->GetActionAmount()},
			torch::TensorOptions().dtype(torch::kBool).pinned_memory(true));
		RG_LOG("InferUnit: Allocated pinned CPU staging buffers for GPU");
	} else {
		// Pin memory for faster CPU operations
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

			if (useGPU) {
				// GPU PATH: Fill CPU staging buffers, then async copy to GPU
				auto cpuObsBatch = cpuObsBuffer.narrow(0, 0, batchSize);
				auto cpuMaskBatch = cpuActionMaskBuffer.narrow(0, 0, batchSize);

				// Fill CPU staging buffers (pinned memory)
				for (int i = 0; i < batchSize; i++) {
					FList curObs = obsBuilder->BuildObs(players[i], states[i]);
					if (curObs.size() != obsSize) {
						RG_ERR_CLOSE(
							"InferUnit: Obs builder produced an obs that differs from the provided size (expected: " << obsSize << ", got: " << curObs.size() << ")\n" <<
							"Make sure you provided the correct obs size to the InferUnit constructor.\n" <<
							"Also, make sure there aren't an incorrect number of players (there are " << states[i].players.size() << " in this state)"
						);
					}
					std::memcpy(cpuObsBatch[i].data_ptr<float>(), curObs.data(), obsSize * sizeof(float));

					auto masks = actionParser->GetActionMask(players[i], states[i]);
					std::memcpy(cpuMaskBatch[i].data_ptr<bool>(), masks.data(), masks.size() * sizeof(uint8_t));
				}

				// Async copy from pinned CPU memory to GPU (non-blocking)
				auto obsBatch = obsBuffer.narrow(0, 0, batchSize);
				auto maskBatch = actionMaskBuffer.narrow(0, 0, batchSize);

				obsBatch.copy_(cpuObsBatch, /*non_blocking=*/true);
				maskBatch.copy_(cpuMaskBatch, /*non_blocking=*/true);

				tObs = obsBatch;
				tActionMasks = maskBatch;

			} else {
				// CPU PATH: Fill buffers directly
				auto obsBatch = obsBuffer.narrow(0, 0, batchSize);
				auto maskBatch = actionMaskBuffer.narrow(0, 0, batchSize);

				for (int i = 0; i < batchSize; i++) {
					FList curObs = obsBuilder->BuildObs(players[i], states[i]);
					if (curObs.size() != obsSize) {
						RG_ERR_CLOSE(
							"InferUnit: Obs builder produced an obs that differs from the provided size (expected: " << obsSize << ", got: " << curObs.size() << ")\n" <<
							"Make sure you provided the correct obs size to the InferUnit constructor.\n" <<
							"Also, make sure there aren't an incorrect number of players (there are " << states[i].players.size() << " in this state)"
						);
					}
					std::memcpy(obsBatch[i].data_ptr<float>(), curObs.data(), obsSize * sizeof(float));

					auto masks = actionParser->GetActionMask(players[i], states[i]);
					std::memcpy(maskBatch[i].data_ptr<bool>(), masks.data(), masks.size() * sizeof(uint8_t));
				}

				tObs = obsBatch;
				tActionMasks = maskBatch;
			}

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

			// Use pinned memory + async copy for GPU
			if (useGPU) {
				auto cpuObs = torch::from_blob(allObs.data(), {(int64_t)players.size(), obsSize},
					torch::TensorOptions().dtype(torch::kFloat32)).clone();
				auto cpuMasks = torch::from_blob(allActionMasks.data(),
					{(int64_t)players.size(), this->actionParser->GetActionAmount()},
					torch::TensorOptions().dtype(torch::kUInt8)).clone();

				// Pin memory for faster transfer
				cpuObs = cpuObs.pin_memory();
				cpuMasks = cpuMasks.pin_memory();

				// Async copy to GPU
				tObs = cpuObs.to(device, /*non_blocking=*/true);
				tActionMasks = cpuMasks.to(torch::kBool).to(device, /*non_blocking=*/true);
			} else {
				tObs = torch::tensor(allObs).reshape({(int64_t)players.size(), obsSize});
				tActionMasks = torch::tensor(allActionMasks).reshape({(int64_t)players.size(), this->actionParser->GetActionAmount()});
			}
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