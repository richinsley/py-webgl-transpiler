# src/angle_translator/translator.py

import json
import base64
from wasmtime import Store, Module, Instance, Linker, Trap, Config, Engine, WasiConfig

try:
    from importlib.resources import files, as_file
except ImportError:
    from importlib_resources import files, as_file

class ShaderTranslator:
    """
    A Python wrapper for the ANGLE shader translator WASM module.
    This class provides both a context manager and an explicit .close() method
    for guaranteed, safe resource cleanup.
    """
    def __init__(self):
        self._closed = False  # Add a flag to track cleanup state
        
        config = Config()
        config.wasm_exceptions = True
        self.store = Store(Engine(config))

        # ... (The rest of __init__ is the same)
        wasi_config = WasiConfig()
        wasi_config.argv = []
        wasi_config.env = []
        self.store.set_wasi(wasi_config)
        linker = Linker(self.store.engine)
        linker.define_wasi()
        wasm_file_traversable = files('angle_translator').joinpath('wasm', 'angle_shader_translator_standalone.wasm')
        with as_file(wasm_file_traversable) as wasm_path:
            self.module = Module(self.store.engine, wasm_path.read_bytes())
        self.instance = linker.instantiate(self.store, self.module)
        self.exports = self.instance.exports(self.store)
        self.memory = self.exports["memory"]
        self._malloc = self.exports["malloc"]
        self._free = self.exports["free"]
        self._invoke = self.exports["invoke"]
        self._initialize = self.exports["initialize"]
        self._finalize = self.exports["finalize"]

        if not self._initialize(self.store):
             raise RuntimeError("CRITICAL: The ANGLE library failed to initialize.")

    def close(self):
            """
            Safely finalizes the ANGLE library and releases all wasmtime resources
            to ensure a clean shutdown.
            """
            if not self._closed:
                # Finalize the C++ ANGLE library first
                if hasattr(self, '_finalize') and self._finalize:
                    self._finalize(self.store)

                # Now, explicitly release our references to the wasmtime objects.
                # This allows them to be garbage collected immediately and safely.
                # The `del` keyword removes our class's reference to the object.
                if hasattr(self, 'instance'):
                    del self.instance
                if hasattr(self, 'module'):
                    del self.module
                if hasattr(self, 'store'):
                    del self.store
                
                # The engine is held by the store, but we can be extra sure.
                if hasattr(self, 'engine'):
                    del self.engine
                
                self._closed = True # Mark as closed to prevent re-entry

    def __del__(self):
        self.close()

    def __enter__(self):
        """Called when entering a 'with' block."""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Called when exiting a 'with' block, ensuring close() is called."""
        self.close()

    # All other methods (translate_shader, etc.) are unchanged.
    def translate_shader(self, shader_code: str, shader_type: str, spec: str = "webgl", output: str = "essl", print_vars: bool = True, enable_name_hashing: bool = True) -> dict:
        """
        Translates shader code using the ANGLE shader translator WASM module.

        This method sends a JSON-RPC request to the compiled WebAssembly module
        to perform shader compilation and translation based on the provided
        parameters.

        Args:
            shader_code (str): The GLSL shader code as a string (e.g., from a .vert or .frag file).
            shader_type (str): The type of shader. Valid options include:
                               - "vertex" (for GL_VERTEX_SHADER)
                               - "fragment" (for GL_FRAGMENT_SHADER)
                               - "compute" (for GL_COMPUTE_SHADER)
                               - "geometry" (for GL_GEOMETRY_SHADER_EXT)
                               - "tess_control" (for GL_TESS_CONTROL_SHADER_EXT)
                               - "tess_eval" (for GL_TESS_EVALUATION_SHADER_EXT)
            spec (str, optional): The shader specification to use. Defaults to "webgl".
                                  Other common options include:
                                  - "gles2", "gles3", "gles31", "gles32" (for GLES specs)
                                  - "webgl", "webgln", "webgl2", "webgl3" (for WebGL specs)
            output (str, optional): The desired output format for the translated shader.
                                    Defaults to "essl" (GLSL ES). Other options:
                                    - "essl" (GLSL ES)
                                    - "glsl" or "glsl[NUM]" (e.g., "glsl330" for GLSL 3.30 Core)
                                    - "spirv" (Vulkan SPIR-V not implemented yet)
                                    - "hlsl9", "hlsl11" (HLSL for DirectX 9 or 11 not implemented yet)
                                    - "msl" (Metal Shading Language not implemented yet)
            print_vars (bool, optional): If True, the response will include a detailed
                                         `active_variables` dictionary showing attributes,
                                         uniforms, varyings, and output variables. Defaults to True.
            enable_name_hashing (bool, optional): Controls whether ANGLE's internal name hashing
                                                  mechanism is active. Defaults to True.
                                                  - If `True`: ANGLE will *prevent* automatic
                                                    name mangling (like prepending `_u` to uniforms
                                                    when translating from WebGL to GLSL).
                                                  - If `False`: ANGLE's default behavior for
                                                    name mangling which often includes 
                                                    prefixing names (e.g., `_u_myUniform`).

        Returns:
            dict: A dictionary containing the translation result.
                  On success, this typically includes:
                  - 'info_log' (str): Compilation log messages (warnings, errors).
                  - 'object_code' (str): The translated shader code (for text outputs).
                  - 'object_code_base64' (str): Base64 encoded binary shader (for 'spirv' output).
                  - 'active_variables' (dict, if print_vars=True): Details of active shader variables.
                  
                  On failure (e.g., invalid parameters or compilation error), it will contain
                  an 'error' key with details. You should check for the presence of this key
                  in the returned dictionary to handle errors.

        Raises:
            RuntimeError: If the translator has been closed or if the WASM invocation
                          returns a null pointer.
            json.JSONDecodeError: If the response from WASM is not valid JSON.
        """
        if self._closed:
            raise RuntimeError("Translator has been closed and cannot be used.")
        shader_code_b64 = base64.b64encode(shader_code.encode('utf-8')).decode('utf-8')

        # Build the resources dictionary
        resources_params = {}
        # Add other resources as needed
        resources_params["EnableNameHashing"] = enable_name_hashing

        request_payload = {
            "jsonrpc": "2.0", "id": 1, "method": "translate",
            "params": {
                "shader_code_base64": shader_code_b64, 
                "shader_type": shader_type,
                "spec": spec, 
                "output": output, 
                "print_active_variables": print_vars,
                "compile_options": {"objectCode": True},
                "resources": resources_params,
            }
        }
        request_str = json.dumps(request_payload)
        request_ptr = 0
        try:
            request_ptr = self._write_string_to_memory(request_str)
            result_ptr = self._invoke(self.store, request_ptr)
            if not result_ptr:
                raise RuntimeError("WASM invoke function returned a null pointer.")
            response_str = self._read_string_from_memory(result_ptr)
        finally:
            if request_ptr:
                self._free(self.store, request_ptr)
        return json.loads(response_str)

    def _write_string_to_memory(self, s: str) -> int:
        s_bytes = s.encode('utf-8')
        ptr = self._malloc(self.store, len(s_bytes) + 1)
        if not ptr:
            raise MemoryError("WASM malloc failed to allocate memory.")
        self.memory.write(self.store, s_bytes, ptr)
        self.memory.write(self.store, b'\0', ptr + len(s_bytes))
        return ptr

    def _read_string_from_memory(self, ptr: int) -> str:
        mem_data = self.memory.read(self.store, ptr, self.memory.data_len(self.store) - ptr)
        null_term_pos = mem_data.find(b'\0')
        if null_term_pos == -1:
            raise ValueError("String from WASM is not null-terminated")
        return mem_data[:null_term_pos].decode('utf-8')