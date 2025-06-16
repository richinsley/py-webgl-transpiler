#!/bin/bash
# This script handles the compilation of the ANGLE shader translator.
# It copies source files, runs cmake, builds the target, and optimizes the wasm output.

# Exit immediately if a command exits with a non-zero status.
set -e

echo "--- Running Build Script ---"

# Ensure all operations are relative to the root of the mounted volume.
cd /workspace

# Add tools to PATH for this script's execution context
export PATH="/workspace/tools:/workspace/tools/depot_tools:${PATH}"

# Check if the ANGLE directory exists as a prerequisite.
if [ ! -d "angle" ]; then
    echo "Error: ANGLE directory not found. Please run setup.sh first."
    exit 1
fi

# Copy the custom/modified source files into the ANGLE source tree.
echo "--- Copying custom source files ---"
cp stdio_shader_translator/*.* angle/samples/shader_translator/
cp stdio_shader_translator/ANGLEShaderProgramVersion.h angle/src/common/
cp stdio_shader_translator/angle_commit.h angle/src/common/
cp stdio_shader_translator/system_utils_emscripten.cpp angle/src/common/
cp stdio_shader_translator/debug_wasm.cpp angle/src/common/

# Use absolute paths to ensure the build directory is in the correct location.
BUILD_DIR="/workspace/build"
WASM_OUT_DIR="/workspace/src/angle_translator/wasm"

# Create the build directory, configure, and compile.
echo "--- Configuring, building, and optimizing... ---"
rm -rf ${BUILD_DIR} # Clean previous build
mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

# Configure the project using emcmake for the Emscripten toolchain.
emcmake cmake .. -DANGLE_ROOT=/workspace/angle

# Compile the target specified in your CMakeLists.txt
make angle_shader_translator_standalone

# Create the final output directory.
cd /workspace
rm -rf ${WASM_OUT_DIR} # Clean previous output
mkdir -p ${WASM_OUT_DIR}

# Optimize the wasm binary with wasm-opt and copy the artifacts.
echo "--- Optimizing and copying final artifacts ---"
wasm-opt -Oz -o ${WASM_OUT_DIR}/angle_shader_translator_standalone.wasm ${BUILD_DIR}/angle_shader_translator_standalone.wasm
cp ${BUILD_DIR}/angle_shader_translator_standalone.js ${WASM_OUT_DIR}/

echo "--- Build complete. Artifacts are in the ${WASM_OUT_DIR} directory. ---"

# Change ownership of generated files to the host user.
# This requires running the container with -e HOST_UID=$(id -u) -e HOST_GID=$(id -g)
if [ -n "${HOST_UID}" ] && [ -n "${HOST_GID}" ]; then
  echo "--- Changing ownership of generated files to ${HOST_UID}:${HOST_GID} ---"
  # Set ownership on all generated directories
  chown -R "${HOST_UID}:${HOST_GID}" /workspace/angle /workspace/tools /workspace/build /workspace/wasm_out
else
  echo "--- HOST_UID/HOST_GID not set. Skipping chown. Files will be owned by root. ---"
fi