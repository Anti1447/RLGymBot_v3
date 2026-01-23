#pragma once

#include "../FrameworkTorch.h"
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Undefine min/max macros from Windows.h to avoid conflicts with std::min/std::max and torch::min
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#else
#include <sched.h>
#include <pthread.h>
#endif

namespace GGL {

    // Singleton class to manage LibTorch inference optimizations
    class InferenceOptimizer {
    private:
        static bool initialized;
        static int numPhysicalCores;
        static int numLogicalCores;

        InferenceOptimizer() = default;

    public:
        // Initialize all LibTorch CPU optimizations
        static void Initialize() {
            if (initialized) {
                RG_LOG("InferenceOptimizer already initialized");
                return;
            }

            RG_LOG("=== Initializing LibTorch CPU Inference Optimizations ===");

            // Detect CPU cores
            numLogicalCores = std::thread::hardware_concurrency();
            numPhysicalCores = numLogicalCores / 2; // Assume hyperthreading

            RG_LOG("Detected " << numLogicalCores << " logical cores, " << numPhysicalCores << " physical cores");

            // Configure threading for optimal CPU inference
            ConfigureThreading();

            // Enable oneDNN/MKL-DNN backend
            EnableMKLDNN();

            // Set inference-specific settings
            SetInferenceMode();

            // Configure CPU affinity
            SetCPUAffinity();

            initialized = true;
            RG_LOG("=== LibTorch Inference Optimizations Complete ===");
        }

        static void ConfigureThreading() {
            // Intra-op parallelism: threads within a single operation
            // Use physical cores to avoid hyperthreading overhead
            torch::set_num_threads(numPhysicalCores);
            RG_LOG("Set intra-op threads: " << numPhysicalCores);

            // Inter-op parallelism: threads for independent operations
            // Keep low to avoid oversubscription (2 is optimal for most workloads)
            torch::set_num_interop_threads(2);
            RG_LOG("Set inter-op threads: 2");

            // Enable thread pool for better scheduling
            at::set_num_threads(numPhysicalCores);
        }

        static void EnableMKLDNN() {
            #ifdef USE_MKLDNN
            // MKL-DNN/oneDNN provides highly optimized CPU kernels
            // This is the biggest performance win for CPU inference
            RG_LOG("MKL-DNN/oneDNN backend enabled via compile flag");
            #else
            RG_LOG("WARNING: MKL-DNN not available. Rebuild LibTorch with MKL support for 5-15x speedup!");
            RG_LOG("  See: https://github.com/pytorch/pytorch#from-source");
            #endif

            // NOTE: Advanced JIT optimizations disabled - requires LibTorch 1.9+
            // setGraphExecutorOptimize and set_fusion_strategy not available in this version
            RG_LOG("Graph optimizations: Using default settings (upgrade to LibTorch 1.9+ for advanced JIT)");
        }

        static void SetInferenceMode() {
            // NOTE: Global inference mode disabled - API not available in this LibTorch version
            // Use torch::NoGradGuard locally in inference functions instead
            RG_LOG("Inference mode: Use NoGradGuard in inference code");

            // Memory optimization strategies not available in this version
        }

        static void SetCPUAffinity() {
            #ifdef _WIN32
            // Windows: Set processor affinity mask
            HANDLE process = GetCurrentProcess();
            DWORD_PTR processAffinityMask = (1ULL << numPhysicalCores) - 1;
            if (SetProcessAffinityMask(process, processAffinityMask)) {
                RG_LOG("CPU affinity set to " << numPhysicalCores << " cores (Windows)");
            } else {
                RG_LOG("WARNING: Failed to set CPU affinity (Windows)");
            }
            #else
            // Linux: Use sched_setaffinity
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            for (int i = 0; i < numPhysicalCores; i++) {
                CPU_SET(i, &cpuset);
            }
            if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0) {
                RG_LOG("CPU affinity set to " << numPhysicalCores << " cores (Linux)");
            } else {
                RG_LOG("WARNING: Failed to set CPU affinity (Linux)");
            }
            #endif
        }

        // Get optimal batch size for current hardware
        static int GetOptimalBatchSize() {
            // Heuristic: Use batch size that's a multiple of physical cores
            // and fits well in L3 cache
            return numPhysicalCores * 4; // Typically 32-64 for most CPUs
        }

        // Check if optimizations are active
        static bool IsInitialized() {
            return initialized;
        }

        static int GetNumPhysicalCores() {
            return numPhysicalCores;
        }

        static int GetNumLogicalCores() {
            return numLogicalCores;
        }
    };

    // Static member initialization
    bool InferenceOptimizer::initialized = false;
    int InferenceOptimizer::numPhysicalCores = 0;
    int InferenceOptimizer::numLogicalCores = 0;

} // namespace GGL
