#!/bin/bash

# MLP-Mixer Build Script
# Quick setup, configure, and compile

set -e  # Exit on error

echo "================================================"
echo "MLP-Mixer Build Script"
echo "================================================"
echo ""

# Get the project root
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "Project root: $PROJECT_ROOT"

# Step 1: Verify all files exist
echo ""
echo "Step 1: Verifying MLP-Mixer files..."
echo "========================================"

REQUIRED_FILES=(
    "GigaLearnBot/GigaLearnCPP/src/private/GigaLearnCPP/Util/MixerBlock.h"
    "GigaLearnBot/GigaLearnCPP/src/private/GigaLearnCPP/Util/RLTokenizer.h"
    "GigaLearnBot/GigaLearnCPP/src/private/GigaLearnCPP/Util/MixerModelWrapper.h"
    "GigaLearnBot/GigaLearnCPP/src/private/GigaLearnCPP/Util/MixerModelWrapper.cpp"
    "GigaLearnBot/GigaLearnCPP/src/public/GigaLearnCPP/Util/ModelConfig.h"
    "GigaLearnBot/GigaLearnCPP/src/private/GigaLearnCPP/Util/Models.h"
    "GigaLearnBot/GigaLearnCPP/src/public/GigaLearnCPP/PPO/PPOLearnerConfig.h"
    "GigaLearnBot/GigaLearnCPP/src/private/GigaLearnCPP/PPO/PPOLearner.cpp"
)

MISSING=0
for file in "${REQUIRED_FILES[@]}"; do
    if [ -f "$PROJECT_ROOT/$file" ]; then
        echo "✅ $file"
    else
        echo "❌ MISSING: $file"
        MISSING=$((MISSING + 1))
    fi
done

if [ $MISSING -gt 0 ]; then
    echo ""
    echo "ERROR: $MISSING required files are missing!"
    exit 1
fi

echo ""
echo "✅ All files verified!"

# Step 2: Clean old build
echo ""
echo "Step 2: Cleaning old build artifacts..."
echo "========================================="

if [ -d "$PROJECT_ROOT/build" ]; then
    echo "Removing old build directory..."
    rm -rf "$PROJECT_ROOT/build"
fi

# Step 3: Create build directory
echo ""
echo "Step 3: Creating build directory..."
echo "======================================"

mkdir -p "$PROJECT_ROOT/build"
cd "$PROJECT_ROOT/build"
echo "Build directory: $(pwd)"

# Step 4: Configure CMake
echo ""
echo "Step 4: Configuring CMake..."
echo "============================="

# Detect OS and use appropriate generator
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    # Windows
    echo "Detected Windows - using Visual Studio generator"
    cmake -G "Visual Studio 17 2022" -A x64 ..
else
    # Linux/Mac
    echo "Detected Unix-like system"
    cmake ..
fi

# Step 5: Build
echo ""
echo "Step 5: Building project..."
echo "=============================="

if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    # Windows build
    echo "Building with Visual Studio..."
    cmake --build . --config Release --parallel 4
else
    # Unix build
    echo "Building with Make..."
    make -j4
fi

# Step 6: Verify build
echo ""
echo "Step 6: Verifying build artifacts..."
echo "======================================="

if [ -f "GigaLearnCPP.dll" ] || [ -f "libGigaLearnCPP.so" ] || [ -f "libGigaLearnCPP.dylib" ]; then
    echo "✅ GigaLearnCPP library built successfully!"
else
    echo "❌ Build completed but library not found"
    exit 1
fi

# Step 7: Check for symbols
echo ""
echo "Step 7: Verifying MLP-Mixer symbols..."
echo "========================================="

if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    echo "Checking symbols with DUMPBIN..."
    if dumpbin /EXPORTS GigaLearnCPP.dll | grep -i "Mixer\|Tokenizer" > /dev/null; then
        echo "✅ MLP-Mixer symbols found in library!"
    else
        echo "⚠️  Could not verify symbols (may still be OK)"
    fi
else
    echo "Checking symbols with nm..."
    if nm libGigaLearnCPP.so | grep -i "Mixer\|Tokenizer" > /dev/null 2>&1; then
        echo "✅ MLP-Mixer symbols found in library!"
    else
        echo "⚠️  Could not verify symbols (may still be OK)"
    fi
fi

# Step 8: Summary
echo ""
echo "================================================"
echo "BUILD COMPLETE! ✅"
echo "================================================"
echo ""
echo "Build artifacts in: $PROJECT_ROOT/build"
echo ""
echo "Next steps:"
echo "1. Configure training to use MLP-Mixer"
echo "2. Start training with NetworkArchitecture::MLP_MIXER"
echo "3. Monitor logs for MixerModels creation message"
echo ""
echo "See SETUP_AND_RUN.md for detailed instructions."
echo ""
