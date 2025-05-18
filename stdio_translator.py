import pygame as pg
import moderngl
import struct
import time
import subprocess # Still needed for ShaderTranslatorRPCClient
import os
import re
import json     # For ShaderTranslatorRPCClient
import base64   # For ShaderTranslatorRPCClient

# --- ShaderTranslatorRPCClient Class (from previous responses) ---
# Ensure this class definition is included here. For brevity, I'm assuming it's
# the version we developed that handles JSON RPC, process management, and base64.
# If you need it again, I can paste the full class.
class ShaderTranslatorRPCClient:
    def __init__(self, translator_executable_path):
        self.executable_path = translator_executable_path
        self.process = None
        self.request_id_counter = 0
        self._start_process()

    def _start_process(self):
        if self.process and self.process.poll() is None:
            return # Already running

        if not os.path.isfile(self.executable_path):
            raise FileNotFoundError(f"Translator executable not found at {self.executable_path}")
        if not os.access(self.executable_path, os.X_OK):
            raise PermissionError(f"Translator executable at {self.executable_path} is not executable")

        try:
            self.process = subprocess.Popen(
                [self.executable_path, "--json-rpc"],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1, 
                universal_newlines=True, # Ensures text mode for pipes
                encoding='utf-8' # Explicitly set encoding
            )
        except Exception as e:
            raise ConnectionError(f"Failed to start translator subprocess: {e}")


    def _generate_request_id(self):
        self.request_id_counter += 1
        return f"py_req_{self.request_id_counter}_{time.time_ns()}" # More unique ID

    def _send_request(self, method: str, params: dict) -> dict:
        if not self.process or self.process.poll() is not None:
            # print("DEBUG: Translator process is not running. Attempting to restart...")
            self._start_process()
            if not self.process or self.process.poll() is not None:
                 raise ConnectionError("Failed to start or connect to the translator process after restart attempt.")

        request_id = self._generate_request_id()
        rpc_request = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params,
            "id": request_id
        }
        request_str = json.dumps(rpc_request)
        
        # print(f"DEBUG: Sending to C++ (ID: {request_id}): {request_str}")
        try:
            self.process.stdin.write(request_str + "\n")
            self.process.stdin.flush()

            # It's crucial to handle the possibility of the process dying
            # and readline() blocking indefinitely or returning empty.
            response_str = self.process.stdout.readline()
            # print(f"DEBUG: Received from C++ (ID: {request_id}): {response_str.strip()}")

            if not response_str: # Empty response often means process died
                self.process.poll() # Update returncode status
                stderr_output = ""
                try: # Try to grab stderr output non-blockingly if possible (tricky)
                    if self.process.stderr:
                        # Reading all of stderr can block if the process is still writing.
                        # For a quick check, one might use select or a very short timeout read.
                        # For simplicity, let's assume a quick read is okay or it's already available.
                         stderr_output = "".join(self.process.stderr.readlines() if self.process.stderr.readable() else [])

                except Exception as e_stderr:
                    stderr_output = f"(Error reading stderr: {e_stderr})"
                
                raise ConnectionError(
                    f"No response from translator process (ID: {request_id}). It might have crashed. "
                    f"Return code: {self.process.returncode}. Stderr hint: '{stderr_output.strip()}'"
                )

            response_json = json.loads(response_str)
            if response_json.get("id") != request_id:
                 # This is a more serious issue, indicates out-of-order or mismatched responses
                print(f"CRITICAL WARNING: Response ID mismatch for request {request_id}. Expected {request_id}, got {response_json.get('id')}. Response: {response_json}")
                # Depending on the application, this could be a fatal error or an ignored one.

            return response_json

        except BrokenPipeError:
            self.process.poll()
            stderr_output = "".join(self.process.stderr.readlines() if self.process.stderr and self.process.stderr.readable() else [])
            raise ConnectionError(
                f"Broken pipe for request {request_id}. Translator process likely crashed. "
                f"Return code: {self.process.returncode}. Stderr: '{stderr_output.strip()}'"
            )
        except Exception as e:
            self.process.poll()
            stderr_output = "".join(self.process.stderr.readlines() if self.process.stderr and self.process.stderr.readable() else [])
            raise ConnectionError(
                f"Exception during communication for request {request_id}: {type(e).__name__} - {e}. "
                f"Return code: {self.process.returncode}. Stderr: '{stderr_output.strip()}'"
            )

    def translate(self, shader_code_str: str, shader_type: str, 
                  spec: str, output_format: str, 
                  compile_options: dict = None, 
                  resources: dict = None, 
                  print_active_variables: bool = False) -> dict:

        shader_code_base64 = base64.b64encode(shader_code_str.encode('utf-8')).decode('ascii')
        
        params = {
            "shader_code_base64": shader_code_base64,
            "shader_type": shader_type,
            "spec": spec,
            "output": output_format,
            "print_active_variables": print_active_variables
        }
        
        # Ensure default compile_options are set if not provided, especially object_code
        final_compile_options = {"object_code": True, "initialize_uninitialized_locals": True}
        if compile_options:
            final_compile_options.update(compile_options)
        params["compile_options"] = final_compile_options

        if resources:
            params["resources"] = resources

        response = self._send_request("translate", params)

        if "error" in response and response["error"] is not None:
            err = response["error"]
            error_message = f"ANGLE Translation Error (Code: {err.get('code')}, Method: translate): {err.get('message')}"
            # Attempt to get info_log from error data or result payload
            info_log_details = err.get("data", {}).get("info_log")
            if not info_log_details and response.get("result") and isinstance(response["result"], dict):
                info_log_details = response["result"].get("info_log")
            if info_log_details:
                 error_message += f"\n--- ANGLE Info Log ---\n{info_log_details}\n----------------------"
            raise ValueError(error_message)
        
        result = response.get("result")
        if not isinstance(result, dict): # Check if result is a dictionary
            # This can happen if "error" was present but incorrectly not None, or other response malformation
            info_log_details = ""
            if isinstance(response.get("result"), dict) : # Check if result exists and is a dict
                 info_log_details = response["result"].get("info_log","")

            raise ValueError(f"Translation Error: 'result' field is missing or not a dictionary. "
                             f"InfoLog (if any): {info_log_details}. Full response: {response}")

        translated_code_output = None
        translated_code_b64 = result.get("object_code_base64")

        if translated_code_b64 is not None: # Allow empty string for empty code
            try:
                decoded_bytes = base64.b64decode(translated_code_b64)
                if output_format == "spirv": # Add other binary output formats if any
                    translated_code_output = decoded_bytes
                else:
                    translated_code_output = decoded_bytes.decode('utf-8')
            except Exception as e_decode:
                raise ValueError(f"Error decoding base64 object code: {e_decode}. Base64 (first 100 chars): '{str(translated_code_b64)[:100]}'")

        elif final_compile_options.get("object_code", True): # Object code was expected
            # This is problematic if object_code was true and we got no 'object_code_base64' key
             info_log_content = result.get('info_log', '(Info log not available in result)')
             raise ValueError(f"Translation warning/error: 'object_code_base64' key missing in result, "
                              f"though object_code was requested.\n--- ANGLE Info Log ---\n{info_log_content}\n----------------------")


        return {
            "object_code": translated_code_output,
            "info_log": result.get("info_log", ""), # Default to empty string if missing
            "active_variables": result.get("active_variables") # Will be dict or None
        }

    def shutdown(self):
        if self.process and self.process.poll() is None:
            # print("DEBUG: Sending shutdown request to translator...")
            try:
                self._send_request("shutdown", {}) # No params needed for shutdown
            except ConnectionError as e: # Catch errors if process died before shutdown ack
                print(f"INFO: Connection error during shutdown (process might have already exited): {e}")
            except Exception as e:
                print(f"INFO: Exception during shutdown request: {e}")
            finally:
                # Aggressively try to clean up the process
                if self.process:
                    if self.process.stdin:
                        try: self.process.stdin.close()
                        except: pass
                    if self.process.stdout:
                        try: self.process.stdout.close()
                        except: pass
                    if self.process.stderr:
                        try: self.process.stderr.close()
                        except: pass
                    
                    if self.process.poll() is None: # If still running
                        try:
                            self.process.terminate() # Ask nicely
                            self.process.wait(timeout=0.5) # Short wait
                        except subprocess.TimeoutExpired:
                            print("INFO: Translator process did not terminate, killing.")
                            self.process.kill() # Force kill
                        except Exception as e_term:
                            print(f"INFO: Exception during terminate/kill: {e_term}")
        # print("DEBUG: Translator process shut down.")
        self.process = None

    def __del__(self):
        self.shutdown()
# --- End ShaderTranslatorRPCClient Class ---


class ShaderToy:
    def __init__(self, width=1920, height=1080):
        self.width = width
        self.height = height
        self.screen_size = (self.width, self.height)

        pg.init()
        pg.display.gl_set_attribute(pg.GL_CONTEXT_MAJOR_VERSION, 3)
        pg.display.gl_set_attribute(pg.GL_CONTEXT_MINOR_VERSION, 3)
        pg.display.gl_set_attribute(pg.GL_CONTEXT_PROFILE_MASK, pg.GL_CONTEXT_PROFILE_CORE)
        pg.display.gl_set_attribute(pg.GL_DOUBLEBUFFER, 1)
        pg.display.gl_set_attribute(pg.GL_DEPTH_SIZE, 0)

        self.screen = pg.display.set_mode(self.screen_size, pg.OPENGL | pg.DOUBLEBUF)
        pg.display.set_caption("ModernGL Shadertoy with ANGLE RPC")
        self.ctx = moderngl.create_context()

        self.start_time = time.time()
        self.current_time = 0.0
        self.frame_count = 0
        self.mouse_pos = [0.0, 0.0, 0.0, 0.0]
        self.last_frame_time = self.start_time

        # Initialize ANGLE RPC Client
        self.angle_translator_path = os.getenv(
            "ANGLE_TRANSLATOR_PATH",
            # Provide a sensible default or raise an error if not set
            # Example for macOS:
            # os.path.expanduser("~/angle/out/Release/angle_shader_translator") 
            # Example for Windows:
            # "C:/dev/angle/out/Release/angle_shader_translator.exe"
            # For this script, let's make it mandatory to set it via ENV or edit here
            "/Users/richardinsley/Projects/libangle/angle/out/M1_Static_Release/angle_shader_translator" # Fallback to user's original path
        )
        if not os.path.isfile(self.angle_translator_path):
             alt_path_suggestion = "YOUR_PATH_TO/angle/out/Default/angle_shader_translator" # Common default build dir
             print(f"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
             print(f"!!! ERROR: ANGLE Translator not found at '{self.angle_translator_path}'")
             print(f"!!! Please set the ANGLE_TRANSLATOR_PATH environment variable or edit the !!!")
             print(f"!!! path in this script. A common path might be: {alt_path_suggestion}    !!!")
             print(f"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
             pg.quit()
             exit(1)
        
        print(f"Using ANGLE Translator Path: {self.angle_translator_path}")
        try:
            self.translator_client = ShaderTranslatorRPCClient(self.angle_translator_path)
        except Exception as e:
            print(f"FATAL: Could not initialize ShaderTranslatorRPCClient: {e}")
            pg.quit()
            exit(1)

        # 1. Define WebGL GLSL ES Vertex Shader
        webgl_vertex_shader_source = """#version 300 es
            in vec2 in_vert;
            out vec2 v_frag_coord_uv; 
            void main() {
                gl_Position = vec4(in_vert, 0.0, 1.0);
                v_frag_coord_uv = in_vert * 0.5 + 0.5;
            }
        """

        # 2. Define your Shadertoy core logic
        shadertoy_core_logic = self.get_shadertoy_logic() # Using a helper for clarity

        # 3. Construct the full WebGL GLSL ES Fragment Shader string
        gles_fragment_shader_string = f"""#version 300 es
precision highp float;
uniform vec3 iResolution;
uniform float iTime;
uniform float iTimeDelta;
uniform int iFrame;
uniform vec4 iMouse;
// uniform sampler2D iChannel0; // Example, if you add textures

in vec2 v_frag_coord_uv; 
out vec4 out_FragColor;

{shadertoy_core_logic}

void main() {{
    vec2 pixel_coords = v_frag_coord_uv * iResolution.xy;
    mainImage(out_FragColor, pixel_coords);
}}
"""
        # 4. Translate shaders using RPC client
        # Original CLI options: "-s=w2", "-b=g330", "-o"
        # "w2"         -> spec="webgl2"
        # "g330"       -> output_format="glsl330" (Desktop GL 3.30 core)
        # "-o"         -> compile_options={"object_code": True}
        # For "-g330", ANGLE's `ParseGLSLOutputVersion` maps 330 to SH_GLSL_330_CORE_OUTPUT.
        # The JSON RPC C++ side should handle "glsl330" as a string for `output` param.

        translation_params = {
            "spec": "webgl2",
            "output_format": "glsl330", # The C++ side maps this to the correct ShShaderOutput
            "compile_options": {"object_code": True, "initialize_uninitialized_locals": True},
            "print_active_variables": True # Crucial for mapping
        }

        translated_vertex_shader = None
        vs_active_vars = None
        print("Translating Vertex Shader via RPC...")
        try:
            vs_result = self.translator_client.translate(
                webgl_vertex_shader_source, "vertex", **translation_params
            )
            translated_vertex_shader = vs_result["object_code"]
            vs_active_vars = vs_result["active_variables"]
            print("Vertex Shader Info Log:\n", vs_result["info_log"])
            if not translated_vertex_shader:
                raise ValueError("Translated vertex shader code is empty but no error was raised.")
        except Exception as e:
            print(f"Vertex Shader translation FAILED via RPC: {e}")
            translated_vertex_shader = """#version 330 core
                in vec2 in_vert; void main() { gl_Position = vec4(in_vert, 0.0, 1.0); }"""

        translated_fragment_shader = None
        fs_active_vars = None
        print("\nTranslating Fragment Shader via RPC...")
        try:
            fs_result = self.translator_client.translate(
                gles_fragment_shader_string, "fragment", **translation_params
            )
            translated_fragment_shader = fs_result["object_code"]
            fs_active_vars = fs_result["active_variables"]
            print("Fragment Shader Info Log:\n", fs_result["info_log"])
            if not translated_fragment_shader:
                 raise ValueError("Translated fragment shader code is empty but no error was raised.")
        except Exception as e:
            print(f"Fragment Shader translation FAILED via RPC: {e}")
            translated_fragment_shader = """#version 330 core
                uniform float iTime; in vec2 v_frag_coord_uv; out vec4 fragColor;
                void main() { fragColor = vec4(v_frag_coord_uv, 0.5 + 0.5*sin(iTime), 1.0); }"""

        # 5. Create ModernGL program
        if not translated_vertex_shader or not translated_fragment_shader:
            print("FATAL: Shader translation failed. Cannot create program.")
            self.shutdown_resources()
            exit(1)

        try:
            # print("\n--- Vertex Shader for ModernGL ---")
            # print(translated_vertex_shader)
            # print("\n--- Fragment Shader for ModernGL ---")
            # print(translated_fragment_shader)
            self.program = self.ctx.program(
                vertex_shader=translated_vertex_shader,
                fragment_shader=translated_fragment_shader
            )
        except moderngl.Error as e:
            print(f"FATAL: ModernGL Program Creation Error: {e}")
            print("\n--- Vertex Shader Sent to ModernGL (Error Context) ---")
            print(translated_vertex_shader)
            print("\n--- Fragment Shader Sent to ModernGL (Error Context) ---")
            print(translated_fragment_shader)
            self.shutdown_resources()
            exit(1)

        # 6. Get Uniform locations using active_variables
        self.uniforms = {}
        shadertoy_uniform_names = ["iResolution", "iTime", "iTimeDelta", "iFrame", "iMouse"]
        
        print("\nMapping uniforms using ANGLE's active_variables:")
        # Uniforms can be in "uniforms" (standalone) or "uniform_blocks" (members of UBOs)
        all_active_uniform_like_vars = []
        if fs_active_vars:
            if fs_active_vars.get("uniforms"): # Standalone uniforms
                all_active_uniform_like_vars.extend(fs_active_vars["uniforms"])
            if fs_active_vars.get("uniform_blocks"): # Uniforms within UBOs
                for ubo in fs_active_vars["uniform_blocks"]:
                    if ubo and ubo.get("fields"):
                        # Add UBO name as a prefix for uniqueness if needed, or just add fields
                        # For shadertoy, uniforms are typically global/standalone from GLSL ES perspective
                        all_active_uniform_like_vars.extend(ubo["fields"])
        
        if all_active_uniform_like_vars:
            for st_uniform_name in shadertoy_uniform_names:
                found_angle_uniform = None
                for angle_var in all_active_uniform_like_vars:
                    if angle_var and angle_var.get("name") == st_uniform_name:
                        found_angle_uniform = angle_var
                        break
                
                if found_angle_uniform:
                    mapped_name = found_angle_uniform.get("mapped_name", st_uniform_name)
                    try:
                        self.uniforms[st_uniform_name] = self.program[mapped_name]
                        # print(f"  Mapped Shadertoy uniform '{st_uniform_name}' to ANGLE's '{mapped_name}' (Location: {self.uniforms[st_uniform_name].location})")
                    except KeyError:
                        print(f"  Warning: ANGLE reported uniform '{st_uniform_name}' (mapped: '{mapped_name}'), but not found in ModernGL program.")
                else:
                    # Fallback for uniforms ANGLE might not report if optimized out or if mainImage itself is complex
                    # print(f"  Warning: Shadertoy uniform '{st_uniform_name}' not in active_variables. Trying direct/guessed names.")
                    try: self.uniforms[st_uniform_name] = self.program[st_uniform_name]
                    except KeyError: 
                        try: self.uniforms[st_uniform_name] = self.program["_u" + st_uniform_name] # Old guess
                        except KeyError:
                             print(f"    Uniform '{st_uniform_name}' NOT found in program (via active_vars or guesses).")
        else:
            print("\nWarning: No 'active_variables' for fragment shader uniforms. Using fallback guessing for all.")
            for st_uniform_name in shadertoy_uniform_names:
                 try: self.uniforms[st_uniform_name] = self.program[st_uniform_name]
                 except KeyError: 
                    try: self.uniforms[st_uniform_name] = self.program["_u" + st_uniform_name]
                    except KeyError: print(f"  Fallback uniform '{st_uniform_name}' NOT found.")


        # Fullscreen quad VBO and VAO
        quad_vertices = [-1.0, -1.0,  1.0, -1.0,  -1.0,  1.0,   -1.0,  1.0,  1.0, -1.0,  1.0,  1.0]
        vertex_data = struct.pack(f'{len(quad_vertices)}f', *quad_vertices)
        self.vbo = self.ctx.buffer(vertex_data)

        vs_in_attribute_name = "in_vert" # Default
        if vs_active_vars and vs_active_vars.get("attributes"):
            for attr in vs_active_vars["attributes"]:
                if attr and attr.get("name") == "in_vert":
                    vs_in_attribute_name = attr.get("mapped_name", "in_vert")
                    break
        print(f"Using vertex shader attribute name for VBO: '{vs_in_attribute_name}'")
        
        try:
            self.vao = self.ctx.simple_vertex_array(self.program, self.vbo, vs_in_attribute_name)
        except Exception as e: # Catch more general errors from moderngl if attribute name is wrong
             print(f"FATAL: Error creating VAO with attribute '{vs_in_attribute_name}': {e}")
             self.shutdown_resources()
             exit(1)

    def get_shadertoy_logic(self):
        # Your original shadertoy_core_logic or a new one
        return """
vec2 stanh(vec2 a) {
    return tanh(clamp(a, -40.,  40.));
}
void mainImage( out vec4 o, vec2 u )
{
    vec2 v = iResolution.xy;
         u = .2*(u+u-v)/v.y;    
         
    vec4 z = o = vec4(1,2,3,0);
     
    for (float a = .5, t = iTime, i; 
         ++i < 19.; 
         o += (1. + cos(z+t)) 
            / length((1.+i*dot(v,v)) 
                   * sin(1.5*u/(.5-dot(u,u)) - 9.*u.yx + t))
         )  
        v = cos(++t - 7.*u*pow(a += .03, i)) - 5.*u, 
        // use stanh here if shader has black artifacts
        //   vvvv
        u += stanh(40. * dot(u *= mat2(cos(i + .02*t - vec4(0,11,33,0)))
                           ,u)
                      * cos(1e2*u.yx + t)) / 2e2
           + .2 * a * u
           + cos(4./exp(dot(o,o)/1e2) + t) / 3e2;
              
     o = 25.6 / (min(o, 13.) + 164. / o) 
       - dot(u, u) / 250.;
}
"""

    def render(self):
        self.ctx.clear(0.0, 0.0, 0.0)
        new_time = time.time()
        time_delta = new_time - self.last_frame_time
        if time_delta <= 0: time_delta = 1.0 / 60.0 # Avoid zero or negative delta
        self.last_frame_time = new_time
        self.current_time = new_time - self.start_time

        if self.uniforms.get("iResolution"):
            self.uniforms["iResolution"].value = (self.width, self.height, 1.0)
        if self.uniforms.get("iTime"):
            self.uniforms["iTime"].value = self.current_time
        if self.uniforms.get("iMouse"):
            self.uniforms["iMouse"].value = tuple(self.mouse_pos)
        if self.uniforms.get("iFrame"):
             self.uniforms["iFrame"].value = self.frame_count
        if self.uniforms.get("iTimeDelta"):
             self.uniforms["iTimeDelta"].value = time_delta

        self.vao.render(moderngl.TRIANGLES)
        pg.display.flip()
        self.frame_count += 1
    
    def run(self):
        running = True
        while running:
            for event in pg.event.get():
                if event.type == pg.QUIT:
                    running = False
                if event.type == pg.KEYDOWN:
                    if event.key == pg.K_ESCAPE:
                        running = False
                # Shadertoy iMouse: xy = current coord, zw = click coord (z<0 means released)
                current_mouse_pixel = event.pos if event.type in (pg.MOUSEMOTION, pg.MOUSEBUTTONDOWN, pg.MOUSEBUTTONUP) else (self.mouse_pos[0], self.height - 1 - self.mouse_pos[1]) # last known if no event
                
                if event.type == pg.MOUSEMOTION:
                    self.mouse_pos[0] = float(current_mouse_pixel[0])
                    self.mouse_pos[1] = float(self.height - 1 - current_mouse_pixel[1]) # Invert Y for GL
                    if pg.mouse.get_pressed()[0]: # If left button is down during motion
                        self.mouse_pos[2] = self.mouse_pos[0]
                        self.mouse_pos[3] = self.mouse_pos[1]
                
                if event.type == pg.MOUSEBUTTONDOWN:
                    if event.button == 1: # Left mouse button
                        self.mouse_pos[0] = float(current_mouse_pixel[0])
                        self.mouse_pos[1] = float(self.height - 1 - current_mouse_pixel[1])
                        self.mouse_pos[2] = self.mouse_pos[0] # Set click coordinates
                        self.mouse_pos[3] = self.mouse_pos[1]
                
                if event.type == pg.MOUSEBUTTONUP:
                     if event.button == 1: 
                        # On release, keep xy as current, make zw negative of last click
                        # This signals release for one frame as per some Shadertoy conventions
                        self.mouse_pos[2] = -abs(self.mouse_pos[2]) if self.mouse_pos[2] > 0 else 0.0 
                        self.mouse_pos[3] = -abs(self.mouse_pos[3]) if self.mouse_pos[3] > 0 else 0.0
            
            self.render()

            # After rendering, if zw were negative for release signal, reset them
            # This means click is only active on the down press and release signal is transient
            if self.mouse_pos[2] < 0.0: self.mouse_pos[2] = 0.0
            if self.mouse_pos[3] < 0.0: self.mouse_pos[3] = 0.0
        
        self.shutdown_resources()

    def shutdown_resources(self):
        print("Shutting down resources...")
        if hasattr(self, 'translator_client') and self.translator_client:
            self.translator_client.shutdown()
        # Release ModernGL resources explicitly if needed, though context destruction handles most
        if hasattr(self, 'vao') and self.vao: self.vao.release()
        if hasattr(self, 'vbo') and self.vbo: self.vbo.release()
        if hasattr(self, 'program') and self.program: self.program.release()
        if hasattr(self, 'ctx') and self.ctx: self.ctx.release()
        pg.quit()


if __name__ == '__main__':
    # IMPORTANT: User needs to set ANGLE_TRANSLATOR_PATH environment variable
    # or edit the default path in ShaderToy.__init__
    if not os.getenv("ANGLE_TRANSLATOR_PATH") and \
       not os.path.isfile("release/angle_shader_translator"):
        print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
        print("!!! CRITICAL: ANGLE_TRANSLATOR_PATH environment variable not set,       !!!")
        print("!!! and the hardcoded fallback path is likely incorrect or missing.     !!!")
        print("!!! Please set ANGLE_TRANSLATOR_PATH to your compiled                 !!!")
        print("!!! 'angle_shader_translator' executable, or update the script.       !!!")
        print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
        # exit(1) # Could exit here, or let it try and fail in ShaderToy init

    app = ShaderToy()
    app.run()