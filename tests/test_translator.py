import pytest
import base64
from angle_translator import ShaderTranslator

@pytest.fixture(scope="module")
def translator():
    """Provides a single ShaderTranslator instance for all tests."""
    return ShaderTranslator()

def test_successful_frag_translation(translator):
    """Tests a valid WebGL fragment shader translation to ESSL (GLSL ES)."""
    fragment_shader = """
    precision mediump float;
    varying vec2 v_texCoord;
    uniform sampler2D u_sampler;

    void main() {
        gl_FragColor = texture2D(u_sampler, v_texCoord);
    }
    """
    response = translator.translate_shader(
        shader_code=fragment_shader,
        shader_type="fragment",
        spec="webgl",
        output="essl"
    )
    assert "result" in response
    assert "object_code" in response["result"]
    assert "ERROR" not in response["result"]["info_log"]
    assert "v_texCoord" in response["result"]["object_code"]

def test_spirv_translation(translator):
    """Tests translation to SPIR-V, which should return base64 encoded binary."""
    vertex_shader = "void main() { gl_Position = vec4(1.0); }"
    response = translator.translate_shader(
        shader_code=vertex_shader,
        shader_type="vertex",
        spec="webgl",
        output="spirv"
    )
    assert "result" in response
    assert "object_code_base64" in response["result"]
    # Try decoding to ensure it's valid base64
    decoded = base64.b64decode(response["result"]["object_code_base64"])
    # SPIR-V binaries start with a magic number: 0x07230203
    assert decoded.startswith(b'\x03\x02\x23\x07')

def test_failed_translation(translator):
    """Tests a shader with a syntax error and checks for the correct error response."""
    vertex_shader_with_error = """
    attribute vec4 a_position;
    void main() {
        gl_Position = a_position * undeclared_variable; // Error here
    }
    """
    response = translator.translate_shader(
        shader_code=vertex_shader_with_error,
        shader_type="vertex",
        spec="webgl"
    )
    assert "error" in response
    assert response["error"]["code"] == -32002  # EFailCompile
    assert "data" in response["error"]
    info_log = response["error"]["data"]["info_log"]
    assert "ERROR:" in info_log
    assert "'undeclared_variable' : undeclared identifier" in info_log

def test_active_variables(translator):
    """Tests that the active_variables field is correctly populated."""
    shader = """
    precision mediump float;
    uniform float u_time;
    attribute vec2 a_pos;
    varying vec2 v_pos;
    void main() { v_pos = a_pos; gl_Position = vec4(a_pos, 0.0, 1.0); }
    """
    response = translator.translate_shader(
        shader_code=shader,
        shader_type="vertex",
        print_vars=True
    )
    assert "result" in response
    active_vars = response["result"]["active_variables"]
    assert "attributes" in active_vars
    assert "uniforms" in active_vars
    assert len(active_vars["attributes"]) == 1
    assert active_vars["attributes"][0]["name"] == "a_pos"
    assert len(active_vars["uniforms"]) == 1
    assert active_vars["uniforms"][0]["name"] == "u_time"