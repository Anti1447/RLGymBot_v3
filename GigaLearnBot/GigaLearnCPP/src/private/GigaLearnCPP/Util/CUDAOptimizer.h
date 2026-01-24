#pragma once

#include "../FrameworkTorch.h"
#include <torch/cuda.h>

namespace GGL {

// Singleton class to manage CUDA inference optimizations
class CUDAOptimizer {
private:
	static bool initialized;
	static c10::cuda::CUDAStream inferenceStream;

	CUDAOptimizer() = default;

public:
	// Initialize CUDA optimizations for inference
	static void Initialize() {
		if (initialized) {
			RG_LOG("CUDAOptimizer already initialized");
			return;
		}

		if (!torch::cuda::is_available()) {
			RG_LOG("WARNING: CUDA not available, skipping GPU optimizations");
			return;
		}

		RG_LOG("=== Initializing CUDA Inference Optimizations ===");

		// Get device info
		int deviceCount = torch::cuda::device_count();
		int currentDevice = torch::cuda::current_device();
		RG_LOG("CUDA devices available: " << deviceCount);
		RG_LOG("Using CUDA device: " << currentDevice);

		// Create dedicated stream for inference
		// Allows async execution and overlap with other operations
		inferenceStream = c10::cuda::getStreamFromPool(false, currentDevice);
		RG_LOG("Created dedicated CUDA stream for inference");

		// Enable cuDNN auto-tuner (finds fastest convolution algorithms)
		// This is a one-time cost that speeds up subsequent operations
		at::globalContext().setBenchmarkCuDNN(true);
		RG_LOG("Enabled cuDNN benchmark mode");

		// Enable cuDNN deterministic mode if needed (usually off for speed)
		// at::globalContext().setDeterministicCuDNN(true);

		// Enable TF32 for Ampere+ GPUs (3090, 4090, A100, etc.)
		// Provides ~2x speedup with minimal accuracy loss
		at::globalContext().setAllowTF32CuDNN(true);
		at::globalContext().setAllowTF32CuBLAS(true);
		RG_LOG("Enabled TF32 precision (Ampere+ GPUs)");

		initialized = true;
		RG_LOG("=== CUDA Inference Optimizations Complete ===");
	}

	// Get the dedicated inference stream
	static c10::cuda::CUDAStream& GetInferenceStream() {
		return inferenceStream;
	}

	// Synchronize the inference stream
	static void Synchronize() {
		if (initialized) {
			inferenceStream.synchronize();
		}
	}

	// Check if initialized
	static bool IsInitialized() {
		return initialized;
	}
};

// Static member initialization
bool CUDAOptimizer::initialized = false;
c10::cuda::CUDAStream CUDAOptimizer::inferenceStream = c10::cuda::getDefaultCUDAStream();

} // namespace GGL
