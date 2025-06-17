# Use the specified Emscripten SDK version as the base image
FROM emscripten/emsdk:3.1.71

# Set the default working directory. The repo will be mounted here.
WORKDIR /workspace

# Install system-level dependencies required for the build process.
RUN apt-get update && apt-get install -y git wget unzip python3 binaryen && rm -rf /var/lib/apt/lists/*

# Copy the build scripts into a known location in the image's filesystem.
# This makes them available to be run from the command line.
COPY scripts/setup.sh scripts/build.sh /usr/local/bin/

# Make the scripts executable
RUN chmod +x /usr/local/bin/setup.sh /usr/local/bin/build.sh

# The container will start with an interactive bash shell.
# From inside the container, you can run `setup.sh` and then `build.sh`
CMD ["bash"]