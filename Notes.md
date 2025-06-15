
## get depot tools and gn:
```bash
# https://chrome-infra-packages.appspot.com/dl/gn/gn/linux-amd64/+/latest
# https://chrome-infra-packages.appspot.com/dl/gn/gn/mac-amd64/+/latest
# https://chrome-infra-packages.appspot.com/dl/gn/gn/windows-amd64/+/latest
mkdir -p tools
cd tools
# add depot_tools
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git .
# download gn for linux (see other platforms above)
wget --content-disposition -P tools/ https://chrome-infra-packages.appspot.com/dl/gn/gn/linux-amd64/+/latest
unzip tools/*.zip -d .
# chmod a+x tools/gn
# add gn and depot tools to path
cd ..
export PATH=$PWD/tools:$PWD/tools/depot_tools:$PATH
```

# clone angle and build repo for x64 linux
```
# https://github.com/google/angle/blob/main/doc/DevSetup.md
mkdir -p angle
cd angle
fetch angle

# Clean previous build (optional, but recommended for a clean switch)
# rm -rf out/M1_Static_Release # Or whatever you want to name it

# Configure for x64 or arm64, release, and more static linking
gn gen out/static_release --args='
    target_cpu="x64"
    is_debug=false
    is_component_build=false
    angle_enable_metal=false
    angle_enable_vulkan=false
    angle_enable_gl=true
    angle_enable_wgpu=true
    use_custom_libcxx=false
    angle_build_all_angle_gl_platforms=false
    treat_warnings_as_errors=false
'
# Explanation of GN Args:
#   target_cpu="arm64"             : arm64/x64 platform sepcifivication. Use "x64" for Intel Macs.
#   is_debug=false                 : Creates an optimized release build (smaller, faster).
#                                      Set to true if you need debug symbols for ANGLE itself.
#   is_component_build=false       : This is key. It tells the build system to link
#                                      dependencies statically into larger modules (e.g.,
#                                      libangle_util into libGLESv2/libEGL) rather
#                                      than creating many small dylibs.
#   angle_enable_vulkan=false      : Disables the Vulkan backend if you only plan to use Metal on macOS.
#                                      This can reduce dependencies on Dawn.
#   angle_enable_gl=false          : Disables the desktop OpenGL backend.
#   use_custom_libcxx=false        : Tries to use the system's C++ standard library
#                                      instead of a custom chrome version (libc++_chrome.dylib).
#                                      This may or may not work depending on ANGLE's current requirements,
#                                      but it's good for reducing custom dylibs. If it fails, remove it.
#   angle_build_all_angle_gl_platforms=false: Reduces build scope.
#   treat_warnings_as_errors=false : Can be helpful to avoid build failures on warnings during experimentation.
```

## build the modified shader translator using CMake
```bash
cp stdio_shader_translator/*.* angle/samples/shader_translator
cp stdio_shader_translator/ANGLEShaderProgramVersion.h angle/src/common
cp stdio_shader_translator/angle_commit.h angle/src/common
mkdir -p build
cd build
cmake -DANGLE_ROOT=$PWD/../angle -DWASM_SOURCES=/home/rich/projects/libangle/angle_wasm_sources ..
```
