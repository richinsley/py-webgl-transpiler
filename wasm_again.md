## clone the repo
git clone git@github.com:richinsley/pyshadertranslator.git

## get depot tools and gn:
```bash
cd pyshadertranslator
mkdir -p tools
cd tools
# add depot_tools
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git .
# download gn for linux (see other platforms above)
wget --content-disposition -P tools/ https://chrome-infra-packages.appspot.com/dl/gn/gn/linux-amd64/+/latest
unzip tools/*.zip -d .
# add gn and depot tools to path
cd ..
export PATH=$PWD/tools:$PATH
```

# clone angle and build repo for x64 linux
```
# https://github.com/google/angle/blob/main/doc/DevSetup.md
mkdir -p angle
cd angle
fetch angle
cd ..
```

## build the modified shader translator using CMake
```bash
cp stdio_shader_translator/*.* angle/samples/shader_translator
cp stdio_shader_translator/ANGLEShaderProgramVersion.h angle/src/common
cp stdio_shader_translator/angle_commit.h angle/src/common
cp stdio_shader_translator/system_utils_emscripten.cpp angle/src/common
cp stdio_shader_translator/debug_wasm.cpp angle/src/common
mkdir -p build
cd build
```

# run docker and mount the current folder (pyshadertranslator) to workspace
docker run -it -v $(pwd):/workspace -w /workspace emscripten/emsdk:3.1.71 bash

# install wasm-opt:
sudo apt install binaryen

# create the build folder
mkdir build
cd build

# Use emcmake to configure the project with the Emscripten toolchain.
emcmake cmake .. -DANGLE_ROOT=/workspace/angle

# Build the target specified in your CMakeLists.txt
make angle_shader_translator_standalone

# Shrink the wasm binary
# copy to was_out
cp angle_shader_translator_standalone.* ../wasm