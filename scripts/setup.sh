#!/bin/bash
# This script handles the initial setup of the build environment.
# It downloads depot_tools, gn, and the ANGLE source code.
# It's designed to be idempotent, so it won't re-download if directories exist.

# Exit immediately if a command exits with a non-zero status.
set -e

echo "--- Running Setup Script ---"

# This script should be run from the /workspace directory.
cd /workspace

# Add tools to PATH for this script's execution context
export PATH="/workspace/tools:/workspace/tools/depot_tools:${PATH}"

# Conditionally create the 'tools' directory and download dependencies.
if [ ! -d "tools" ]; then
    echo "--- Tools directory not found, downloading dependencies ---"
    mkdir -p tools
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git tools/depot_tools
    wget --content-disposition -P tools/ https://chrome-infra-packages.appspot.com/dl/gn/gn/linux-amd64/+/latest
    unzip tools/gn-linux-amd64.zip -d tools/
else
    echo "--- Tools directory found, skipping download ---"
fi

# Conditionally fetch the ANGLE source code.
if [ ! -d "angle" ]; then
    echo "--- ANGLE directory not found, fetching source ---"
    mkdir -p angle
    cd angle
    fetch --nohooks angle
    git checkout 79ec8b3400ceeafc3e69b9bec29fa39a0e1a9a16
    gclient sync
    cd ..
else
    echo "--- ANGLE directory found, skipping fetch ---"
fi

echo "--- Setup complete. You can now run build.sh ---"