cmake_minimum_required(VERSION 3.16)
project(AngleShaderTranslatorStandalone CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Define ANGLE root for convenience (assuming CMakeLists.txt is in ANGLE root)
if (NOT DEFINED ANGLE_ROOT)
set(ANGLE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# --- Source Files ---

set(ANGLE_SHADER_TRANSLATOR_EXECUTABLE_SOURCES
    ${ANGLE_ROOT}/samples/shader_translator/shader_translator.cpp
)

set(TRANSLATOR_CORE_SOURCES
    # Base translator sources (contents from previous response)
    ${ANGLE_ROOT}/src/compiler/translator/BaseTypes.cpp
    ${ANGLE_ROOT}/src/compiler/translator/BuiltInFunctionEmulator.cpp
    ${ANGLE_ROOT}/src/compiler/translator/CallDAG.cpp
    ${ANGLE_ROOT}/src/compiler/translator/CodeGen.cpp
    ${ANGLE_ROOT}/src/compiler/translator/CollectVariables.cpp
    ${ANGLE_ROOT}/src/compiler/translator/Compiler.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ConstantUnion.cpp
    ${ANGLE_ROOT}/src/compiler/translator/Declarator.cpp
    ${ANGLE_ROOT}/src/compiler/translator/Diagnostics.cpp
    ${ANGLE_ROOT}/src/compiler/translator/DirectiveHandler.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ExtensionBehavior.cpp
    ${ANGLE_ROOT}/src/compiler/translator/FlagStd140Structs.cpp
    ${ANGLE_ROOT}/src/compiler/translator/FunctionLookup.cpp
    ${ANGLE_ROOT}/src/compiler/translator/HashNames.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ImmutableStringBuilder.cpp
    ${ANGLE_ROOT}/src/compiler/translator/InfoSink.cpp
    ${ANGLE_ROOT}/src/compiler/translator/Initialize.cpp
    ${ANGLE_ROOT}/src/compiler/translator/InitializeDll.cpp
    ${ANGLE_ROOT}/src/compiler/translator/IntermNode.cpp
    ${ANGLE_ROOT}/src/compiler/translator/IntermRebuild.cpp
    ${ANGLE_ROOT}/src/compiler/translator/IsASTDepthBelowLimit.cpp
    ${ANGLE_ROOT}/src/compiler/translator/Name.cpp
    ${ANGLE_ROOT}/src/compiler/translator/Operator.cpp
    ${ANGLE_ROOT}/src/compiler/translator/OutputTree.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ParseContext.cpp
    ${ANGLE_ROOT}/src/compiler/translator/PoolAlloc.cpp
    ${ANGLE_ROOT}/src/compiler/translator/QualifierTypes.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ShaderLang.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ShaderVars.cpp
    ${ANGLE_ROOT}/src/compiler/translator/Symbol.cpp
    ${ANGLE_ROOT}/src/compiler/translator/SymbolTable.cpp
    ${ANGLE_ROOT}/src/compiler/translator/SymbolUniqueId.cpp
    ${ANGLE_ROOT}/src/compiler/translator/Types.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ValidateAST.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ValidateBarrierFunctionCall.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ValidateClipCullDistance.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ValidateGlobalInitializer.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ValidateLimitations.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ValidateMaxParameters.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ValidateOutputs.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ValidateSwitch.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ValidateTypeSizeLimitations.cpp
    ${ANGLE_ROOT}/src/compiler/translator/ValidateVaryingLocations.cpp
    ${ANGLE_ROOT}/src/compiler/translator/VariablePacker.cpp
    ${ANGLE_ROOT}/src/compiler/translator/blocklayout.cpp
    ${ANGLE_ROOT}/src/compiler/translator/glslang_lex_autogen.cpp # Assumes pre-generated
    ${ANGLE_ROOT}/src/compiler/translator/glslang_tab_autogen.cpp # Assumes pre-generated
    # tree_ops (contents from previous response)
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/ClampFragDepth.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/ClampIndirectIndices.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/ClampPointSize.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/DeclareAndInitBuiltinsForInstancedMultiview.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/DeclarePerVertexBlocks.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/DeferGlobalInitializers.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/EmulateGLFragColorBroadcast.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/EmulateMultiDrawShaderBuiltins.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/FoldExpressions.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/ForcePrecisionQualifier.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/InitializeVariables.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/MonomorphizeUnsupportedFunctions.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/PreTransformTextureCubeGradDerivatives.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/PruneEmptyCases.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/PruneInfiniteLoops.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/PruneNoOps.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RecordConstantPrecision.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/ReduceInterfaceBlocks.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RemoveArrayLengthMethod.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RemoveAtomicCounterBuiltins.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RemoveDynamicIndexing.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RemoveInactiveInterfaceVariables.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RemoveInvariantDeclaration.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RemoveUnreferencedVariables.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RemoveUnusedFramebufferFetch.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RescopeGlobalVariables.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RewriteArrayOfArrayOfOpaqueUniforms.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RewriteAtomicCounters.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RewriteDfdy.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RewritePixelLocalStorage.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RewriteStructSamplers.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/RewriteTexelFetchOffset.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/ScalarizeVecAndMatConstructorArgs.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/SeparateDeclarations.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/SeparateStructFromUniformDeclarations.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/SimplifyLoopConditions.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/SplitSequenceOperator.cpp
    # tree_util (contents from previous response)
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/DriverUniform.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/FindFunction.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/FindMain.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/FindPreciseNodes.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/FindSymbolNode.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/IntermNodePatternMatcher.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/IntermNode_util.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/IntermTraverse.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/ReplaceArrayOfMatrixVarying.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/ReplaceClipCullDistanceVariable.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/ReplaceShadowingVariables.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/ReplaceVariable.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/RewriteSampleMaskVariable.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/RunAtTheBeginningOfShader.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/RunAtTheEndOfShader.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_util/SpecializationConstant.cpp
    ${ANGLE_ROOT}/src/compiler/translator/util.cpp
    # From angle_translator_essl_symbol_table_sources
    ${ANGLE_ROOT}/src/compiler/translator/ImmutableString_autogen.cpp # Assumes pre-generated
    ${ANGLE_ROOT}/src/compiler/translator/SymbolTable_autogen.cpp   # Assumes pre-generated
)

set(TRANSLATOR_GLSL_BASE_SOURCES
    ${ANGLE_ROOT}/src/compiler/translator/glsl/OutputGLSLBase.cpp
)
set(TRANSLATOR_GLSL_AND_VULKAN_BASE_SOURCES
    ${ANGLE_ROOT}/src/compiler/translator/glsl/OutputGLSL.cpp
)

set(TRANSLATOR_ESSL_BACKEND_SOURCES
    ${TRANSLATOR_GLSL_BASE_SOURCES}
    ${ANGLE_ROOT}/src/compiler/translator/glsl/OutputESSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/glsl/TranslatorESSL.cpp
)

set(TRANSLATOR_GLSL_BACKEND_SOURCES
    ${TRANSLATOR_GLSL_BASE_SOURCES}
    ${TRANSLATOR_GLSL_AND_VULKAN_BASE_SOURCES}
    ${ANGLE_ROOT}/src/compiler/translator/glsl/BuiltInFunctionEmulatorGLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/glsl/ExtensionGLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/glsl/TranslatorGLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/glsl/VersionGLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/glsl/RegenerateStructNames.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/glsl/RewriteRepeatedAssignToSwizzled.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/glsl/UseInterfaceBlockFields.cpp
)

set(TRANSLATOR_HLSL_BACKEND_SOURCES
    # (contents from previous response)
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/ASTMetadataHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/AtomicCounterFunctionHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/BuiltInFunctionEmulatorHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/ImageFunctionHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/OutputHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/ResourcesHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/ShaderStorageBlockFunctionHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/ShaderStorageBlockOutputHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/StructureHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/TextureFunctionHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/TranslatorHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/UtilsHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/blocklayoutHLSL.cpp
    ${ANGLE_ROOT}/src/compiler/translator/hlsl/emulated_builtin_functions_hlsl_autogen.cpp # Assumes pre-generated
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/AddDefaultReturnStatements.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/AggregateAssignArraysInSSBOs.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/AggregateAssignStructsInSSBOs.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/ArrayReturnValueToOutParameter.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/BreakVariableAliasingInInnerLoops.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/ExpandIntegerPowExpressions.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/RecordUniformBlocksWithLargeArrayMember.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/RemoveSwitchFallThrough.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/RewriteAtomicFunctionExpressions.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/RewriteElseBlocks.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/RewriteExpressionsWithShaderStorageBlock.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/RewriteUnaryMinusOperatorInt.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/SeparateArrayConstructorStatements.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/SeparateArrayInitialization.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/SeparateExpressionsReturningArrays.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/UnfoldShortCircuitToIf.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/hlsl/WrapSwitchStatementsInBlocks.cpp
)

set(TRANSLATOR_SPIRV_BACKEND_SOURCES
    ${TRANSLATOR_GLSL_BASE_SOURCES}
    ${TRANSLATOR_GLSL_AND_VULKAN_BASE_SOURCES}
    ${ANGLE_ROOT}/src/compiler/translator/spirv/BuildSPIRV.cpp
    ${ANGLE_ROOT}/src/compiler/translator/spirv/BuiltinsWorkaround.cpp
    ${ANGLE_ROOT}/src/compiler/translator/spirv/OutputSPIRV.cpp
    ${ANGLE_ROOT}/src/compiler/translator/spirv/TranslatorSPIRV.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/ClampGLLayer.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/EmulateAdvancedBlendEquations.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/EmulateDithering.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/EmulateFragColorData.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/EmulateFramebufferFetch.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/EmulateYUVBuiltIns.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/FlagSamplersWithTexelFetch.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/ReswizzleYUVOps.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/RewriteInterpolateAtOffset.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/RewriteR32fImages.cpp
    ${ANGLE_ROOT}/src/compiler/translator/tree_ops/spirv/RewriteSamplerExternalTexelFetch.cpp
)

set(PREPROCESSOR_SOURCES
    # (contents from previous response)
    ${ANGLE_ROOT}/src/compiler/preprocessor/DiagnosticsBase.cpp
    ${ANGLE_ROOT}/src/compiler/preprocessor/DirectiveHandlerBase.cpp
    ${ANGLE_ROOT}/src/compiler/preprocessor/DirectiveParser.cpp
    ${ANGLE_ROOT}/src/compiler/preprocessor/Input.cpp
    ${ANGLE_ROOT}/src/compiler/preprocessor/Lexer.cpp
    ${ANGLE_ROOT}/src/compiler/preprocessor/Macro.cpp
    ${ANGLE_ROOT}/src/compiler/preprocessor/MacroExpander.cpp
    ${ANGLE_ROOT}/src/compiler/preprocessor/Preprocessor.cpp
    ${ANGLE_ROOT}/src/compiler/preprocessor/Token.cpp
    ${ANGLE_ROOT}/src/compiler/preprocessor/preprocessor_lex_autogen.cpp # Assumes pre-generated
    ${ANGLE_ROOT}/src/compiler/preprocessor/preprocessor_tab_autogen.cpp # Assumes pre-generated
)

# Placeholder - POPULATE FROM ANGLE'S .gni FILES (e.g., libangle_common_sources)
set(ANGLE_COMMON_SOURCES
    ${ANGLE_ROOT}/src/common/angle_version_info.cpp
    ${ANGLE_ROOT}/src/common/third_party/xxhash/xxhash.c # xxhash is often part of angle_common
    # .cpp files from libangle_common_sources definition
    ${ANGLE_ROOT}/src/common/Float16ToFloat32.cpp
    ${ANGLE_ROOT}/src/common/MemoryBuffer.cpp
    ${ANGLE_ROOT}/src/common/PackedEGLEnums_autogen.cpp # Assumes pre-generated
    ${ANGLE_ROOT}/src/common/PackedEnums.cpp
    ${ANGLE_ROOT}/src/common/PackedGLEnums_autogen.cpp # Assumes pre-generated
    ${ANGLE_ROOT}/src/common/PoolAlloc.cpp # Also in TRANSLATOR_CORE_SOURCES, ensure listed once effectively
    # ${ANGLE_ROOT}/src/common/SimpleMutex.cpp
    ${ANGLE_ROOT}/src/common/WorkerThread.cpp
    ${ANGLE_ROOT}/src/common/aligned_memory.cpp
    ${ANGLE_ROOT}/src/common/android_util.cpp
    ${ANGLE_ROOT}/src/common/angleutils.cpp
    ${ANGLE_ROOT}/src/common/base/anglebase/sha1.cc
    ${ANGLE_ROOT}/src/common/entry_points_enum_autogen.cpp # Assumes pre-generated
    ${ANGLE_ROOT}/src/common/event_tracer.cpp
    ${ANGLE_ROOT}/src/common/mathutil.cpp
    ${ANGLE_ROOT}/src/common/matrix_utils.cpp
    ${ANGLE_ROOT}/src/common/platform_helpers.cpp
    ${ANGLE_ROOT}/src/common/string_utils.cpp
    ${ANGLE_ROOT}/src/common/system_utils.cpp
    ${ANGLE_ROOT}/src/common/tls.cpp
    ${ANGLE_ROOT}/src/common/uniform_type_info_autogen.cpp # Assumes pre-generated
    ${ANGLE_ROOT}/src/common/utilities.cpp
)

set(ANGLE_COMMON_SHADER_STATE_SOURCES
    ${ANGLE_ROOT}/src/common/CompiledShaderState.cpp
)

# ANGLE SPIR-V utility sources (from src/common/spirv/BUILD.gn)
set(ANGLE_SPIRV_UTIL_SOURCES
    ${ANGLE_ROOT}/src/common/spirv/angle_spirv_utils.cpp
    ${ANGLE_ROOT}/src/common/spirv/spirv_instruction_builder_autogen.cpp # Assumes pre-generated
    ${ANGLE_ROOT}/src/common/spirv/spirv_instruction_parser_autogen.cpp  # Assumes pre-generated
)

## ALERT we need to ensure this works with WASM or create a WASM specific version
##  this is abstracted in angle/src/common/system_utils.cpp
if(EMSCRIPTEN)
    # Emscripten has its own implementation for system utilities.
    set(SYSTEM_UTILS_SOURCES
        ${ANGLE_ROOT}/src/common/system_utils_emscripten.cpp
    )
else()
    # Your original logic for native builds (Linux, etc.)
    set(SYSTEM_UTILS_SOURCES
        ${ANGLE_ROOT}/src/common/system_utils_posix.cpp
        ${ANGLE_ROOT}/src/common/system_utils_linux.cpp
    )
endif()

# --- Executable Target ---
add_executable(angle_shader_translator_standalone
    ${ANGLE_SHADER_TRANSLATOR_EXECUTABLE_SOURCES}
    ${TRANSLATOR_CORE_SOURCES}
    ${TRANSLATOR_ESSL_BACKEND_SOURCES}
    ${TRANSLATOR_GLSL_BACKEND_SOURCES}
    ${PREPROCESSOR_SOURCES}
    ${ANGLE_COMMON_SOURCES}
    ${ANGLE_COMMON_SHADER_STATE_SOURCES}
    ${SYSTEM_UTILS_SOURCES}
    ## for spirv/vulkan backend
    # ${TRANSLATOR_SPIRV_BACKEND_SOURCES}
    # ${ANGLE_SPIRV_UTIL_SOURCES}
    ## for hlsl backend
    # ${TRANSLATOR_HLSL_BACKEND_SOURCES}
)

# Conditionally add the correct implementation of debug.cpp
if(EMSCRIPTEN)
    target_sources(angle_shader_translator_standalone PRIVATE
        ${ANGLE_ROOT}/src/common/debug_wasm.cpp
    )
else()
    target_sources(angle_shader_translator_standalone PRIVATE
        ${ANGLE_ROOT}/src/common/debug.cpp
    )
endif()

# --- Include Directories ---
target_include_directories(angle_shader_translator_standalone PUBLIC
    ${ANGLE_ROOT}/include
    ${ANGLE_ROOT}/src
    # Paths to external SPIRV-Headers and SPIRV-Tools headers
    # These paths assume they are located in ANGLE's third_party directory
    # or that you provide them via CMake cache variables (e.g., -DSPIRV_HEADERS_INCLUDE_DIR=...)
    ${ANGLE_ROOT}/third_party/spirv-headers/include
    ${ANGLE_ROOT}/third_party/spirv-tools/include
    ${ANGLE_ROOT}/third_party/spirv-headers/src/include
    ${ANGLE_ROOT}/third_party/spirv-tools/src/include/
    ${ANGLE_ROOT}/src/common/base
    ${ANGLE_ROOT}/src/common/spirv
    ${ANGLE_ROOT}/src/common/third_party/xxhash
    ${CMAKE_CURRENT_SOURCE_DIR} # we need to copy ANGLEShaderProgramVersion.h and angle_commit.h from build folder
)

# --- Compile Definitions ---
target_compile_definitions(angle_shader_translator_standalone PRIVATE
    # Common definitions
    ANGLE_ENABLE_GLSL
    # ANGLE_ENABLE_WGPU
    ANGLE_ENABLE_ESSL
    GL_GLES_PROTOTYPES=0
    EGL_EGL_PROTOTYPES=0
    ANGLE_STATIC=1
    ANGLE_EXPORT=

    # --- Platform-specific definitions ---
    
    # IMPORTANT: These definitions are now conditional on NOT being an Emscripten build.
    # This prevents ANGLE_IS_LINUX from being defined when cross-compiling to WASM,
    # which is the root cause of the futex.h inclusion issue.
    $<$<AND:$<NOT:$<BOOL:EMSCRIPTEN>>,$<PLATFORM_ID:Windows>>:ANGLE_IS_WIN>
    $<$<AND:$<NOT:$<BOOL:EMSCRIPTEN>>,$<PLATFORM_ID:Linux>>:ANGLE_IS_LINUX>
    $<$<AND:$<NOT:$<BOOL:EMSCRIPTEN>>,$<PLATFORM_ID:Darwin>>:ANGLE_IS_APPLE>
)

# if(EMSCRIPTEN)
#     target_link_options(angle_shader_translator_standalone PRIVATE
#         # Tell the C++ compiler to enable exceptions.
#         "SHELL:-fexceptions"
        
#         # Tell the Emscripten linker to generate JS glue code to handle exceptions.
#         # This provides the `__cxa_throw` implementation.
#         "SHELL:-s DISABLE_EXCEPTION_CATCHING=0"

#         "SHELL:-s WASM=1"
#         "SHELL:-s MODULARIZE=1"
#         "SHELL:-s EXPORT_ES6=1"
#         "SHELL:-s ENVIRONMENT=node"
#         "SHELL:-s ALLOW_MEMORY_GROWTH=1"
#         "SHELL:-s EXPORTED_FUNCTIONS=['_invoke','_malloc','_free']"
#         "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
#     )
# endif()
if(EMSCRIPTEN)
    target_link_options(angle_shader_translator_standalone PRIVATE
        # Build a self-contained WASM module with no JS glue.
        # This solves all C library import errors (memcpy, exit, etc).
        "SHELL:-s STANDALONE_WASM=1"

        # Use the older, non-native exception model.
        # This tells the C++ compiler to enable exceptions...
        "SHELL:-fno-exceptions"
        "SHELL:-fno-unwind-tables"
        "SHELL:-fno-asynchronous-unwind-tables"

        # --- Memory & Exports ---
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s EXPORTED_FUNCTIONS=['_initialize','_invoke','_malloc','_free']"
    )
endif()

# --- Message for user ---
message(STATUS "ANGLE Standalone Shader Translator configured.")
message(STATUS "PLEASE VERIFY:")
message(STATUS "1. ANGLE_COMMON_SOURCES and ANGLE_COMMON_SHADER_STATE_SOURCES variables in CMakeLists.txt are correctly populated with *all* source files from ANGLE's .gni definitions (e.g., libangle_common_sources).")
message(STATUS "2. Paths to SPIRV-Headers and SPIRV-Tools include directories are correct. This script assumes they are in ${ANGLE_ROOT}/third_party/...")
message(STATUS "3. All autogenerated source files (e.g., *_autogen.cpp) are present or generated before building.")
message(STATUS "4. This build does *not* compile or link against SPIRV-Tools *libraries* (like libSPIRV-Tools-opt.a or libSPIRV-Tools-val.a). If ANGLE's C++ code requires these, they must be built and linked separately.")