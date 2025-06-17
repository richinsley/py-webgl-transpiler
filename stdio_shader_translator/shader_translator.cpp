//
// Copyright 2002 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "GLSLANG/ShaderLang.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <vector>
#include "angle_gl.h"

#include <iostream>
#include "base64.hpp"
#include "json.hpp"
using json = nlohmann::json;
using namespace base64;

#if defined(ANGLE_ENABLE_VULKAN)
// SPIR-V tools include for disassembly.
#    include <spirv-tools/libspirv.hpp>
#endif

//
// Return codes from main.
//
enum TFailCode
{
    ESuccess = 0,
    EFailUsage,
    EFailCompile,
    EFailCompilerCreate,
    EFailJSONRPCParse = -32700,
    EFailJSONRPCInvalidRequest = -32600,
    EFailJSONRPCMethodNotFound = -32601,
    EFailJSONRPCInvalidParams = -32602,
    EFailJSONRPCInternalError = -32603,
};

static void usage();
static sh::GLenum FindShaderType(const char *fileName);
static bool CompileFile(char *fileName, ShHandle compiler, const ShCompileOptions &compileOptions);
static void LogMsg(const char *msg, const char *name, const int num, const char *logName);
static void PrintVariable(const std::string &prefix, size_t index, const sh::ShaderVariable &var);
static void PrintActiveVariables(ShHandle compiler);
// 
static void GenerateResources(ShBuiltInResources *resources); // From original
static sh::GLenum FindShaderTypeFromJson(const std::string &typeName);
static bool ParseGLSLOutputVersion(const std::string &num, ShShaderOutput *outResult); // From original
static bool ParseIntValue(const std::string &num, int emptyDefault, int *outValue); // From original
static void PrintSpirvToBuffer(const sh::BinaryBlob &blob, std::string& out_buffer); // Modified for string output
static json SerializeShaderVariable(const sh::ShaderVariable &var);
static json SerializeActiveVariablesToJson(ShHandle compiler);

// jl - a simple null hash function to disable name mangling
const khronos_uint64_t FNV_PRIME = 1099511628211ULL; // 2^40 + 2^8 + 0xB3
const khronos_uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL; // 64-bit offset basis
static khronos_uint64_t FNVHashFunction(const char *str, size_t len) {
    khronos_uint64_t hash = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<khronos_uint64_t>(str[i]);
        hash *= FNV_PRIME;
    }
    return hash;
}

// Modified version of PrintSpirv
#if defined(ANGLE_ENABLE_VULKAN)
#    include <spirv-tools/libspirv.hpp>
void PrintSpirvToBuffer(const sh::BinaryBlob &blob, std::string& out_buffer) {
    spvtools::SpirvTools spirvTools(SPV_ENV_VULKAN_1_1);
    spirvTools.Disassemble(blob, &out_buffer,
                           SPV_BINARY_TO_TEXT_OPTION_COMMENT | SPV_BINARY_TO_TEXT_OPTION_INDENT |
                               SPV_BINARY_TO_TEXT_OPTION_NESTED_INDENT);
}
#else
void PrintSpirvToBuffer(const sh::BinaryBlob &blob, std::string& out_buffer) {
    out_buffer = "SPIR-V disassembly not available (ANGLE_ENABLE_VULKAN not defined).";
}
#endif

// Definition for FindShaderTypeFromJson
static sh::GLenum FindShaderTypeFromJson(const std::string &typeName) {
    if (typeName == "vertex") return GL_VERTEX_SHADER;
    if (typeName == "fragment") return GL_FRAGMENT_SHADER;
    if (typeName == "compute") return GL_COMPUTE_SHADER;
    if (typeName == "geometry") return GL_GEOMETRY_SHADER_EXT; // Ensure ANGLE_ সাধারণত_SHADER_EXT is defined if you use this
    if (typeName == "tess_control") return GL_TESS_CONTROL_SHADER_EXT; // Ensure ANGLE_TESS_CONTROL_SHADER_EXT is defined
    if (typeName == "tess_eval") return GL_TESS_EVALUATION_SHADER_EXT; // Ensure ANGLE_TESS_EVALUATION_SHADER_EXT is defined
    return GL_NONE; // Indicate error or unknown
}

static json SerializeShaderVariable(const sh::ShaderVariable &var) {
    json jvar;
    jvar["name"] = var.name;
    jvar["mapped_name"] = var.mappedName;
    jvar["type_enum"] = var.type; // Consider mapping to string for readability
    jvar["precision_enum"] = var.precision; // Consider mapping to string

    // Add other relevant fields from sh::ShaderVariable
    // For example, check ANGLE's sh_structs.h for exact field names like:
    jvar["static_use"] = var.staticUse;
    jvar["active"] = var.active;
    if (var.location != -1) { // -1 often indicates no explicit location
        jvar["location"] = var.location;
    }
    if (var.binding != -1) { // For samplers, images, UBO members with binding
        jvar["binding"] = var.binding;
    }
    if (var.offset != -1) { // For UBO members with offset
        jvar["offset"] = var.offset;
    }
     jvar["is_row_major"] = var.isRowMajorLayout;


    if (!var.arraySizes.empty()) {
        jvar["array_sizes"] = var.arraySizes;
    }
    // Use structOrBlockName (structName is deprecated in some ANGLE versions)
    if (!var.structOrBlockName.empty()) {
        jvar["struct_or_block_name"] = var.structOrBlockName;
    }

    if (!var.fields.empty()) { // If the ShaderVariable itself is a struct
        json jfields = json::array();
        for (const auto& field : var.fields) {
            jfields.push_back(SerializeShaderVariable(field)); // Recursive call
        }
        jvar["fields"] = jfields;
    }
    return jvar;
}

// New helper function to serialize an sh::InterfaceBlock
// (also usable for UniformBlocks and ShaderStorageBufferBlocks as they are typedefs)
static json SerializeInterfaceBlock(const sh::InterfaceBlock& block) {
    json jBlock;
    jBlock["name"] = block.name;                 // e.g., "MyUniforms"
    jBlock["mapped_name"] = block.mappedName;
    if (!block.instanceName.empty()) {           // e.g., "myUniformsInstance" (if an instance is named)
        jBlock["instance_name"] = block.instanceName;
    }
    // arraySize for blocks (0 or 1 typically means not an array of blocks, or an implicit array of size 1)
    // Check sh_structs.h for how arraySize is used for blocks. If it's an array of blocks, this will be > 0.
    if (block.arraySize > 0) { // Or block.arraySize > 1 depending on convention
        jBlock["array_size"] = block.arraySize;
    }

    // Layout: sh::BlockLayoutType (enum class: kShared, kPacked, kStd140, kStd430)
    // You'll want to convert this enum to a string or use its integer value.
    // Example: static_cast<int>(block.layout)
    std::string layoutStr = "unknown";
    switch (block.layout) {
        case sh::BlockLayoutType::BLOCKLAYOUT_SHARED: layoutStr = "shared"; break;
        case sh::BlockLayoutType::BLOCKLAYOUT_PACKED: layoutStr = "packed"; break;
        case sh::BlockLayoutType::BLOCKLAYOUT_STD140: layoutStr = "std140"; break;
        case sh::BlockLayoutType::BLOCKLAYOUT_STD430: layoutStr = "std430"; break;
        default: break;
    }
    jBlock["layout"] = layoutStr;

    if (block.binding != -1) { // -1 means no explicit binding
        jBlock["binding"] = block.binding;
    }
    jBlock["static_use"] = block.staticUse;
    jBlock["active"] = block.active;
    jBlock["is_row_major_layout"] = block.isRowMajorLayout; // For matrix packing within the block

    // Serialize the member variables (fields) of the block
    json jfields = json::array();
    for (const auto& field_var : block.fields) {
        jfields.push_back(SerializeShaderVariable(field_var)); // Use the existing variable serializer
    }
    jBlock["fields"] = jfields;

    return jBlock;
}

// Updated definition for SerializeActiveVariablesToJson
static json SerializeActiveVariablesToJson(ShHandle compiler) {
    json jroot;

    // Lambda for processing lists of simple sh::ShaderVariable (attributes, varyings, standalone uniforms)
    auto process_shader_variable_list = [&](const std::string& key, const std::vector<sh::ShaderVariable>* vars_vec) {
        if (vars_vec != nullptr) { // Always check for nullptr from ANGLE API
            json jlist = json::array();
            for (const auto& var : *vars_vec) {
                jlist.push_back(SerializeShaderVariable(var));
            }
            jroot[key] = jlist;
        } else {
            jroot[key] = json::array(); // Represent as empty list if ANGLE returns null
        }
    };

    // Lambda for processing lists of sh::InterfaceBlock (used for UBOs, SSBOs)
    auto process_interface_block_list = [&](const std::string& key, const std::vector<sh::InterfaceBlock>* blocks_vec) {
        if (blocks_vec != nullptr) { // Always check for nullptr
            json jlist = json::array();
            for (const auto& block : *blocks_vec) {
                jlist.push_back(SerializeInterfaceBlock(block));
            }
            jroot[key] = jlist;
        } else {
            jroot[key] = json::array();
        }
    };

    process_shader_variable_list("attributes", sh::GetAttributes(compiler));
    process_shader_variable_list("input_varyings", sh::GetInputVaryings(compiler));
    process_shader_variable_list("output_varyings", sh::GetOutputVaryings(compiler));
    process_shader_variable_list("output_variables", sh::GetOutputVariables(compiler)); // For fragment shader outputs
    process_shader_variable_list("uniforms", sh::GetUniforms(compiler)); // These are standalone uniforms (samplers, basic types not in blocks)

    // Uniform Blocks (UBOs) are sh::InterfaceBlock
    process_interface_block_list("uniform_blocks", sh::GetUniformBlocks(compiler));

    // Shader Storage Buffer Blocks (SSBOs) are also sh::InterfaceBlock
    process_interface_block_list("shader_storage_buffer_blocks", sh::GetShaderStorageBlocks(compiler));

    // sh::GetInterfaceBlocks(compiler) can sometimes be a more generic list or include
    // other types of interface blocks if ANGLE's API distinguishes them further.
    // If it's redundant with UBOs/SSBOs and they are already covered, you might not need this,
    // or you might want to see what it contains. For now, let's include it.
    // If it's the same underlying type (sh::InterfaceBlock), this lambda is appropriate.
    process_interface_block_list("generic_interface_blocks", sh::GetInterfaceBlocks(compiler));


    // Example: If ANGLE provides separate atomic counter buffer info (if not already in uniforms)
    // const std::vector<sh::AtomicCounterBuffer>* atomic_counters = sh::GetAtomicCounterBuffers(compiler);
    // if (atomic_counters) { ... serialize them ... }


    return jroot;
}

// Helper to create a JSON RPC error object for our return values
// This is NOT the full response, just the "error" part.
static json make_json_error_payload(int code, const std::string& message, const json& data = nullptr) {
    json error_payload;
    error_payload["code"] = code;
    error_payload["message"] = message;
    if (data != nullptr && !data.is_null()) { // Check for null before adding
        error_payload["data"] = data;
    }
    return error_payload;
}

// Modified handle_translate_request
// Returns:
// - On success: a json object representing the "result" field of the JSON-RPC response.
// - On failure: a json object representing the "error" field (with "code", "message").
json handle_translate_request(const json& params) {
    ShCompileOptions compileOptions = {};
    ShBuiltInResources resources;
    GenerateResources(&resources); // Initialize with defaults

    sh::GLenum shaderType = GL_NONE;
    ShShaderSpec spec = SH_GLES2_SPEC;
    ShShaderOutput output = SH_ESSL_OUTPUT;
    std::string shader_source_decoded;
    bool print_active_vars = false;

    // --- Parameter Extraction and Validation ---

    // 1. Shader Code Base64
    if (!params.contains("shader_code_base64")) {
        return make_json_error_payload(EFailJSONRPCInvalidParams, "Missing 'shader_code_base64' parameter.");
    }
    if (!params["shader_code_base64"].is_string()) {
        return make_json_error_payload(EFailJSONRPCInvalidParams, "'shader_code_base64' parameter must be a string.");
    }
    std::string shader_source_base64_str = params["shader_code_base64"].get<std::string>();
    shader_source_decoded = base64_decode_to_string(shader_source_base64_str);
    if (shader_source_decoded.empty() && !shader_source_base64_str.empty()) {
        return make_json_error_payload(EFailJSONRPCInvalidParams, "Failed to decode 'shader_code_base64'.");
    }

    // 2. Shader Type
    if (!params.contains("shader_type")) {
        return make_json_error_payload(EFailJSONRPCInvalidParams, "Missing 'shader_type' parameter.");
    }
    if (!params["shader_type"].is_string()) {
        return make_json_error_payload(EFailJSONRPCInvalidParams, "'shader_type' parameter must be a string.");
    }
    std::string shader_type_str = params["shader_type"].get<std::string>();
    shaderType = FindShaderTypeFromJson(shader_type_str);
    if (shaderType == GL_NONE) {
        return make_json_error_payload(EFailJSONRPCInvalidParams, "Unsupported 'shader_type': " + shader_type_str);
    }

    // 3. Spec (Optional, defaults to GLES2_SPEC)
    if (params.contains("spec")) {
        if (!params["spec"].is_string()) {
            return make_json_error_payload(EFailJSONRPCInvalidParams, "'spec' parameter must be a string.");
        }
        std::string spec_str = params["spec"].get<std::string>();
        if (spec_str == "gles2") spec = SH_GLES2_SPEC;
        else if (spec_str == "gles3") spec = SH_GLES3_SPEC;
        else if (spec_str == "gles31") spec = SH_GLES3_1_SPEC;
        else if (spec_str == "gles32") spec = SH_GLES3_2_SPEC;
        else if (spec_str == "webgl") spec = SH_WEBGL_SPEC;
        else if (spec_str == "webgl2") spec = SH_WEBGL2_SPEC;
        else if (spec_str == "webgl3") spec = SH_WEBGL3_SPEC;
        else if (spec_str == "webgln") { // WebGL 1.0 no highp
            spec = SH_WEBGL_SPEC;
            resources.FragmentPrecisionHigh = 0;
        } else {
            return make_json_error_payload(EFailJSONRPCInvalidParams, "Unsupported 'spec': " + spec_str);
        }
    }

    // Update resources for GLES3/WebGL2 and higher specs, mimicking original main()
    if (spec != SH_GLES2_SPEC && spec != SH_WEBGL_SPEC) {
        resources.MaxVertexTextureImageUnits = 16;
        resources.MaxCombinedTextureImageUnits = 32;
        resources.MaxTextureImageUnits = 16;
        resources.MaxDrawBuffers = 4;
    }
    // WebGL2/GLES3 have even higher minimums for some resources.
    if (spec == SH_WEBGL2_SPEC || spec == SH_GLES3_SPEC || spec == SH_GLES3_1_SPEC || spec == SH_GLES3_2_SPEC) {
        resources.MaxDrawBuffers = 8;
    }

    // 4. Output (Optional, defaults to ESSL_OUTPUT)
    if (params.contains("output")) {
        if (!params["output"].is_string()) {
            return make_json_error_payload(EFailJSONRPCInvalidParams, "'output' parameter must be a string.");
        }
        std::string output_str_full = params["output"].get<std::string>();
        std::string output_type_str;
        std::string output_version_str;

        // Simplified parsing logic (adapt from previous, more detailed version)
        if (output_str_full.rfind("glsl", 0) == 0 && output_str_full.length() > 4) {
            output_type_str = "glsl";
            output_version_str = output_str_full.substr(4);
        } else if (output_str_full.rfind("hlsl", 0) == 0 && output_str_full.length() > 4) {
            output_type_str = "hlsl";
            output_version_str = output_str_full.substr(4);
        } else {
            output_type_str = output_str_full;
        }

        if (output_type_str == "essl") output = SH_ESSL_OUTPUT;
        else if (output_type_str == "glsl") {
            if (!ParseGLSLOutputVersion(output_version_str, &output)) { // Ensure ParseGLSLOutputVersion doesn't throw
                return make_json_error_payload(EFailJSONRPCInvalidParams, "Unsupported 'output' GLSL version: " + output_version_str);
            }
        } else if (output_type_str == "spirv") output = SH_SPIRV_VULKAN_OUTPUT;
        else if (output_type_str == "hlsl") {
            if (output_version_str == "9") output = SH_HLSL_3_0_OUTPUT;
            else if (output_version_str == "11") output = SH_HLSL_4_1_OUTPUT;
            else return make_json_error_payload(EFailJSONRPCInvalidParams, "Unsupported 'output' HLSL version: " + output_version_str);
        } else if (output_type_str == "msl") output = SH_MSL_METAL_OUTPUT;
        else return make_json_error_payload(EFailJSONRPCInvalidParams, "Unsupported 'output' type: " + output_type_str);
    }
    
    // 5. Compile Options (Optional)
    if (params.contains("compile_options")) {
        if (!params["compile_options"].is_object()) {
            return make_json_error_payload(EFailJSONRPCInvalidParams, "'compile_options' must be an object.");
        }
        const auto& co = params["compile_options"];
        compileOptions.intermediateTree = co.value("intermediate_tree", false); // .value is safe
        compileOptions.objectCode = co.value("object_code", true);
        compileOptions.initializeUninitializedLocals = co.value("initialize_uninitialized_locals", true);
        compileOptions.initializeBuiltinsForInstancedMultiview = co.value("initialize_builtins_for_instanced_multiview", false);
        compileOptions.selectViewInNvGLSLVertexShader = co.value("select_view_in_nv_glsl_vertex_shader", false);
    } else { // Default if not provided
         compileOptions.objectCode = true;
         compileOptions.initializeUninitializedLocals = true;
    }

    // 6. Resources (Optional)
    if (params.contains("resources")) {
        if (!params["resources"].is_object()) {
            return make_json_error_payload(EFailJSONRPCInvalidParams, "'resources' must be an object.");
        }
        const auto& res_params = params["resources"];
        
        // Handle 'EnableNameHashing' specifically
        if (res_params.contains("EnableNameHashing")) {
            if (!res_params["EnableNameHashing"].is_boolean()) {
                return make_json_error_payload(EFailJSONRPCInvalidParams, "resources.EnableNameHashing must be a boolean.");
            }
            // If EnableNameHashing is true, set a non-nullptr hash function to disable ANGLE's default _u prefix.
            // If false, leave it as nullptr (default from GenerateResources), which enables ANGLE's _u prefix.
            if (res_params["EnableNameHashing"].get<bool>()) {
                resources.HashFunction = FNVHashFunction; // Use our custom hash function to disable ANGLE's _u prefixing
            } else {
                resources.HashFunction = nullptr; // Explicitly revert to default behavior for _u prefixing
            }
        }
        // else: If "EnableNameHashing" is not present, resources.HashFunction remains nullptr from GenerateResources(),
        //       meaning ANGLE's default _u prefixing will occur.

        // Example: MaxVertexAttribs
        if (res_params.contains("MaxVertexAttribs")) {
            if (!res_params["MaxVertexAttribs"].is_number_integer()) {
                 return make_json_error_payload(EFailJSONRPCInvalidParams, "resources.MaxVertexAttribs must be an integer.");
            }
            resources.MaxVertexAttribs = res_params["MaxVertexAttribs"].get<int>();
        }
        // Example: OES_EGL_image_external (boolean represented as 0 or 1 int)
        if (res_params.contains("OES_EGL_image_external")) {
            if (!res_params["OES_EGL_image_external"].is_number_integer()) {
                 return make_json_error_payload(EFailJSONRPCInvalidParams, "resources.OES_EGL_image_external must be an integer (0 or 1).");
            }
            resources.OES_EGL_image_external = res_params["OES_EGL_image_external"].get<int>();
        }
    }
    // Adjust resources based on spec (mirroring original logic more carefully)
    if (spec != SH_GLES2_SPEC && spec != SH_WEBGL_SPEC) {
        bool resources_overridden = params.contains("resources") && params["resources"].is_object();
        if (!resources_overridden || !params["resources"].contains("MaxDrawBuffers")) {
            resources.MaxDrawBuffers = 8;
        }
        // ... (similar for MaxVertexTextureImageUnits, MaxTextureImageUnits)
    }
    // Special case for -s=wn (webgln)
    if (spec == SH_WEBGL_SPEC && params.value("spec", "") == "webgln") {
        resources.FragmentPrecisionHigh = 0;
    } else if (spec == SH_WEBGL_SPEC) { // Default WebGL1.0 spec implies FragmentPrecisionHigh = 1
        bool resources_overridden = params.contains("resources") && params["resources"].is_object();
        if (!resources_overridden || !params["resources"].contains("FragmentPrecisionHigh")) {
            resources.FragmentPrecisionHigh = 1;
        }
    }

    // if (params.contains("resources")) {
    //     if (!params["resources"].is_object()) {
    //         return make_json_error_payload(EFailJSONRPCInvalidParams, "'resources' must be an object.");
    //     }
    //     const auto& res_params = params["resources"];
    //     // Example: MaxVertexAttribs
    //     if (res_params.contains("MaxVertexAttribs")) {
    //         if (!res_params["MaxVertexAttribs"].is_number_integer()) {
    //              return make_json_error_payload(EFailJSONRPCInvalidParams, "resources.MaxVertexAttribs must be an integer.");
    //         }
    //         resources.MaxVertexAttribs = res_params["MaxVertexAttribs"].get<int>();
    //     }
    //     // Example: OES_EGL_image_external (boolean represented as 0 or 1 int)
    //     if (res_params.contains("OES_EGL_image_external")) {
    //         if (!res_params["OES_EGL_image_external"].is_number_integer()) {
    //              return make_json_error_payload(EFailJSONRPCInvalidParams, "resources.OES_EGL_image_external must be an integer (0 or 1).");
    //         }
    //         resources.OES_EGL_image_external = res_params["OES_EGL_image_external"].get<int>();
    //     }
    // }
    // // Adjust resources based on spec (mirroring original logic more carefully)
    // if (spec != SH_GLES2_SPEC && spec != SH_WEBGL_SPEC) {
    //     bool resources_overridden = params.contains("resources") && params["resources"].is_object();
    //     if (!resources_overridden || !params["resources"].contains("MaxDrawBuffers")) {
    //          resources.MaxDrawBuffers = 8;
    //     }
    //     // ... (similar for MaxVertexTextureImageUnits, MaxTextureImageUnits)
    // }
    // // Special case for -s=wn (webgln)
    // if (spec == SH_WEBGL_SPEC && params.value("spec", "") == "webgln") {
    //     resources.FragmentPrecisionHigh = 0;
    // } else if (spec == SH_WEBGL_SPEC) { // Default WebGL1.0 spec implies FragmentPrecisionHigh = 1
    //     bool resources_overridden = params.contains("resources") && params["resources"].is_object();
    //     if (!resources_overridden || !params["resources"].contains("FragmentPrecisionHigh")) {
    //          resources.FragmentPrecisionHigh = 1;
    //     }
    // }


    // 7. print_active_variables (Optional)
    if (params.contains("print_active_variables")) {
        if(!params["print_active_variables"].is_boolean()){
            return make_json_error_payload(EFailJSONRPCInvalidParams, "'print_active_variables' must be a boolean.");
        }
        print_active_vars = params["print_active_variables"].get<bool>();
    }


    // --- Perform Compilation ---
    ShHandle compiler = sh::ConstructCompiler(shaderType, spec, output, &resources);
    int cr = (int)compiler;
    if (!compiler) {
        return make_json_error_payload(EFailCompilerCreate, "Failed to construct compiler.");
    }

    const char* shader_strings[] = { shader_source_decoded.c_str() };
    bool compile_success = sh::Compile(compiler, shader_strings, 1, compileOptions);

    json result_payload; // This is the "result" field on success
    result_payload["info_log"] = sh::GetInfoLog(compiler);

    if (compile_success) {
        if (compileOptions.objectCode) {
            // Correctly handle binary vs. text output
            if (output == SH_SPIRV_VULKAN_OUTPUT)
            {
                // For binary output, base64 encode it
                const sh::BinaryBlob& blob = sh::GetObjectBinaryBlob(compiler);
                result_payload["object_code_base64"] = (blob.data() && blob.size() > 0) ?
                    base64_encode(reinterpret_cast<const unsigned char*>(blob.data()), (unsigned int)blob.size()) : "";
            }
            else
            {
                // For text output (ESSL, GLSL, HLSL), return the string directly
                result_payload["object_code"] = sh::GetObjectCode(compiler);
            }
        }
        if (print_active_vars) {
            result_payload["active_variables"] = SerializeActiveVariablesToJson(compiler); // Ensure this doesn't throw
        }
        sh::Destruct(compiler);
        return result_payload; // Success!
    } else {
        // Compilation failed
        sh::Destruct(compiler);
        json error_data;
        error_data["info_log"] = result_payload["info_log"]; // Reuse info_log
        return make_json_error_payload(EFailCompile, "Shader compilation failed.", error_data);
    }
}

// If NUM_SOURCE_STRINGS is set to a value > 1, the input file data is
// broken into that many chunks. This will affect file/line numbering in
// the preprocessor.
const unsigned int NUM_SOURCE_STRINGS = 1;
typedef std::vector<char *> ShaderSource;
static bool ReadShaderSource(const char *fileName, ShaderSource &source);
static void FreeShaderSource(ShaderSource &source);

static bool ParseGLSLOutputVersion(const std::string &, ShShaderOutput *outResult);
static bool ParseIntValue(const std::string &, int emptyDefault, int *outValue);

static void PrintSpirv(const sh::BinaryBlob &blob);

//
// Set up the per compile resources
//
void GenerateResources(ShBuiltInResources *resources)
{
    sh::InitBuiltInResources(resources);

    resources->MaxVertexAttribs             = 8;
    resources->MaxVertexUniformVectors      = 128;
    resources->MaxVaryingVectors            = 8;
    resources->MaxVertexTextureImageUnits   = 0;
    resources->MaxCombinedTextureImageUnits = 8;
    resources->MaxTextureImageUnits         = 8;
    resources->MaxFragmentUniformVectors    = 16;
    resources->MaxDrawBuffers               = 1;
    resources->MaxDualSourceDrawBuffers     = 1;

    resources->OES_standard_derivatives  = 0;
    resources->OES_EGL_image_external    = 0;
    resources->EXT_geometry_shader       = 1;
    resources->ANGLE_texture_multisample = 0;
    resources->APPLE_clip_distance       = 0;
}

int main(int argc, char *argv[]) {
    sh::Initialize(); // Initialize ANGLE once at the start

    bool json_rpc_mode = false;
    // Check if the first argument is --json-rpc
    if (argc > 1 && argv[1] != nullptr && std::string(argv[1]) == "--json-rpc") {
        json_rpc_mode = true;
    }

    int main_return_code = ESuccess; // Default success

    if (json_rpc_mode) {
        // JSON RPC Mode Logic
        std::string line;
        // Ensure std::cout is not buffered in a way that prevents timely responses
        std::ios_base::sync_with_stdio(false); // Can sometimes help, but endl flushes cout
        std::cin.tie(nullptr);


        while (std::getline(std::cin, line)) {
            json request_json;
            json response_json_shell;
            response_json_shell["jsonrpc"] = "2.0";
            response_json_shell["id"] = nullptr; // Default

            request_json = json::parse(line, nullptr, false); // Non-throwing parse

            if (request_json.is_discarded()) {
                response_json_shell["error"] = make_json_error_payload(EFailJSONRPCParse, "Parse error: Invalid JSON format.");
            } else {
                if (request_json.contains("id")) {
                    response_json_shell["id"] = request_json["id"];
                }

                if (!request_json.contains("method") || !request_json["method"].is_string()) {
                    response_json_shell["error"] = make_json_error_payload(EFailJSONRPCInvalidRequest, "Invalid Request: 'method' is missing or not a string.");
                } else {
                    std::string method = request_json["method"].get<std::string>();

                    if (method == "translate") {
                        if (!request_json.contains("params") || !request_json["params"].is_object()) {
                            response_json_shell["error"] = make_json_error_payload(EFailJSONRPCInvalidParams, "Invalid Params: 'params' is missing or not an object for 'translate' method.");
                        } else {
                            json params = request_json["params"];
                            json result_or_error_payload = handle_translate_request(params);

                            if (result_or_error_payload.contains("code") && result_or_error_payload.contains("message") && result_or_error_payload.is_object()) {
                                response_json_shell["error"] = result_or_error_payload;
                            } else {
                                response_json_shell["result"] = result_or_error_payload;
                            }
                        }
                    } else if (method == "shutdown") {
                        response_json_shell["result"] = "Shutdown acknowledged.";
                        std::cout << response_json_shell.dump() << std::endl; // Ensure this flushes
                        goto finalize_and_exit_success; // Use goto for clean exit path
                    } else {
                        response_json_shell["error"] = make_json_error_payload(EFailJSONRPCMethodNotFound, "Method not found: " + method);
                    }
                }
            }
            // Ensure "result" is not present if "error" is present
            if (response_json_shell.contains("error") && response_json_shell.contains("result")) {
                response_json_shell.erase("result");
            }
            std::cout << response_json_shell.dump() << std::endl; // std::endl flushes
        }
        // If loop exits due to EOF on stdin
        main_return_code = ESuccess;

    } else {
        // Original Command Line Interface (CLI) Mode Logic
        // This section is a careful copy and adaptation of your original main function.

        printf("HOWDY! (CLI Mode)\n"); // From original example
        TFailCode cli_fail_code = ESuccess;

        ShCompileOptions cli_compile_options = {};
        int cli_num_compiles                 = 0;
        ShHandle cli_vertex_compiler         = 0;
        ShHandle cli_fragment_compiler       = 0;
        ShHandle cli_compute_compiler        = 0;
        ShHandle cli_geometry_compiler       = 0;
        ShHandle cli_tess_eval_compiler      = 0;
        ShHandle cli_tess_control_compiler   = 0;
        ShShaderSpec cli_spec               = SH_GLES2_SPEC;
        ShShaderOutput cli_output           = SH_ESSL_OUTPUT;

        ShBuiltInResources cli_resources;
        GenerateResources(&cli_resources); // Initialize CLI default resources (ensure GenerateResources is defined)

        bool cli_print_active_variables = false;

        // Argument parsing loop from original:
        // Original main did argc--, argv++ before this loop.
        // We adjust current_argc and current_argv to skip argv[0] (program name).
        int current_argc = argc - 1;
        char **current_argv = argv + 1;

        for (; (current_argc >= 1) && (cli_fail_code == ESuccess); current_argc--, current_argv++) {
            if ((*current_argv)[0] == '-') { // Argument is an option
                switch ((*current_argv)[1]) {
                    case 'i':
                        cli_compile_options.intermediateTree = true;
                        break;
                    case 'o':
                        cli_compile_options.objectCode = true;
                        break;
                    case 'u':
                        cli_print_active_variables = true;
                        break;
                    case 's': // Shader Spec: -s=[e2|e3|e31|e32|w|wn|w2|w3]
                        if ((*current_argv)[2] == '=') {
                            switch ((*current_argv)[3]) {
                                case 'e': // GLES
                                    if ((*current_argv)[4] == '3') {
                                        if ((*current_argv)[5] == '1') cli_spec = SH_GLES3_1_SPEC;
                                        else if ((*current_argv)[5] == '2') cli_spec = SH_GLES3_2_SPEC;
                                        else cli_spec = SH_GLES3_SPEC;
                                    } else { // e.g. -s=e or -s=e2
                                        cli_spec = SH_GLES2_SPEC;
                                    }
                                    break;
                                case 'w': // WebGL
                                    if ((*current_argv)[4] == '3') cli_spec = SH_WEBGL3_SPEC;
                                    else if ((*current_argv)[4] == '2') cli_spec = SH_WEBGL2_SPEC;
                                    else if ((*current_argv)[4] == 'n') { // WebGL 1.0 no highp
                                        cli_spec = SH_WEBGL_SPEC;
                                        cli_resources.FragmentPrecisionHigh = 0;
                                    } else { // WebGL 1.0 default
                                        cli_spec = SH_WEBGL_SPEC;
                                        cli_resources.FragmentPrecisionHigh = 1;
                                    }
                                    break;
                                default: cli_fail_code = EFailUsage; break;
                            }
                        } else { cli_fail_code = EFailUsage; }
                        break;
                    case 'b': // Output backend: -b=[e|g[NUM]|v|h9|h11|m]
                        if ((*current_argv)[2] == '=') {
                            cli_compile_options.initializeUninitializedLocals = true; // Common for most backends
                            switch ((*current_argv)[3]) {
                                case 'e': cli_output = SH_ESSL_OUTPUT; break;
                                case 'g':
                                    // Pass the string part after "-b=g" to ParseGLSLOutputVersion
                                    if (!ParseGLSLOutputVersion(&(*current_argv)[sizeof("-b=g") - 1], &cli_output)) {
                                        cli_fail_code = EFailUsage;
                                    }
                                    break;
                                case 'v': cli_output = SH_SPIRV_VULKAN_OUTPUT; break;
                                case 'h':
                                    if ((*current_argv)[4] == '1' && (*current_argv)[5] == '1') cli_output = SH_HLSL_4_1_OUTPUT;
                                    else cli_output = SH_HLSL_3_0_OUTPUT; // Default -b=h or -b=h9
                                    break;
                                case 'm': cli_output = SH_MSL_METAL_OUTPUT; break;
                                default: cli_fail_code = EFailUsage; break;
                            }
                        } else { cli_fail_code = EFailUsage; }
                        break;
                    case 'x': // Extensions: -x=[i|d|r|b[NUM]|w[NUM]|g|l|f|n|a|m|y|s]
                        if ((*current_argv)[2] == '=') {
                            switch ((*current_argv)[3]) {
                                case 'i': cli_resources.OES_EGL_image_external = 1; break;
                                case 'd': cli_resources.OES_standard_derivatives = 1; break;
                                case 'r': cli_resources.ARB_texture_rectangle = 1; break;
                                case 'b': // EXT_blend_func_extended
                                    if (ParseIntValue(&(*current_argv)[sizeof("-x=b") - 1], 1, &cli_resources.MaxDualSourceDrawBuffers)) {
                                        cli_resources.EXT_blend_func_extended = 1;
                                    } else { cli_fail_code = EFailUsage; }
                                    break;
                                case 'w': // EXT_draw_buffers
                                    if (ParseIntValue(&(*current_argv)[sizeof("-x=w") - 1], 1, &cli_resources.MaxDrawBuffers)) {
                                        cli_resources.EXT_draw_buffers = 1;
                                    } else { cli_fail_code = EFailUsage; }
                                    break;
                                case 'g': cli_resources.EXT_frag_depth = 1; break;
                                case 'l': cli_resources.EXT_shader_texture_lod = 1; break;
                                case 'f': cli_resources.EXT_shader_framebuffer_fetch = 1; break;
                                case 'n': cli_resources.NV_shader_framebuffer_fetch = 1; break;
                                case 'a': cli_resources.ARM_shader_framebuffer_fetch = 1; break;
                                case 'm': // OVR_multiview
                                    cli_resources.OVR_multiview2 = 1; cli_resources.OVR_multiview = 1;
                                    cli_compile_options.initializeBuiltinsForInstancedMultiview = true;
                                    cli_compile_options.selectViewInNvGLSLVertexShader = true; // This was in original code
                                    break;
                                case 'y': cli_resources.EXT_YUV_target = 1; break;
                                case 's': cli_resources.OES_sample_variables = 1; break;
                                default: cli_fail_code = EFailUsage; break;
                            }
                        } else { cli_fail_code = EFailUsage; }
                        break;
                    default:
                        cli_fail_code = EFailUsage; break;
                }
            } else { // Argument is a filename
                // Update resources dynamically based on spec (original logic)
                if (cli_spec != SH_GLES2_SPEC && cli_spec != SH_WEBGL_SPEC) {
                    cli_resources.MaxDrawBuffers             = 8;
                    cli_resources.MaxVertexTextureImageUnits = 16;
                    cli_resources.MaxTextureImageUnits       = 16;
                }

                ShHandle* current_active_compiler_handle_ptr = nullptr;
                sh::GLenum resolved_shader_type = FindShaderType(*current_argv); // Original FindShaderType

                switch (resolved_shader_type) {
                    case GL_VERTEX_SHADER:
                        if (!cli_vertex_compiler) cli_vertex_compiler = sh::ConstructCompiler(GL_VERTEX_SHADER, cli_spec, cli_output, &cli_resources);
                        current_active_compiler_handle_ptr = &cli_vertex_compiler;
                        break;
                    case GL_FRAGMENT_SHADER:
                        if (!cli_fragment_compiler) cli_fragment_compiler = sh::ConstructCompiler(GL_FRAGMENT_SHADER, cli_spec, cli_output, &cli_resources);
                        current_active_compiler_handle_ptr = &cli_fragment_compiler;
                        break;
                    case GL_COMPUTE_SHADER:
                        if (!cli_compute_compiler) cli_compute_compiler = sh::ConstructCompiler(GL_COMPUTE_SHADER, cli_spec, cli_output, &cli_resources);
                        current_active_compiler_handle_ptr = &cli_compute_compiler;
                        break;
                    case GL_GEOMETRY_SHADER_EXT:
                        if (!cli_geometry_compiler) {
                            cli_resources.EXT_geometry_shader = 1; // Must be set before ConstructCompiler
                            cli_geometry_compiler = sh::ConstructCompiler(GL_GEOMETRY_SHADER_EXT, cli_spec, cli_output, &cli_resources);
                        }
                        current_active_compiler_handle_ptr = &cli_geometry_compiler;
                        break;
                    case GL_TESS_CONTROL_SHADER_EXT:
                        if (!cli_tess_control_compiler) {
                            cli_resources.EXT_tessellation_shader = 1; // Must be set
                            cli_tess_control_compiler = sh::ConstructCompiler(GL_TESS_CONTROL_SHADER_EXT, cli_spec, cli_output, &cli_resources);
                        }
                        current_active_compiler_handle_ptr = &cli_tess_control_compiler;
                        break;
                    case GL_TESS_EVALUATION_SHADER_EXT:
                        if (!cli_tess_eval_compiler) {
                            cli_resources.EXT_tessellation_shader = 1; // Must be set
                            cli_tess_eval_compiler = sh::ConstructCompiler(GL_TESS_EVALUATION_SHADER_EXT, cli_spec, cli_output, &cli_resources);
                        }
                        current_active_compiler_handle_ptr = &cli_tess_eval_compiler;
                        break;
                    default: // Should be caught by FindShaderType if it defaults or errors
                        cli_fail_code = EFailUsage; // Or EFailCompilerCreate
                        break;
                }

                if (current_active_compiler_handle_ptr && *current_active_compiler_handle_ptr) {
                    ShHandle active_compiler = *current_active_compiler_handle_ptr;

                    // Original logic for HLSL output options
                    if (cli_output == SH_HLSL_3_0_OUTPUT || cli_output == SH_HLSL_4_1_OUTPUT) {
                        cli_compile_options.selectViewInNvGLSLVertexShader = false;
                    } else if (cli_output != SH_ESSL_OUTPUT && cli_output != SH_GLSL_COMPATIBILITY_OUTPUT) {
                        // For non-ESSL/GLSLComp, selectViewInNvGLSLVertexShader might need to be true if OVR_multiview is enabled
                        // This was implicitly handled by -x=m setting it, ensure it's correct.
                        // The OVR_multiview option already sets `cli_compile_options.selectViewInNvGLSLVertexShader = true;`
                    }


                    bool compiled = CompileFile(*current_argv, active_compiler, cli_compile_options);

                    LogMsg("BEGIN", "COMPILER", cli_num_compiles, "INFO LOG");
                    std::string log_output = sh::GetInfoLog(active_compiler);
                    puts(log_output.c_str());
                    LogMsg("END", "COMPILER", cli_num_compiles, "INFO LOG");
                    printf("\n\n");

                    if (compiled && cli_compile_options.objectCode) {
                        LogMsg("BEGIN", "COMPILER", cli_num_compiles, "OBJ CODE");
                        if (cli_output != SH_SPIRV_VULKAN_OUTPUT) {
                            const std::string &code = sh::GetObjectCode(active_compiler);
                            puts(code.c_str());
                        } else {
                            const sh::BinaryBlob &blob = sh::GetObjectBinaryBlob(active_compiler);
                            PrintSpirv(blob); // Original PrintSpirv
                        }
                        LogMsg("END", "COMPILER", cli_num_compiles, "OBJ CODE");
                        printf("\n\n");
                    }
                    if (compiled && cli_print_active_variables) {
                        LogMsg("BEGIN", "COMPILER", cli_num_compiles, "VARIABLES");
                        PrintActiveVariables(active_compiler); // Original PrintActiveVariables
                        LogMsg("END", "COMPILER", cli_num_compiles, "VARIABLES");
                        printf("\n\n");
                    }
                    if (!compiled) {
                        cli_fail_code = EFailCompile;
                    }
                    ++cli_num_compiles;
                } else {
                    if (cli_fail_code == ESuccess) { // Only set if not already an error
                         cli_fail_code = EFailCompilerCreate;
                    }
                }
            }
        } // End of CLI argument parsing loop

        // Final checks for CLI mode from original main
        if (cli_num_compiles == 0 && cli_fail_code == ESuccess) {
            // No files were processed, and no errors occurred during option parsing.
            // This means no input files were provided.
            cli_fail_code = EFailUsage;
        }
        // The original check for all compilers being null is implicitly covered if numCompiles is 0
        // and no EFailCompilerCreate occurred for an attempted file.

        if (cli_fail_code == EFailUsage) {
            usage(); // Original usage function
        }

        // Destruct CLI mode compilers
        if (cli_vertex_compiler) sh::Destruct(cli_vertex_compiler);
        if (cli_fragment_compiler) sh::Destruct(cli_fragment_compiler);
        if (cli_compute_compiler) sh::Destruct(cli_compute_compiler);
        if (cli_geometry_compiler) sh::Destruct(cli_geometry_compiler);
        if (cli_tess_control_compiler) sh::Destruct(cli_tess_control_compiler);
        if (cli_tess_eval_compiler) sh::Destruct(cli_tess_eval_compiler);

        main_return_code = cli_fail_code;
    }

finalize_and_exit_success: // Label for successful exit path from JSON RPC shutdown
    if (json_rpc_mode && main_return_code != ESuccess && main_return_code == 0 /*check if it was a successful goto*/) {
        // This means we jumped here from JSON RPC shutdown.
        // Ensure main_return_code reflects success unless set otherwise by other logic.
        // If the goto was used, main_return_code is already ESuccess implicitly.
    }

    sh::Finalize(); // Finalize ANGLE once at the end
    return main_return_code;
}

//
//   print usage to stdout
//
void usage()
{
    // clang-format off
    printf(
        "Usage: translate [-i -o -u -l -b=e -b=g -b=h9 -x=i -x=d] file1 file2 ...\n"
        "Where: filename : filename ending in .frag*, .vert*, .comp*, .geom*, .tcs* or .tes*\n"
        "       -i       : print intermediate tree\n"
        "       -o       : print translated code\n"
        "       -u       : print active attribs, uniforms, varyings and program outputs\n"
        "       -s=e2    : use GLES2 spec (this is by default)\n"
        "       -s=e3    : use GLES3 spec\n"
        "       -s=e31   : use GLES31 spec (in development)\n"
        "       -s=e32   : use GLES32 spec (in development)\n"
        "       -s=w     : use WebGL 1.0 spec\n"
        "       -s=wn    : use WebGL 1.0 spec with no highp support in fragment shaders\n"
        "       -s=w2    : use WebGL 2.0 spec\n"
        "       -b=e     : output GLSL ES code (this is by default)\n"
        "       -b=g     : output GLSL code (compatibility profile)\n"
        "       -b=g[NUM]: output GLSL code (NUM can be 130, 140, 150, 330, 400, 410, 420, 430, "
        "440, 450)\n"
        "       -b=v     : output Vulkan SPIR-V code\n"
        "       -b=h9    : output HLSL9 code\n"
        "       -b=h11   : output HLSL11 code\n"
        "       -b=m     : output MSL code (direct)\n"
        "       -x=i     : enable GL_OES_EGL_image_external\n"
        "       -x=d     : enable GL_OES_EGL_standard_derivatives\n"
        "       -x=r     : enable ARB_texture_rectangle\n"
        "       -x=b[NUM]: enable EXT_blend_func_extended (NUM default 1)\n"
        "       -x=w[NUM]: enable EXT_draw_buffers (NUM default 1)\n"
        "       -x=g     : enable EXT_frag_depth\n"
        "       -x=l     : enable EXT_shader_texture_lod\n"
        "       -x=f     : enable EXT_shader_framebuffer_fetch\n"
        "       -x=n     : enable NV_shader_framebuffer_fetch\n"
        "       -x=a     : enable ARM_shader_framebuffer_fetch\n"
        "       -x=m     : enable OVR_multiview\n"
        "       -x=y     : enable YUV_target\n"
        "       -x=s     : enable OES_sample_variables\n"
        "       --json-rpc : run in JSON-RPC mode\n");
    // clang-format on
}

//
//   Deduce the shader type from the filename.  Files must end in one of the
//   following extensions:
//
//   .frag*    = fragment shader
//   .vert*    = vertex shader
//   .comp*    = compute shader
//   .geom*    = geometry shader
//   .tcs*     = tessellation control shader
//   .tes*     = tessellation evaluation shader
//
sh::GLenum FindShaderType(const char *fileName)
{
    assert(fileName);

    const char *ext = strrchr(fileName, '.');

    if (ext && strcmp(ext, ".sl") == 0)
        for (; ext > fileName && ext[0] != '.'; ext--)
            ;

    ext = strrchr(fileName, '.');
    if (ext)
    {
        if (strncmp(ext, ".frag", 5) == 0)
            return GL_FRAGMENT_SHADER;
        if (strncmp(ext, ".vert", 5) == 0)
            return GL_VERTEX_SHADER;
        if (strncmp(ext, ".comp", 5) == 0)
            return GL_COMPUTE_SHADER;
        if (strncmp(ext, ".geom", 5) == 0)
            return GL_GEOMETRY_SHADER_EXT;
        if (strncmp(ext, ".tcs", 5) == 0)
            return GL_TESS_CONTROL_SHADER_EXT;
        if (strncmp(ext, ".tes", 5) == 0)
            return GL_TESS_EVALUATION_SHADER_EXT;
    }

    return GL_FRAGMENT_SHADER;
}

//
//   Read a file's data into a string, and compile it using sh::Compile
//
bool CompileFile(char *fileName, ShHandle compiler, const ShCompileOptions &compileOptions)
{
    ShaderSource source;
    if (!ReadShaderSource(fileName, source))
        return false;

    int ret = sh::Compile(compiler, &source[0], source.size(), compileOptions);

    FreeShaderSource(source);
    return ret ? true : false;
}

void LogMsg(const char *msg, const char *name, const int num, const char *logName)
{
    printf("#### %s %s %d %s ####\n", msg, name, num, logName);
}

void PrintVariable(const std::string &prefix, size_t index, const sh::ShaderVariable &var)
{
    std::string typeName;
    switch (var.type)
    {
        case GL_FLOAT:
            typeName = "GL_FLOAT";
            break;
        case GL_FLOAT_VEC2:
            typeName = "GL_FLOAT_VEC2";
            break;
        case GL_FLOAT_VEC3:
            typeName = "GL_FLOAT_VEC3";
            break;
        case GL_FLOAT_VEC4:
            typeName = "GL_FLOAT_VEC4";
            break;
        case GL_INT:
            typeName = "GL_INT";
            break;
        case GL_INT_VEC2:
            typeName = "GL_INT_VEC2";
            break;
        case GL_INT_VEC3:
            typeName = "GL_INT_VEC3";
            break;
        case GL_INT_VEC4:
            typeName = "GL_INT_VEC4";
            break;
        case GL_UNSIGNED_INT:
            typeName = "GL_UNSIGNED_INT";
            break;
        case GL_UNSIGNED_INT_VEC2:
            typeName = "GL_UNSIGNED_INT_VEC2";
            break;
        case GL_UNSIGNED_INT_VEC3:
            typeName = "GL_UNSIGNED_INT_VEC3";
            break;
        case GL_UNSIGNED_INT_VEC4:
            typeName = "GL_UNSIGNED_INT_VEC4";
            break;
        case GL_BOOL:
            typeName = "GL_BOOL";
            break;
        case GL_BOOL_VEC2:
            typeName = "GL_BOOL_VEC2";
            break;
        case GL_BOOL_VEC3:
            typeName = "GL_BOOL_VEC3";
            break;
        case GL_BOOL_VEC4:
            typeName = "GL_BOOL_VEC4";
            break;
        case GL_FLOAT_MAT2:
            typeName = "GL_FLOAT_MAT2";
            break;
        case GL_FLOAT_MAT3:
            typeName = "GL_FLOAT_MAT3";
            break;
        case GL_FLOAT_MAT4:
            typeName = "GL_FLOAT_MAT4";
            break;
        case GL_FLOAT_MAT2x3:
            typeName = "GL_FLOAT_MAT2x3";
            break;
        case GL_FLOAT_MAT3x2:
            typeName = "GL_FLOAT_MAT3x2";
            break;
        case GL_FLOAT_MAT4x2:
            typeName = "GL_FLOAT_MAT4x2";
            break;
        case GL_FLOAT_MAT2x4:
            typeName = "GL_FLOAT_MAT2x4";
            break;
        case GL_FLOAT_MAT3x4:
            typeName = "GL_FLOAT_MAT3x4";
            break;
        case GL_FLOAT_MAT4x3:
            typeName = "GL_FLOAT_MAT4x3";
            break;

        case GL_SAMPLER_2D:
            typeName = "GL_SAMPLER_2D";
            break;
        case GL_SAMPLER_3D:
            typeName = "GL_SAMPLER_3D";
            break;
        case GL_SAMPLER_CUBE:
            typeName = "GL_SAMPLER_CUBE";
            break;
        case GL_SAMPLER_CUBE_SHADOW:
            typeName = "GL_SAMPLER_CUBE_SHADOW";
            break;
        case GL_SAMPLER_2D_SHADOW:
            typeName = "GL_SAMPLER_2D_ARRAY_SHADOW";
            break;
        case GL_SAMPLER_2D_ARRAY:
            typeName = "GL_SAMPLER_2D_ARRAY";
            break;
        case GL_SAMPLER_2D_ARRAY_SHADOW:
            typeName = "GL_SAMPLER_2D_ARRAY_SHADOW";
            break;
        case GL_SAMPLER_2D_MULTISAMPLE:
            typeName = "GL_SAMPLER_2D_MULTISAMPLE";
            break;
        case GL_IMAGE_2D:
            typeName = "GL_IMAGE_2D";
            break;
        case GL_IMAGE_3D:
            typeName = "GL_IMAGE_3D";
            break;
        case GL_IMAGE_CUBE:
            typeName = "GL_IMAGE_CUBE";
            break;
        case GL_IMAGE_2D_ARRAY:
            typeName = "GL_IMAGE_2D_ARRAY";
            break;

        case GL_INT_SAMPLER_2D:
            typeName = "GL_INT_SAMPLER_2D";
            break;
        case GL_INT_SAMPLER_3D:
            typeName = "GL_INT_SAMPLER_3D";
            break;
        case GL_INT_SAMPLER_CUBE:
            typeName = "GL_INT_SAMPLER_CUBE";
            break;
        case GL_INT_SAMPLER_2D_ARRAY:
            typeName = "GL_INT_SAMPLER_2D_ARRAY";
            break;
        case GL_INT_SAMPLER_2D_MULTISAMPLE:
            typeName = "GL_INT_SAMPLER_2D_MULTISAMPLE";
            break;
        case GL_INT_IMAGE_2D:
            typeName = "GL_INT_IMAGE_2D";
            break;
        case GL_INT_IMAGE_3D:
            typeName = "GL_INT_IMAGE_3D";
            break;
        case GL_INT_IMAGE_CUBE:
            typeName = "GL_INT_IMAGE_CUBE";
            break;
        case GL_INT_IMAGE_2D_ARRAY:
            typeName = "GL_INT_IMAGE_2D_ARRAY";
            break;

        case GL_UNSIGNED_INT_SAMPLER_2D:
            typeName = "GL_UNSIGNED_INT_SAMPLER_2D";
            break;
        case GL_UNSIGNED_INT_SAMPLER_3D:
            typeName = "GL_UNSIGNED_INT_SAMPLER_3D";
            break;
        case GL_UNSIGNED_INT_SAMPLER_CUBE:
            typeName = "GL_UNSIGNED_INT_SAMPLER_CUBE";
            break;
        case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
            typeName = "GL_UNSIGNED_INT_SAMPLER_2D_ARRAY";
            break;
        case GL_UNSIGNED_INT_ATOMIC_COUNTER:
            typeName = "GL_UNSIGNED_INT_ATOMIC_COUNTER";
            break;
        case GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE:
            typeName = "GL_UNSIGNED_INT_SAMPLER_2D_MULTISAMPLE";
            break;
        case GL_UNSIGNED_INT_IMAGE_2D:
            typeName = "GL_UNSIGNED_INT_IMAGE_2D";
            break;
        case GL_UNSIGNED_INT_IMAGE_3D:
            typeName = "GL_UNSIGNED_INT_IMAGE_3D";
            break;
        case GL_UNSIGNED_INT_IMAGE_CUBE:
            typeName = "GL_UNSIGNED_INT_IMAGE_CUBE";
            break;
        case GL_UNSIGNED_INT_IMAGE_2D_ARRAY:
            typeName = "GL_UNSIGNED_INT_IMAGE_2D_ARRAY";
            break;

        case GL_SAMPLER_EXTERNAL_OES:
            typeName = "GL_SAMPLER_EXTERNAL_OES";
            break;
        case GL_SAMPLER_EXTERNAL_2D_Y2Y_EXT:
            typeName = "GL_SAMPLER_EXTERNAL_2D_Y2Y_EXT";
            break;
        default:
            typeName = "UNKNOWN";
            break;
    }

    printf("%s %u : name=%s, mappedName=%s, type=%s, arraySizes=", prefix.c_str(),
           static_cast<unsigned int>(index), var.name.c_str(), var.mappedName.c_str(),
           typeName.c_str());
    for (unsigned int arraySize : var.arraySizes)
    {
        printf("%u ", arraySize);
    }
    printf("\n");
    if (var.fields.size())
    {
        std::string structPrefix;
        for (size_t i = 0; i < prefix.size(); ++i)
            structPrefix += ' ';
        printf("%s  struct %s\n", structPrefix.c_str(), var.structOrBlockName.c_str());
        structPrefix += "    field";
        for (size_t i = 0; i < var.fields.size(); ++i)
            PrintVariable(structPrefix, i, var.fields[i]);
    }
}

static void PrintActiveVariables(ShHandle compiler)
{
    const std::vector<sh::ShaderVariable> *uniforms       = sh::GetUniforms(compiler);
    const std::vector<sh::ShaderVariable> *inputVaryings  = sh::GetInputVaryings(compiler);
    const std::vector<sh::ShaderVariable> *outputVaryings = sh::GetOutputVaryings(compiler);
    const std::vector<sh::ShaderVariable> *attributes     = sh::GetAttributes(compiler);
    const std::vector<sh::ShaderVariable> *outputs        = sh::GetOutputVariables(compiler);
    for (size_t varCategory = 0; varCategory < 5; ++varCategory)
    {
        size_t numVars = 0;
        std::string varCategoryName;
        if (varCategory == 0)
        {
            numVars         = uniforms->size();
            varCategoryName = "uniform";
        }
        else if (varCategory == 1)
        {
            numVars         = inputVaryings->size();
            varCategoryName = "input varying";
        }
        else if (varCategory == 2)
        {
            numVars         = outputVaryings->size();
            varCategoryName = "output varying";
        }
        else if (varCategory == 3)
        {
            numVars         = attributes->size();
            varCategoryName = "attribute";
        }
        else
        {
            numVars         = outputs->size();
            varCategoryName = "output";
        }

        for (size_t i = 0; i < numVars; ++i)
        {
            const sh::ShaderVariable *var;
            if (varCategory == 0)
                var = &((*uniforms)[i]);
            else if (varCategory == 1)
                var = &((*inputVaryings)[i]);
            else if (varCategory == 2)
                var = &((*outputVaryings)[i]);
            else if (varCategory == 3)
                var = &((*attributes)[i]);
            else
                var = &((*outputs)[i]);

            PrintVariable(varCategoryName, i, *var);
        }
        printf("\n");
    }
}

static bool ReadShaderSource(const char *fileName, ShaderSource &source)
{
    FILE *in = fopen(fileName, "rb");
    if (!in)
    {
        printf("Error: unable to open input file: %s\n", fileName);
        return false;
    }

    // Obtain file size.
    fseek(in, 0, SEEK_END);
    size_t count = ftell(in);
    rewind(in);

    int len = (int)ceil((float)count / (float)NUM_SOURCE_STRINGS);
    source.reserve(NUM_SOURCE_STRINGS);
    // Notice the usage of do-while instead of a while loop here.
    // It is there to handle empty files in which case a single empty
    // string is added to vector.
    do
    {
        char *data   = new char[len + 1];
        size_t nread = fread(data, 1, len, in);
        data[nread]  = '\0';
        source.push_back(data);

        count -= nread;
    } while (count > 0);

    fclose(in);
    return true;
}

static void FreeShaderSource(ShaderSource &source)
{
    for (ShaderSource::size_type i = 0; i < source.size(); ++i)
    {
        delete[] source[i];
    }
    source.clear();
}

static bool ParseGLSLOutputVersion(const std::string &num, ShShaderOutput *outResult)
{
    if (num.length() == 0)
    {
        *outResult = SH_GLSL_COMPATIBILITY_OUTPUT;
        return true;
    }
    std::istringstream input(num);
    int value;
    if (!(input >> value && input.eof()))
    {
        return false;
    }

    switch (value)
    {
        case 130:
            *outResult = SH_GLSL_130_OUTPUT;
            return true;
        case 140:
            *outResult = SH_GLSL_140_OUTPUT;
            return true;
        case 150:
            *outResult = SH_GLSL_150_CORE_OUTPUT;
            return true;
        case 330:
            *outResult = SH_GLSL_330_CORE_OUTPUT;
            return true;
        case 400:
            *outResult = SH_GLSL_400_CORE_OUTPUT;
            return true;
        case 410:
            *outResult = SH_GLSL_410_CORE_OUTPUT;
            return true;
        case 420:
            *outResult = SH_GLSL_420_CORE_OUTPUT;
            return true;
        case 430:
            *outResult = SH_GLSL_430_CORE_OUTPUT;
            return true;
        case 440:
            *outResult = SH_GLSL_440_CORE_OUTPUT;
            return true;
        case 450:
            *outResult = SH_GLSL_450_CORE_OUTPUT;
            return true;
        default:
            break;
    }
    return false;
}

static bool ParseIntValue(const std::string &num, int emptyDefault, int *outValue)
{
    if (num.length() == 0)
    {
        *outValue = emptyDefault;
        return true;
    }

    std::istringstream input(num);
    int value;
    if (!(input >> value && input.eof()))
    {
        return false;
    }
    *outValue = value;
    return true;
}

static void PrintSpirv(const sh::BinaryBlob &blob)
{
#if defined(ANGLE_ENABLE_VULKAN)
    spvtools::SpirvTools spirvTools(SPV_ENV_VULKAN_1_1);

    std::string readableSpirv;
    spirvTools.Disassemble(blob, &readableSpirv,
                           SPV_BINARY_TO_TEXT_OPTION_COMMENT | SPV_BINARY_TO_TEXT_OPTION_INDENT |
                               SPV_BINARY_TO_TEXT_OPTION_NESTED_INDENT);

    puts(readableSpirv.c_str());
#endif
}

#if defined(__EMSCRIPTEN__)
// This manually provides a stub for a memory management function that
// Emscripten requires when memory growth is enabled.
extern "C" {
    void emscripten_notify_memory_growth(int memory_index) {}
}

#include <emscripten.h>

// This global string will hold the last result. It's a simple approach
// for a single-threaded WASM environment.
static std::string last_result_json;

extern "C"
{
    /**
     * @brief The main entry point for the WASM module.
     * * Takes a full JSON-RPC request as a string, processes it, and returns
     * the full JSON-RPC response as a string. The returned string pointer is
     * valid until the next call to invoke().
     * * @param request_json_str A C-string containing the JSON request.
     * @return A C-string containing the JSON response.
     */
    EMSCRIPTEN_KEEPALIVE
    const char *invoke(const char *request_json_str)
    {
        json request_json = json::parse(request_json_str, nullptr, false);
        json response_json_shell;
        response_json_shell["jsonrpc"] = "2.0";
        response_json_shell["id"] = nullptr;

        if (request_json.is_discarded())
        {
            response_json_shell["error"] = make_json_error_payload(EFailJSONRPCParse, "Parse error: Invalid JSON format.");
        }
        else
        {
            if (request_json.contains("id"))
            {
                response_json_shell["id"] = request_json["id"];
            }

            if (!request_json.contains("method") || !request_json["method"].is_string())
            {
                response_json_shell["error"] = make_json_error_payload(EFailJSONRPCInvalidRequest, "Invalid Request: 'method' is missing or not a string.");
            }
            else
            {
                std::string method = request_json["method"].get<std::string>();
                if (method == "translate")
                {
                    if (!request_json.contains("params") || !request_json["params"].is_object())
                    {
                        response_json_shell["error"] = make_json_error_payload(EFailJSONRPCInvalidParams, "Invalid Params: 'params' is missing or not an object for 'translate' method.");
                    }
                    else
                    {
                        json result_or_error_payload = handle_translate_request(request_json["params"]);
                        if (result_or_error_payload.contains("code") && result_or_error_payload.contains("message"))
                        {
                            response_json_shell["error"] = result_or_error_payload;
                        }
                        else
                        {
                            response_json_shell["result"] = result_or_error_payload;
                        }
                    }
                }
                else
                {
                    response_json_shell["error"] = make_json_error_payload(EFailJSONRPCMethodNotFound, "Method not found: " + method);
                }
            }
        }
        
        if (response_json_shell.contains("error") && response_json_shell.contains("result")) {
            response_json_shell.erase("result");
        }

        last_result_json = response_json_shell.dump();
        return last_result_json.c_str();
    }

    // Return an int to signal success/failure to Python
    int initialize() {
        if (sh::Initialize()) {
            return 1; // Success
        }
        return 0; // Failure
    }

    void finalize() {
        sh::Finalize();
    }
}
#endif