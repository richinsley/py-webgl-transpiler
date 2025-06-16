import pygame as pg
import moderngl
import struct
import time
import json
import base64
import re

# ==============================================================================
# 1. Include the ShaderTranslator class from our previous work
# ==============================================================================
from wasmtime import Store, Module, Instance, Linker, Trap, Config, Engine, WasiConfig

class ShaderTranslator:
    """
    A Python wrapper for the ANGLE shader translator WASM module.
    Handles memory management for passing strings to and from the WASM module.
    """
    def __init__(self, wasm_path: str):
        print(f"Initializing WASM runtime and loading module from: {wasm_path}")
        config = Config()
        config.wasm_exceptions = True
        self.store = Store(Engine(config))
        wasi_config = WasiConfig()
        wasi_config.argv = []
        wasi_config.env = []
        self.store.set_wasi(wasi_config)
        linker = Linker(self.store.engine)
        linker.define_wasi()
        self.module = Module(self.store.engine, open(wasm_path, 'rb').read())
        self.instance = linker.instantiate(self.store, self.module)
        self.exports = self.instance.exports(self.store)
        self._initialize = self.exports["initialize"]
        self._finalize = self.exports["finalize"]
        self.memory = self.exports["memory"]
        self._invoke = self.exports["invoke"]
        self._malloc = self.exports["malloc"]
        self._free = self.exports["free"]
        
        # Initialize the ANGLE library once
        success = self._initialize(self.store)
        if not success:
            raise RuntimeError("CRITICAL: The ANGLE library failed to initialize (sh::Initialize() returned false).")
        print("ANGLE library initialized successfully.")

    def finalize(self):
        print("Finalizing ANGLE library.")
        self._finalize(self.store)

    def _read_string_from_memory(self, ptr: int) -> str:
        memory_data = self.memory.read(self.store, ptr, self.memory.data_len(self.store) - ptr)
        null_terminator_pos = memory_data.find(b'\0')
        if null_terminator_pos == -1:
            raise ValueError("String from WASM is not null-terminated")
        return memory_data[:null_terminator_pos].decode('utf-8')

    def _write_string_to_memory(self, s: str) -> int:
        s_bytes = s.encode('utf-8')
        ptr = self._malloc(self.store, len(s_bytes) + 1)
        if not ptr:
            raise MemoryError("WASM malloc failed to allocate memory.")
        self.memory.write(self.store, s_bytes, ptr)
        self.memory.write(self.store, b'\0', ptr + len(s_bytes))
        return ptr

    def translate(self, shader_code: str, shader_type: str, spec: str, output: str) -> dict:
        shader_code_b64 = base64.b64encode(shader_code.encode('utf-8')).decode('utf-8')
        request_payload = {
            "jsonrpc": "2.0", "id": 1, "method": "translate",
            "params": {
                "shader_code_base64": shader_code_b64,
                "shader_type": shader_type,
                "spec": spec, "output": output,
                "print_active_variables": True,
                "compile_options": {"objectCode": True}
            }
        }
        request_str = json.dumps(request_payload)
        request_ptr = 0
        try:
            request_ptr = self._write_string_to_memory(request_str)
            result_ptr = self._invoke(self.store, request_ptr)
            if not result_ptr: raise RuntimeError("WASM invoke returned a null pointer.")
            response_str = self._read_string_from_memory(result_ptr)
        finally:
            if request_ptr: self._free(self.store, request_ptr)
        return json.loads(response_str)

# ==============================================================================
# 2. Create a single, global instance of the translator
# ==============================================================================
WASM_FILE_PATH = "./wasm_output/angle_shader_translator_standalone.wasm"
try:
    translator = ShaderTranslator(WASM_FILE_PATH)
except Exception as e:
    print(f"FATAL: Could not initialize the WASM translator: {e}")
    exit()

# ==============================================================================
# 3. Create a new translation function that uses the WASM module
#    This replaces `translate_shader_from_string` and `parse_angle_output`
# ==============================================================================
def translate_shader(shader_string: str, shader_type: str, spec: str, output: str) -> tuple[str | None, dict | None, str | None]:
    """
    Translates a shader using the WASM module.

    Returns:
        A tuple: (translated_code, reflection_data, error_string)
    """
    print(f"Translating {shader_type} shader from {spec} to {output}...")
    response = translator.translate(shader_string, shader_type, spec, output)

    if "error" in response:
        error_info = response["error"]
        error_message = f"ANGLE Error Code {error_info.get('code')}: {error_info.get('message')}\n"
        if "data" in error_info and "info_log" in error_info["data"]:
            error_message += error_info["data"]["info_log"]
        return None, None, error_message

    if "result" in response:
        result = response["result"]
        code = result.get("object_code", "")
        reflection = result.get("active_variables", {})
        return code, reflection, None
    
    return None, None, "Unknown error: Invalid JSON response from translator."

class ShaderToy:
    def __init__(self, width=1280, height=720):
        # ... (pygame and moderngl setup is the same) ...
        self.width = width
        self.height = height
        self.screen_size = (self.width, self.height)

        pg.init()
        pg.display.gl_set_attribute(pg.GL_CONTEXT_MAJOR_VERSION, 3)
        pg.display.gl_set_attribute(pg.GL_CONTEXT_MINOR_VERSION, 3)
        pg.display.gl_set_attribute(pg.GL_CONTEXT_PROFILE_MASK, pg.GL_CONTEXT_PROFILE_CORE)
        pg.display.gl_set_attribute(pg.GL_CONTEXT_FORWARD_COMPATIBLE_FLAG, True)
        pg.display.gl_set_attribute(pg.GL_DOUBLEBUFFER, 1)
        pg.display.gl_set_attribute(pg.GL_DEPTH_SIZE, 0)

        self.screen = pg.display.set_mode(self.screen_size, pg.OPENGL | pg.DOUBLEBUF)
        pg.display.set_caption("ModernGL Shadertoy with WASM ANGLE")
        self.ctx = moderngl.create_context()

        self.start_time = time.time()
        # ... (rest of the variable setup is the same) ...
        self.current_time = 0.0
        self.frame_count = 0
        self.mouse_pos = [0.0, 0.0, 0.0, 0.0]

        webgl_vertex_shader_source = """#version 300 es
            in vec2 in_vert;
            out vec2 v_frag_coord_uv; 
            void main() {
                gl_Position = vec4(in_vert, 0.0, 1.0);
                v_frag_coord_uv = in_vert * 0.5 + 0.5;
            }
        """

        shadertoy_core_logic = """
        void mainImage( out vec4 o, vec2 u )
        {
            // ... (Your creative shader code remains here) ...
            vec2 R=iResolution.xy, uv=(u+u-R)/R.y;
            o=mix(vec4(uv,.5+.5*cos(iTime),1),o,sin(length(uv)));
        }
        """

        gles_fragment_shader_string = f"""#version 300 es
            precision highp float;
            uniform vec3 iResolution;
            uniform float iTime;
            uniform vec4 iMouse;
            in vec2 v_frag_coord_uv; 
            out vec4 out_FragColor;
            {shadertoy_core_logic}
            void main() {{
                vec2 pixel_coords = v_frag_coord_uv * iResolution.xy;
                mainImage(out_FragColor, pixel_coords);
            }}
        """

        # ==============================================================================
        # 4. Use the new WASM translation function
        # ==============================================================================
        translated_vertex_shader, vs_reflection, vs_error = translate_shader(
            webgl_vertex_shader_source, "vertex", "webgl2", "glsl330"
        )
        if vs_error:
            print(f"Vertex Shader translation FAILED!\n{vs_error}")
            pg.quit()
            exit()
        print("--- Translated Vertex Shader ---\n", translated_vertex_shader)

        translated_fragment_shader, fs_reflection, fs_error = translate_shader(
            gles_fragment_shader_string, "fragment", "webgl2", "glsl330"
        )
        if fs_error:
            print(f"Fragment Shader translation FAILED!\n{fs_error}")
            pg.quit()
            exit()
        print("--- Translated Fragment Shader ---\n", translated_fragment_shader)

        try:
            self.program = self.ctx.program(
                vertex_shader=translated_vertex_shader,
                fragment_shader=translated_fragment_shader
            )
        except moderngl.Error as e:
            # ... (error handling is the same) ...
            pg.quit()
            exit()

        # ==============================================================================
        # 5. Use the reflection data to find uniforms automatically!
        # ==============================================================================
        self.uniforms = {}
        # We only care about uniforms from the fragment shader's reflection data
        print("\n--- Automatically Mapping Uniforms ---")
        if fs_reflection and 'uniforms' in fs_reflection:
            for uniform_data in fs_reflection['uniforms']:
                original_name = uniform_data['name']
                mapped_name = uniform_data['mapped_name']
                
                # The names we look for in python (iTime, etc.)
                uniform_key = original_name
                if uniform_key.startswith("i"): # a simple heuristic for iTime, iResolution etc
                    uniform = self.program.get(mapped_name, None)
                    if uniform:
                        self.uniforms[uniform_key] = uniform
                        print(f"Found and mapped uniform: '{uniform_key}' -> '{mapped_name}'")
                    else:
                        print(f"Warning: Uniform '{original_name}' (mapped to '{mapped_name}') not found in final program.")

        # Fullscreen quad VBO and VAO
        quad_vertices = [-1.0, -1.0, 1.0, -1.0, -1.0, 1.0, -1.0, 1.0, 1.0, -1.0, 1.0, 1.0]
        vertex_data = struct.pack(f'{len(quad_vertices)}f', *quad_vertices)
        self.vbo = self.ctx.buffer(vertex_data)
        
        # Find the vertex attribute name automatically from reflection data
        vs_in_attribute_name = "in_vert" # Default
        if vs_reflection and 'attributes' in vs_reflection:
             for attr in vs_reflection['attributes']:
                 if attr['name'] == 'in_vert':
                     vs_in_attribute_name = attr['mapped_name']
                     break
        print(f"Detected vertex shader attribute name: '{vs_in_attribute_name}'")

        self.vao = self.ctx.simple_vertex_array(self.program, self.vbo, vs_in_attribute_name)
        self.last_frame_time = self.start_time

    def render(self):
        # ... (render loop is the same) ...
        self.ctx.clear(0.0, 0.0, 0.0)
        new_time = time.time()
        self.current_time = new_time - self.start_time
        if "iResolution" in self.uniforms:
            self.uniforms["iResolution"].value = (self.width, self.height, 1.0)
        if "iTime" in self.uniforms:
            self.uniforms["iTime"].value = self.current_time
        if "iMouse" in self.uniforms:
            self.uniforms["iMouse"].value = tuple(self.mouse_pos)
        self.vao.render(moderngl.TRIANGLES)
        pg.display.flip()

    def run(self):
        # ... (run loop is the same) ...
        running = True
        while running:
            for event in pg.event.get():
                if event.type == pg.QUIT: running = False
                # Handle mouse input
            self.render()

# ==============================================================================
# 6. Main execution block with finalization
# ==============================================================================
if __name__ == '__main__':
    app = ShaderToy()
    try:
        app.run()
    except Exception as e:
        print(f"An error occurred during execution: {e}")
    finally:
        # Ensure the ANGLE library is cleaned up properly on exit
        translator.finalize()
        pg.quit()