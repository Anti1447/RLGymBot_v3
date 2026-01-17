@echo off
REM MLP-Mixer Build Script for Windows
REM Quick setup, configure, and compile

setlocal enabledelayedexpansion

echo.
echo ================================================
echo MLP-Mixer Build Script (Windows)
echo ================================================
echo.

REM Get project root
set PROJECT_ROOT=%~dp0
echo Project root: %PROJECT_ROOT%

REM Step 1: Verify files exist
echo.
echo Step 1: Verifying MLP-Mixer files...
echo ========================================

set MISSING=0

for %%F in (
    "GigaLearnBot\GigaLearnCPP\src\private\GigaLearnCPP\Util\MixerBlock.h"
    "GigaLearnBot\GigaLearnCPP\src\private\GigaLearnCPP\Util\RLTokenizer.h"
    "GigaLearnBot\GigaLearnCPP\src\private\GigaLearnCPP\Util\MixerModelWrapper.h"
    "GigaLearnBot\GigaLearnCPP\src\private\GigaLearnCPP\Util\MixerModelWrapper.cpp"
    "GigaLearnBot\GigaLearnCPP\src\public\GigaLearnCPP\Util\ModelConfig.h"
    "GigaLearnBot\GigaLearnCPP\src\private\GigaLearnCPP\Util\Models.h"
    "GigaLearnBot\GigaLearnCPP\src\public\GigaLearnCPP\PPO\PPOLearnerConfig.h"
    "GigaLearnBot\GigaLearnCPP\src\private\GigaLearnCPP\PPO\PPOLearner.cpp"
) do (
    if exist "%PROJECT_ROOT%%%F" (
        echo [OK] %%F
    ) else (
        echo [MISSING] %%F
        set /a MISSING=!MISSING!+1
    )
)

if %MISSING% gtr 0 (
    echo.
    echo ERROR: %MISSING% required files are missing!
    exit /b 1
)

echo.
echo [SUCCESS] All files verified!

REM Step 2: Clean old build
echo.
echo Step 2: Cleaning old build artifacts...
echo ========================================

if exist "%PROJECT_ROOT%build" (
    echo Removing old build directory...
    rmdir /S /Q "%PROJECT_ROOT%build"
)

REM Step 3: Create build directory
echo.
echo Step 3: Creating build directory...
echo ====================================

mkdir "%PROJECT_ROOT%build"
cd /d "%PROJECT_ROOT%build"
echo Build directory: %cd%

REM Step 4: Configure CMake
echo.
echo Step 4: Configuring CMake...
echo =============================

echo Using Visual Studio 17 2022 generator...
cmake -G "Visual Studio 17 2022" -A x64 ..

if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

REM Step 5: Build
echo.
echo Step 5: Building project...
echo ============================

echo Building with Release configuration...
cmake --build . --config Release --parallel 4

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    exit /b 1
)

REM Step 6: Verify build
echo.
echo Step 6: Verifying build artifacts...
echo ====================================

if exist "Release\GigaLearnCPP.dll" (
    echo [SUCCESS] GigaLearnCPP.dll built successfully!
) else if exist "GigaLearnCPP.dll" (
    echo [SUCCESS] GigaLearnCPP.dll built successfully!
) else (
    echo [ERROR] GigaLearnCPP.dll not found in build output
    echo Checking build directory...
    dir /S GigaLearnCPP.dll
    exit /b 1
)

REM Step 7: Summary
echo.
echo ================================================
echo BUILD COMPLETE! [SUCCESS]
echo ================================================
echo.
echo Build artifacts: %PROJECT_ROOT%build
echo.
echo Next steps:
echo 1. Configure training to use MLP-Mixer
echo 2. Start training with NetworkArchitecture::MLP_MIXER
echo 3. Monitor logs for MixerModels creation message
echo.
echo See SETUP_AND_RUN.md for detailed instructions.
echo.

endlocal
