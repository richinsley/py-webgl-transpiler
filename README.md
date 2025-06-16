# Python WebGL ANGLE Shader Translator

A Python wrapper for the official ANGLE project's shader translator, compiled to WebAssembly. This package allows for fast, cross-platform translation of WebGL/GLES shaders into desktop GLSL, GLES shaders, and more.

## Features

-   Translate GLSL ES 1.00 (WebGL 1), GLSL ES 3.00 (WebGL 2), and GLSL ES 3.10/3.20 shaders.
-   Output to GLSL (Core/Compatibility), GLSL ES.
-   Runs on any platform where Python and Wasmtime are supported.
-   Extracts active uniforms, attributes, varyings, and other shader metadata.
-   No C++ compilation or external dependencies are required to *use* the package.

## Installation

```bash
pip install py-webgl-transpiler
```

## Usage
Here is a quick example of translating a WebGL 2 fragment shader to desktop GLSL.
```python
import base64
from angle_translator import WasmShaderTranslator

# 1. Initialize the translator
# This loads the WASM module into memory and initializes the transpiler.
translator = WasmShaderTranslator()

# 2. Define a shader to translate
webgl2_shader = """#version 300 es
    precision mediump float;
    in vec2 v_texCoord;
    out vec4 my_FragColor;

    void main() {
        my_FragColor = vec4(v_texCoord, 0.0, 1.0);
    }
"""

# 3. Call the translation function
response = translator.translate_shader(
    shader_code=webgl2_shader,
    shader_type="fragment",
    spec="webgl2",
    output="glsl" # Target desktop GLSL (compatibility profile by default)
)

# 4. Check the result
if "error" in response:
    print("Translation failed:")
    print(response["error"]["data"]["info_log"])
else:
    translated_code = response["result"]["object_code"]
    print("--- Translated GLSL ---")
    print(translated_code)

    # You can also get active variable info
    active_vars = response["result"]["active_variables"]
    print("\n--- Active Uniforms ---")
    print(active_vars.get("uniforms", []))
```

## Acknowledgements
This package is a wrapper around the fantastic work done by the ANGLE project authors. The core translation logic is entirely theirs. The original ANGLE license can be found within the project source.