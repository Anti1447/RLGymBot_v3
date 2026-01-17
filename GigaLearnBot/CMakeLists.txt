cmake_minimum_required (VERSION 3.10)

project("GigaLearnBot")

# Force all executables and libraries to output to the build root (not Debug/Release subdirs)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
# Also set config-specific versions to the same location (for Visual Studio)
foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG} CONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG} "${CMAKE_BINARY_DIR}/lib")
endforeach()

# Make sure CMake finds libtorch if its in this directory
if (EXISTS "${PROJECT_SOURCE_DIR}/GigaLearnCPP/libtorch/")
	message("Using local libtorch folder...")
	list(APPEND CMAKE_PREFIX_PATH "GigaLearnCPP/libtorch")
	# Make ultra-sure we can find libtorch if its local
	set(CMAKE_PREFIX_PATH "GigaLearnCPP/libtorch/share/cmake/Torch")
endif()

# Add libtorch (https://pytorch.org/cppdocs/installing.html#minimal-example)
find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

# Define sources for the main GigaLearnBot executable
file(GLOB_RECURSE GIGALEARNBOT_FILES_SRC "src/*.cpp" "src/*.h" "src/*.hpp")
list(REMOVE_ITEM GIGALEARNBOT_FILES_SRC
     "${CMAKE_CURRENT_SOURCE_DIR}/src/rlbotmain.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/RLBotClient.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/RLBotClient.h"
)
add_executable(GigaLearnBot ${GIGALEARNBOT_FILES_SRC})

# Define sources for the new rlbot executable
add_executable(rlbot "src/rlbotmain.cpp" "src/RLBotClient.cpp" "src/RLBotClient.h")


# Set C++ version to 20 for GigaLearnBot
set_target_properties(GigaLearnBot PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(GigaLearnBot PROPERTIES CXX_STANDARD 20)
set_target_properties(GigaLearnBot PROPERTIES CXX_STANDARD_REQUIRED ON)

# Set C++ version to 20 for rlbot
set_target_properties(rlbot PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(rlbot PROPERTIES CXX_STANDARD 20)
set_target_properties(rlbot PROPERTIES CXX_STANDARD_REQUIRED ON)


# GigaLearnCPP will use the CMAKE_RUNTIME/LIBRARY_OUTPUT_DIRECTORY settings above

# Include GigaLearnCPP
add_subdirectory(GigaLearnCPP)
target_link_libraries(GigaLearnBot PUBLIC GigaLearnCPP)
target_link_libraries(rlbot PUBLIC GigaLearnCPP)

# Link torch libraries
target_link_libraries(GigaLearnBot PRIVATE "${TORCH_LIBRARIES}")
target_link_libraries(rlbot PRIVATE "${TORCH_LIBRARIES}")

# Include RLBotCPP
add_subdirectory(RLBotCPP)
target_link_libraries(GigaLearnBot PUBLIC RLBotCPP)
target_link_libraries(rlbot PUBLIC RLBotCPP)

# Suppress harmless PyTorch size_t warnings (MSVC)
if (MSVC)
    target_compile_options(GigaLearnBot PRIVATE /wd4267)
    target_compile_options(rlbot PRIVATE /wd4267)
endif()
