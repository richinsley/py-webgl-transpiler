import json
import base64
# Add Config and Engine to the wasmtime imports
from wasmtime import Store, Module, Instance, Linker, Trap, Config, Engine, WasiConfig

class ShaderTranslator:
    """
    A Python wrapper for the ANGLE shader translator WASM module.
    Handles memory management for passing strings to and from the WASM module.
    """
    def __init__(self, wasm_path: str):
        print(f"Initializing WASM runtime and loading module from: {wasm_path}")
        
        # --- START OF THE FIX ---
        config = Config()
        config.wasm_exceptions = True # Keep this from our previous step
        self.store = Store(Engine(config))

        # Create a WasiConfig object and configure it.
        wasi_config = WasiConfig()
        wasi_config.argv = [] # We don't have any command-line args to pass
        wasi_config.env = []  # No environment variables
        self.store.set_wasi(wasi_config)

        # Create a linker and tell it to define the standard WASI functions.
        linker = Linker(self.store.engine)
        linker.define_wasi()
        # --- END OF THE FIX ---

        # Load the WebAssembly module
        self.module = Module(self.store.engine, open(wasm_path, 'rb').read())
        
        # Instantiate the module using the linker that now provides WASI.
        self.instance = linker.instantiate(self.store, self.module)
        
        self.exports = self.instance.exports(self.store)
        
        # Get the new initialize function and call it once.
        self._initialize = self.exports["initialize"]
        # Call initialize and check its return value
        success = self._initialize(self.store)
        if not success:
            raise RuntimeError("CRITICAL: The ANGLE library failed to initialize (sh::Initialize() returned false).")

        # Get the exported C functions and memory
        self.memory = self.exports["memory"]
        self._invoke = self.exports["invoke"]
        self._malloc = self.exports["malloc"]
        self._free = self.exports["free"]
        print("Module loaded and functions exported successfully.")

    # ... the rest of your ShaderTranslator class remains exactly the same ...
    def _read_string_from_memory(self, ptr: int) -> str:
        """Helper to read a null-terminated C-string from WASM memory."""
        # Read the raw bytes from the pointer onwards
        memory_data = self.memory.read(self.store, ptr, self.memory.data_len(self.store) - ptr)
        
        # Find the null terminator
        null_terminator_pos = memory_data.find(b'\0')
        if null_terminator_pos == -1:
            raise ValueError("String from WASM is not null-terminated")
            
        # Decode the bytes up to the terminator as a UTF-8 string
        return memory_data[:null_terminator_pos].decode('utf-8')

    def _write_string_to_memory(self, s: str) -> int:
        """Helper to write a Python string into WASM memory, returns the pointer."""
        s_bytes = s.encode('utf-8')
        
        # 1. Allocate memory in WASM (+1 for the null terminator)
        ptr = self._malloc(self.store, len(s_bytes) + 1)
        if not ptr:
            raise MemoryError("WASM malloc failed to allocate memory.")
            
        # 2. Write the string's bytes to the allocated memory
        self.memory.write(self.store, s_bytes, ptr)
        
        # 3. Write the null terminator at the end of the string
        self.memory.write(self.store, b'\0', ptr + len(s_bytes))
        
        return ptr

    def translate_shader(self, shader_code: str, shader_type: str, spec: str = "webgl", output: str = "essl", print_vars: bool = True) -> dict:
        """
        High-level method to translate a shader.
        
        Args:
            shader_code: The GLSL shader code as a string.
            shader_type: 'vertex' or 'fragment'.
            spec: 'webgl', 'gles2', 'gles3', etc.
            output: 'essl', 'glsl', 'spirv', etc.
            print_vars: Whether to request active variable information.
            
        Returns:
            A dictionary representing the JSON response from the translator.
        """
        shader_code_b64 = base64.b64encode(shader_code.encode('utf-8')).decode('utf-8')
        
        # Construct the JSON-RPC request payload
        request_payload = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "translate",
            "params": {
                "shader_code_base64": shader_code_b64,
                "shader_type": shader_type,
                "spec": spec,
                "output": output,
                "print_active_variables": print_vars,
                "compile_options": {
                    "objectCode": True
                }
            }
        }
        
        request_str = json.dumps(request_payload)
        request_ptr = 0
        
        try:
            # Write the request string into WASM memory
            request_ptr = self._write_string_to_memory(request_str)
            
            # Call the main 'invoke' function with the pointer
            result_ptr = self._invoke(self.store, request_ptr)
            
            if not result_ptr:
                raise RuntimeError("WASM invoke function returned a null pointer.")
            
            # Read the JSON response string back from WASM memory
            response_str = self._read_string_from_memory(result_ptr)
            
        finally:
            # CRITICAL: Always free the memory we allocated for the request string
            if request_ptr:
                self._free(self.store, request_ptr)

        return json.loads(response_str)


# --- Main execution ---
if __name__ == "__main__":
    # Path to your compiled WASM file
    # WASM_FILE_PATH = "./wasm_output/angle_shader_translator_standalone.wasm"
    WASM_FILE_PATH = "/home/rich/projects/arcana-nvenc-docker/test/wasm_output/angle_shader_translator_standalone2.wasm"

    translator = ShaderTranslator(WASM_FILE_PATH)

    # --- Example 1: A simple valid fragment shader ---
    fragment_shader = """
    precision mediump float;
    varying vec2 v_texCoord;
    uniform sampler2D u_sampler;

    void main() {
        gl_FragColor = texture2D(u_sampler, v_texCoord);
    }
    """
    
    print("\n--- Translating Fragment Shader to GLSL ES 3.0 (ESSL) ---")
    try:
        response = translator.translate_shader(
            shader_code=fragment_shader, 
            shader_type="fragment",
            spec="webgl2",
            output="essl"
        )
        print(json.dumps(response, indent=2))

        if 'result' in response and 'object_code_base64' in response['result']:
            translated_code_b64 = response['result']['object_code_base64']
            translated_code = base64.b64decode(translated_code_b64).decode('utf-8')
            print("\n--- Decoded Translated Shader ---")
            print(translated_code)
            
    except Trap as e:
        print(f"\n--- A WASM Trap Occurred (this could be an assertion failure in C++) ---\n{e}")
    except Exception as e:
        print(f"\n--- A Python Error Occurred ---\n{e}")


    # --- Example 2: A vertex shader with an error ---
    vertex_shader_with_error = """
    attribute vec4 a_position;
    varying vec4 v_color;

    void main() {
        gl_Position = a_position;
        v_color = a_position * undeclared_variable; // Error here
    }
    """
    
    print("\n\n--- Translating a Vertex Shader with an error ---")
    try:
        response = translator.translate_shader(
            shader_code=vertex_shader_with_error,
            shader_type="vertex"
        )
        print(json.dumps(response, indent=2))
        
        if 'error' in response and 'data' in response['error']:
            print("\n--- Extracted Info Log ---")
            print(response['error']['data']['info_log'])
            
    except Exception as e:
        print(f"\n--- An Error Occurred ---\n{e}")

    # --- Example 3: webgl ---
    # --- Example: WebGL 2 to OpenGL ES 3.0 ---
    webgl2_shader = """#version 300 es
        precision mediump float;
        in vec2 v_texCoord;
        out vec4 my_FragColor;
        void main() {
            my_FragColor = vec4(v_texCoord, 0.0, 1.0);
        }
    """

    response = translator.translate_shader(
        shader_code=webgl2_shader,
        shader_type="fragment",
        spec="webgl2",
        output="essl"
    )

    # The translated code will be in response['result']['object_code']
    print(response['result']['object_code'])