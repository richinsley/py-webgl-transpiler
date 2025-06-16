import json
import base64
from wasmtime import Store, Module, Instance, Linker, Trap, Config, Engine, WasiConfig
import importlib.resources

class WasmShaderTranslator:
    """
    A Python wrapper for the ANGLE shader translator WASM module.
    Handles loading the WASM module and managing memory for communication.
    """
    def __init__(self):
        """
        Initializes the translator by loading and configuring the WASM module.
        """
        # --- WASM Initialization ---
        config = Config()
        config.wasm_exceptions = True  # Allows WASM traps to be caught as Python exceptions
        self.store = Store(Engine(config))

        wasi_config = WasiConfig()
        wasi_config.argv = []
        wasi_config.env = []
        self.store.set_wasi(wasi_config)

        linker = Linker(self.store.engine)
        linker.define_wasi()

        # --- Load the WebAssembly module from package data ---
        # This is the key change for making the package distributable.
        # It finds the .wasm file inside the installed package.
        try:
            with importlib.resources.path('angle_translator.wasm', 'angle_shader_translator.wasm') as wasm_path:
                self.module = Module(self.store.engine, wasm_path.read_bytes())
        except FileNotFoundError:
            raise RuntimeError(
                "Could not find 'angle_shader_translator.wasm'. "
                "Ensure the package was installed correctly."
            )

        self.instance = linker.instantiate(self.store, self.module)
        self.exports = self.instance.exports(self.store)
        
        # --- Exported Functions & Memory ---
        self.memory = self.exports["memory"]
        self._initialize = self.exports["initialize"]
        self._invoke = self.exports["invoke"]
        self._malloc = self.exports["malloc"]
        self._free = self.exports["free"]

        # Call the one-time initialize function in the WASM module
        success = self._initialize(self.store)
        if not success:
            raise RuntimeError("CRITICAL: The ANGLE library failed to initialize (sh::Initialize() returned false).")

    def _read_string_from_memory(self, ptr: int) -> str:
        """Helper to read a null-terminated C-string from WASM memory."""
        mem_data = self.memory.read(self.store, ptr, self.memory.data_len(self.store) - ptr)
        null_term_pos = mem_data.find(b'\0')
        if null_term_pos == -1:
            raise ValueError("String from WASM is not null-terminated")
        return mem_data[:null_term_pos].decode('utf-8')

    def _write_string_to_memory(self, s: str) -> int:
        """Helper to write a Python string into WASM memory, returns the pointer."""
        s_bytes = s.encode('utf-8')
        ptr = self._malloc(self.store, len(s_bytes) + 1)
        if not ptr:
            raise MemoryError("WASM malloc failed to allocate memory.")
        self.memory.write(self.store, ptr, s_bytes)
        self.memory.write(self.store, ptr + len(s_bytes), b'\0')
        return ptr

    def translate_shader(self, shader_code: str, shader_type: str, spec: str = "webgl", output: str = "essl", print_vars: bool = True) -> dict:
        """
        High-level method to translate a shader.
        
        Args:
            shader_code: The GLSL shader code as a string.
            shader_type: 'vertex', 'fragment', 'compute', etc.
            spec: The input spec ('webgl', 'webgl2', 'gles2', 'gles3', etc.).
            output: The target output format ('essl', 'glsl', 'spirv', 'hlsl9', etc.).
            print_vars: Whether to request active variable information.
            
        Returns:
            A dictionary representing the JSON response from the translator.
        """
        shader_code_b64 = base64.b64encode(shader_code.encode('utf-8')).decode('utf-8')
        
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
                "compile_options": {"objectCode": True}
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