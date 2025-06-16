# src/angle_translator/translator.py

import json
import base64
from wasmtime import Store, Module, Instance, Linker, Trap, Config, Engine, WasiConfig

# --- Compatibility for importlib.resources.files ---
# This block provides the modern `files()` function for Python 3.8+
# We now ALSO import the top-level as_file() function.
try:
    from importlib.resources import files, as_file
except ImportError:
    # Try to import the backport for Python < 3.9
    from importlib_resources import files, as_file

class ShaderTranslator:
    """
    A Python wrapper for the ANGLE shader translator WASM module.
    """
    def __init__(self):
        config = Config()
        config.wasm_exceptions = True
        self.store = Store(Engine(config))

        wasi_config = WasiConfig()
        wasi_config.argv = []
        wasi_config.env = []
        self.store.set_wasi(wasi_config)

        linker = Linker(self.store.engine)
        linker.define_wasi()

        # --- THE DEFINITIVE, CORRECTED WASM LOADING LOGIC ---
        try:
            wasm_file_traversable = files('angle_translator').joinpath('wasm', 'angle_shader_translator_standalone.wasm')
            
            # This is the key change. We use the top-level `as_file` function,
            # which correctly handles both regular files and files inside zips.
            with as_file(wasm_file_traversable) as wasm_path:
                # wasm_path is now guaranteed to be a concrete path object.
                self.module = Module(self.store.engine, wasm_path.read_bytes())

        except (ModuleNotFoundError, FileNotFoundError) as e:
            raise RuntimeError(
                "Could not find 'angle_shader_translator_standalone.wasm'. "
                "Ensure the package was installed correctly. "
                f"Original error: {e}"
            )

        self.instance = linker.instantiate(self.store, self.module)
        self.exports = self.instance.exports(self.store)
        
        self.memory = self.exports["memory"]
        self._initialize = self.exports["initialize"]
        self._invoke = self.exports["invoke"]
        self._malloc = self.exports["malloc"]
        self._free = self.exports["free"]

        success = self._initialize(self.store)
        if not success:
            raise RuntimeError("CRITICAL: The ANGLE library failed to initialize.")

    # ... The rest of the class is unchanged ...
    def _read_string_from_memory(self, ptr: int) -> str:
        mem_data = self.memory.read(self.store, ptr, self.memory.data_len(self.store) - ptr)
        null_term_pos = mem_data.find(b'\0')
        if null_term_pos == -1:
            raise ValueError("String from WASM is not null-terminated")
        return mem_data[:null_term_pos].decode('utf-8')

    def _write_string_to_memory(self, s: str) -> int:
        """Helper to write a Python string into WASM memory, returns the pointer."""
        s_bytes = s.encode('utf-8')
        
        # 1. Allocate memory in WASM (+1 for the null terminator)
        ptr = self._malloc(self.store, len(s_bytes) + 1)
        if not ptr:
            raise MemoryError("WASM malloc failed to allocate memory.")
            
        # 2. Write the string's bytes to the allocated memory
        #    Correct argument order: (store, data, pointer)
        self.memory.write(self.store, s_bytes, ptr)
        
        # 3. Write the null terminator at the end of the string
        #    Correct argument order: (store, data, pointer)
        self.memory.write(self.store, b'\0', ptr + len(s_bytes))
        
        return ptr


    def translate_shader(self, shader_code: str, shader_type: str, spec: str = "webgl", output: str = "essl", print_vars: bool = True) -> dict:
        shader_code_b64 = base64.b64encode(shader_code.encode('utf-8')).decode('utf-8')
        request_payload = {
            "jsonrpc": "2.0", "id": 1, "method": "translate",
            "params": {
                "shader_code_base64": shader_code_b64, "shader_type": shader_type,
                "spec": spec, "output": output, "print_active_variables": print_vars,
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