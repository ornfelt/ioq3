# ioq3 on gfx - status

ioq3 drawing, opening its window and reading its input through the gfx library
(`$code_root_dir/Code2/General/gfx/gfx_dll/gfx`), behind the `USE_GFX` CMake option (default on).
Work happens on the `gfx` branch; `main` stays the plain fork. The design follows ioq3-rs's
finished renderer_gfx (`$code_root_dir/Code2/Rust/ioq3-rs`, `gfx` branch, its `GFX_STATUS.md`).
This file is the current snapshot; the history is in git.

**Last run:** phase 2. The client's keyboard, text, mouse and window events come from the gfx
window: new `code/gfx/gfx_input.c` / `.h` (the port of ioq3-rs's `gfx_input.rs`) queue gfx's
events in a fixed ring and hand them to the client as the SDL path does; `sdl_input.c` dispatches
IN_ProcessEvents, IN_ActivateMouse / IN_DeactivateMouse, IN_Frame's focus test and IN_Init to it
while `GfxGLimp_GetWindow()` is not NULL. Checked on gl4 (RelWithDebInfo, q3dm1) with injected
input (keybd_event with scancodes, mouse_event) on the sdl, glfw and win32 windows: binds (F5,
MOUSE2, MOUSE4 from X1, MWHEELUP), console typing and Enter, the console key (win32 / glfw),
10 relative mouse moves (`cl_showMouseRate`), a held Shift released on focus loss, WM_CLOSE as
`quit Closed window`, and the same after `vid_restart` from win32 to glfw; `cl_renderer opengl1`
still reads SDL's input, and `USE_GFX=OFF` builds.

**Next:** phase 3 - 2D. `gfx_draw.c` / `.h` (programs, vertex / index / uniform rings, the
pipeline-key cache, draw submission), `gfx_shader.c` (loading `shaders/<dialect>/*.gfx` next to
the client), `gfx_gl.c`'s texture staging and upload (formats, mipmaps, picmip, texture modes,
glTexSubImage2D for cinematics), glDrawElements / glBegin..glEnd through the generic program,
clears through a bound pipeline so they obey GL's masks. Read ioq3-rs's phase 3 commit
(`82b20ed`), `gfx_draw.rs`, `gfx_gl.rs`, `gfx_shader.rs` and its "Shaders" / "Draws" decisions
first. Checkpoint: console and menus on gl4; start `GFX_TESTING.md`.

## Phases

- [x] **0. Setup** - `CMakeLists.txt` (`USE_GFX`, includes), `cmake/gfx.cmake`,
  `cmake/renderer_gfx.cmake`, `code/renderer_gfx/*.c|h` (copies of `code/renderergl1/`),
  `code/renderer_gfx/gfx_public.h` (`GfxGetRefAPI`), `gfx_glimp.c` / `.h` (backend selection,
  gfxinfo lines), `tr_init.c` / `tr_local.h` copies (the two cvars, gfxinfo, `GfxGetRefAPI`),
  `code/client/cl_main.c` (`CL_InitRef`), `code/renderer_gfx/shaders/` (ioq3-rs's, plus
  `generate_shaders.ps1` / `.sh`), `.gitignore`, `.gitattributes`, README, this file.
  Checkpoint: both configurations build; with `USE_GFX` the client runs renderer_opengl1 - done.
- [x] **1. Window and device** - `code/renderer_gfx/gfx_glimp.c` (GLimp_* on gfx, the D3D ->
  win32 rule, desktop size / win32 client size / screen read on Windows, `GfxGLimp_GetWindow`),
  `gfx_glimp.h`, `gfx_public.h` (`GfxGLimp_GetWindow`), `gfx_gl.c` / `gfx_gl.h` (new: the qgl
  calls on gfx, the extension entry points), `tr_local.h` (qgl names declared as functions),
  `cmake/renderer_gfx.cmake` (gfx_gl.c in, `SDL_RENDERER_SOURCES` out), `code/sdl/sdl_input.c`
  (`IN_GfxWindowWithoutSDL`: no SDL video needed, `gfx_window_poll_events` each frame), README.
  Checkpoint: a cleared frame on gl4 - done.
- [x] **2. Input** - `code/gfx/gfx_input.c` / `.h` (new, compiled into the client by
  `cmake/renderer_gfx.cmake`): event ring, key map, chars, relative mouse with grab / release,
  focus with key release, resize, close; `code/sdl/sdl_input.c` (gfx dispatch, SDL device / quit
  events taken before gfx polls, `IN_IsConsoleKey` shared); `gfx_glimp.c` (gfx's raw-input
  no-motion error to the developer console); README.
  Checkpoint: keys, text, mouse, focus, close on the sdl, glfw and win32 windows - done.
- [ ] **3. 2D** - `gfx_draw.c` / `.h` (programs, rings, pipeline-key cache, draw submission),
  `gfx_shader.c` (loading `shaders/<dialect>/*.gfx`), `gfx_gl.c` (texture staging and upload,
  glTexSubImage2D, glDrawElements / glArrayElement / glBegin..glEnd, clears through a bound
  pipeline so they obey GL's masks). Checkpoint: console and menus on gl4; `GFX_TESTING.md`
  from here on.
- [ ] **4. 3D** - polygon offset and clip plane as uniforms, the one generic program, bind
  caching, world / curves / models / sky / fog / dlights / marks / portals / mirrors,
  `screenshot` / `screenshotJPEG` / video capture checked. Checkpoint: a full game on q3dm1
  with bots.
- [ ] **5. Parity and speed** - frame comparisons against renderer_opengl1, overbright as blend
  passes (and glReadPixels taking the shift out again), `timedemo` on gl4 in a Release build.
- [ ] **6. All backends** - gl3, gles3, d3d11, d3d12, vk (validation layer); sdl, glfw, win32
  windows; x11 on Linux.
- [ ] 7. Optional, only when asked: renderergl2 features on gfx.

## Decisions

- **`USE_GFX`** (`CMakeLists.txt`, default on). Off, `cmake/gfx.cmake` and
  `cmake/renderer_gfx.cmake` return at once and the tree builds what `main` builds. On, it is a
  compile definition of the client and of renderer_gfx (not of the GL renderer dlls). It needs
  `USE_RENDERER_DLOPEN` (the GL renderers stay dlls) and a GL renderer enabled
  (`libraries/jpeg.cmake` sets up jpeg only then); gfx builds with MSVC on Windows and on Linux,
  elsewhere configuration stops with a message to use `-DUSE_GFX=OFF`.
- **gfx as a static library** (`cmake/gfx.cmake`): `add_library(gfx STATIC)` over gfx's sources at
  `GFX_DIR` (cache path, default `$code_root_dir/Code2/General/gfx/gfx_dll/gfx`), with gfx's
  per-platform source lists (Windows: gl3 / gl4 / gles3 / vk / d3d11 / d3d12 devices, win32 /
  sdl / glfw windows; Linux: the x11 window and the jkg device instead of the D3D / win32 ones),
  `GFX_ENABLE_TARGET_FRAMEBUFFER` public as in gfx's dll build, gfx's `/W3 /sdl /permissive-`
  instead of ioq3's `/W4`, and the system libraries (opengl32 d3d11 d3dcompiler dxgi; X11 Xi
  Xcursor GL pthread dl). It is compiled in ioq3's configuration (RelWithDebInfo gfx in a
  RelWithDebInfo build, so `NDEBUG` and no `GL_CALL_DEBUG`; gfx prints "(release)"). Code
  includes `"gfx/window.h"` & co. (`${GFX_DIR}/include` is the public include dir). No gfx.dll,
  no `gfx_dll_*` wrapper - and so ioq3-rs's Release-gfx step (its gap 8) is not needed here.
  GLFW: FetchContent of 3.4 on Windows as gfx does, from the gfx build's checkout
  (`build/_deps/glfw-src`) when it exists, so no download; `find_package(glfw3)` on Linux. The
  gfx tree is only read.
- **One SDL2.** gfx's SDL window is compiled against ioq3's own SDL2 (`libraries/sdl.cmake`: the
  `code/thirdparty/SDL2-2.32.8` headers and `code/thirdparty/libs/win64` import library on
  Windows, the system SDL2 elsewhere) and linked into the same client, so the one `SDL2.dll`
  ioq3 deploys next to `ioquake3` serves gfx's window and the client's audio. The two-SDL2
  question of ioq3-rs (gfx.dll's 2.30.7 next to the C tree's 2.32.8) does not arise.
- **renderer_gfx built into the client** (`cmake/renderer_gfx.cmake`): a static library of the
  renderer_gfx files, the renderer-common sources and the renderer libraries (jpeg), compiled
  with `RENDERER_DEFINITIONS` minus `USE_RENDERER_DLOPEN` - the way client.cmake compiles a static
  renderer - so it calls the engine's `Com_Printf` & co. directly and `tr_subs.c` / `q_shared.c` /
  `q_math.c` are not compiled twice; linked into ioquake3 together with gfx, so the renderer and
  the client's input share one copy of gfx. The client keeps `USE_RENDERER_DLOPEN`. No SDL
  sources in it (`SDL_RENDERER_SOURCES` left out); SDL's headers stay on its include path for the
  GL types `qgl.h` takes from `SDL_opengl.h`.
- **`CL_InitRef` hook** (`cl_main.c`, under `USE_GFX`): `cl_renderer` defaults to `gfx`;
  `cl_renderer gfx` calls renderer_gfx's `GfxGetRefAPI` (`code/renderer_gfx/gfx_public.h`; its own
  name, as the client's dlopen code declares a local `GetRefAPI`), any other name loads
  `renderer_<name>` as before, and a dll that fails to load resets `cl_renderer` to `gfx` and
  uses the built-in renderer (`using the built-in renderer_gfx`). The `main` code path is kept
  unchanged under `#else`. `refexport_t` / `refimport_t` are unchanged.
- **renderer_gfx = renderergl1's files, copied.** `code/renderer_gfx/` holds a copy of every
  `code/renderergl1/*.c` / `*.h` (first line `/* renderer_gfx: a copy of renderergl1/<file> */`);
  changes in them are marked `// renderer_gfx:`. So far: `tr_init.c` (`r_gfxDevice` /
  `r_gfxWindow`, gfxinfo's gfx lines, `GfxGetRefAPI`), `tr_local.h` (the two cvars; the qgl
  declarations, below).
- **The qgl calls are plain functions** (`gfx_gl.c`, where C is simpler than ioq3-rs). qgl.h's
  `GLE` lists stay as they are; renderer_gfx's `tr_local.h` declares the GL 1.1 names through
  qgl.h's own typedefs (`#define GLE(ret, name, ...) name##proc qgl##name;`) instead of as
  `extern` pointers, and gfx_gl.c defines them - direct calls, every call site unchanged.
  `qglGetStringi` stays a NULL pointer (gfxinfo then prints `GL_EXTENSIONS`). The extension entry
  points stay qgl.h's pointers: `GLimp_InitExtensions` sets `qglActiveTextureARB` /
  `qglClientActiveTextureARB` / `qglMultiTexCoord2fARB` and `qglLockArraysEXT` /
  `qglUnlockArraysEXT` to gfx_gl.c's functions unless `r_ext_multitexture` /
  `r_ext_compiled_vertex_array` is 0 - so their NULL tests in tr_image.c / tr_shade.c /
  tr_shader.c / tr_init.c work unchanged (ioq3-rs had to rewrite those tests to read flags).
  GL's state (caps, blend / depth / alpha / stencil, color mask, cull, polygon mode / offset,
  clip plane, matrices with push / pop, client arrays per unit, tex env, texture names and their
  parameters, glBegin vertices) is plain data in one `gfxGl` (`gfx_gl.h`), with fixed arrays:
  `GFX_GL_MAX_TEXTURES` names (twice `MAX_DRAWIMAGES`, ERR_DROP past it) and
  `GFX_GL_IMMEDIATE_VERTICES` (`SHADER_MAX_INDEXES`, the rest dropped with one warning).
  Emulated: GL 1.3 with GL_ARB_multitexture (2 units), GL_EXT_compiled_vertex_array,
  GL_EXT_texture_env_add, GL_EXT_texture_filter_anisotropic (max 16) and edge clamp; no texture
  compression. `GL_MAX_TEXTURE_SIZE` per device: gl4 / d3d11 / d3d12 16384, gl3 8192, vk 4096,
  gles3 2048. Clears, viewport, depth range and scissor go to the device; draws and texture
  uploads are recorded and dropped until phases 3 / 4.
- **GLimp on gfx** (`gfx_glimp.c`): the window is R8G8B8A8 + D24S8 (`r_ext_multisample` as the
  sample count), titled `CLIENT_WINDOW_TITLE`; `r_mode` as in the GL renderers, then the window's
  real size: gfx makes its windows dpi aware and grows a small one to most of the work area, so
  glConfig takes the size gfx gave (`gfx sized the window to W H`), for the win32 window from
  Windows' client rectangle of this thread's `ioquake3` window (it reports the requested size,
  ioq3-rs gap 9). `r_fullscreen` and `r_mode -2` make a desktop-sized window (desktop size from
  GetSystemMetrics on Windows, 640x480 elsewhere for now); toggling `r_fullscreen` runs
  `vid_restart`; `r_swapInterval` applied at creation; `minimize` prints that gfx cannot. A D3D
  `r_gfxDevice` with another `r_gfxWindow` uses the win32 window (console message), as gfx makes
  D3D devices only there. A failed window falls back to the default backends (gl4 / sdl), then
  to `r_mode 3`. gfx's error reports go to the console as warnings (`gfx_error_callback`).
  sdl_glimp's cvars (`r_allowResize`, `r_centerWindow`, ...) stay registered, unused.
- **Gamma** (ioq3-rs gap 2): gfx has no gamma ramp. `deviceSupportsGamma` only with `r_gamma 1`
  and `r_ignorehwgamma 0`; GLimp_SetGamma keeps the ramp's overbright shift
  (`gfxGl.overbrightPasses`; any other ramp: a note that `r_gamma` needs `vid_restart`, and
  `tr.overbrightBits`). The end-of-frame blend passes that apply it come in phase 5.
- **Window handoff** - a plain call, where ioq3-rs needed an exported `GetGfxWindow` looked up in
  the renderer dll: `GfxGLimp_GetWindow()` (`gfx_public.h`) returns renderer_gfx's window, NULL
  while it has none (another renderer loaded, or before GLimp_Init / after GLimp_Shutdown).
  `ri.IN_Init` gets NULL (there is no SDL window); `cl_main.c` needs no change for it.
- **Input on gfx** (`code/gfx/gfx_input.c`, the port of ioq3-rs's `gfx_input.rs`). For every
  gfx window, SDL ones included, IN_ProcessEvents calls `gfx_window_poll_events`; the event
  handler only copies events into a fixed 1024-entry ring (a Com_Error longjmp out of gfx's
  message dispatch would leave the window system mid-call), drained right after the poll. GFX_KEY_*
  map to the keys IN_TranslateSDLToQ3Key gives (letters / digits / US punctuation as ASCII,
  keypad, F1-F15, modifiers; F16+ and KP_EQUAL none, as there), then the console-key check;
  `in_keyboardDebug` prints the gfx key and mods. KEY_PRESS is SDL's repeat (dropped with no key
  catcher). Char events go through the SDL path's console-key / SE_CHAR logic; control
  characters (win32's WM_CHAR has them, SDL's text input not) are dropped - the key path queues
  CTRL-h / CTRL-letters as before. Mouse motion is the delta between gfx's reported positions
  (its virtual position while grabbed); `gfx_grab_cursor` / `gfx_ungrab_cursor` stand for SDL's
  relative mode + window grab, `in_nograb` ungrabs; motion is gobbled by pumping and dropping the
  queued moves. On release gfx puts the cursor back where the grab started (the SDL path warps
  it to the window center; `gfx_set_mouse_position` is there in the static build, but takes screen
  coordinates on the win32 window and window coordinates on the others, so ioq3-rs's behaviour is
  kept). Buttons: left / right / middle as K_MOUSE1-3, the rest by SDL's button number (X1 / X2
  as K_MOUSE4 / 5, then K_AUX as the SDL formula), with gfx's sdl window numbered as SDL and the
  win32 / glfw windows one lower (gap below). Focus: gfx has no query, so IN_GfxInit asks Windows
  whether the foreground window is this process's (elsewhere: focused), and IN_Init's
  `com_unfocused` follows it; FOCUS_IN / FOCUS_OUT after that; focus loss sends the key ups of
  the keys reported down (SDL does that itself) and a re-press arriving as a repeat counts as a
  press. Resize: only a size other than the last one, only with `r_allowResize`, then the SDL
  path's delayed `vid_restart`; a 0x0 size (win32 minimized) sets `com_minimized`. CLOSE runs
  `quit Closed window`. SDL keeps joysticks / gamepads and audio: IN_ProcessSDLDeviceEvents takes
  the gamepad hot-plug and SDL_QUIT events out of SDL's queue (SDL_PeepEvents) before gfx's sdl
  window polls it.
- **sdl_input.c's state stays in sdl_input.c** (where C is simpler than ioq3-rs, which made
  seven globals `pub(crate)`): `mouseActive` / `mouseAvailable` are tested and set around the
  `IN_Gfx*` calls, IN_GfxProcessEvents takes `in_eventTime` and `mouseActive` and returns whether
  a resize asks for the delayed `vid_restart`, gfx_input.c reads `in_keyboardDebug` / `in_nograb`
  through `Cvar_Get` and keeps its own `lastKeyDown` (each event path has its own). Only
  `IN_IsConsoleKey` is shared (not `static` under `USE_GFX`), as its parse cache follows
  `cl_consoleKeys->modified`. `IN_GfxWindowWithoutSDL()` still lets IN_Init run without SDL video
  on a non-SDL gfx window; the phase 1 poll in IN_Frame is gone (IN_GfxProcessEvents polls).
- **gfx's raw-input warning** (`gfx_glimp.c`): gfx's win32 window reports each raw mouse input
  without motion (a click or the wheel while grabbed) as an error; `GfxGLimp_Error` sends that
  one message to the developer console, the rest stay warnings.
- **glReadPixels from the screen** (ioq3-rs gap 3; here from phase 1, so screenshots can check
  each phase). On Windows, glReadPixels of GL_RGB / GL_RGBA bytes reads the gfx window's client
  area from the screen (GDI BitBlt + GetDIBits of this thread's `ioquake3` window, bottom-up rows
  as GL gives them): the frame presented last, and a covered window shows what covers it. Depth
  reads (flares) and other platforms get zeros (one warning). The callers apply the gamma ramp
  themselves (`R_GammaCorrect`), so fullscreen screenshots show the overbright ramp applied - as
  renderer_opengl1's do; once phase 5 draws the overbright passes, glReadPixels takes the shift
  out again (`v >> n`, as ioq3-rs).
- **Shaders.** `code/renderer_gfx/shaders/` is ioq3-rs's `shaders/` unchanged: `generic.vs.gfxs` /
  `generic.fs.gfxs` and the compiled `shaders/` (gl3, gl4, gles3), `shaders_d3/` (d3d11; d3d12
  loads it), `shaders_gles3_dark/` (compiled from a folder named so: gfx's compiler adds its
  Windows gles3 gamma fix), `shaders_vk/` (SPIR-V). `.gitattributes` marks `*.gfx` binary. A
  post-build step of ioquake3 copies the four folders next to it as `shaders/<dialect>/` (ioq3-rs's
  dist layout; also installed). `generate_shaders.ps1` / `.sh` build `wc_compiler_rs` with rustc
  into `build_shaders/` and recompile every folder (vk only with glslangValidator); run with
  ioq3-rs's glslang 16.6 they reproduce all 8 files byte for byte. Not loaded yet (phase 3).
- **Builds** go to `build_gfx` (`USE_GFX` on) and `build_nogfx` (off), `build/` is main's. Only
  Visual Studio 18 2026 is installed here, so the generator is `-G "Visual Studio 18 2026" -A x64`
  (the skill's commands name VS 17 2022).

## Backends (Windows; this gfx build has devices gl3 gl4 gles3 vk d3d11 d3d12, windows win32 glfw sdl)

renderer_gfx only clears so far (`r_clear 1`), so "clears" means: window and device made, the
clear color shown, `vid_restart` in and out, clean quit; "+ input": phase 2's input check passes
(keys, chars, console key, mouse motion / buttons / wheel, focus loss, close). The other devices wait for phase 6 (GPU safety:
each feature on gl4 first). The x11 window and the Linux build are untested here (no Linux
machine; `cmake/gfx.cmake`'s Linux branch follows gfx's CMakeLists.txt).

| | sdl | glfw | win32 |
| --- | --- | --- | --- |
| gl4 (baseline) | clears + input | clears + input | clears + input |
| gl3 | - | - | - |
| gles3 | - | - | - |
| d3d11 | - (uses win32, not run yet) | - (same) | - |
| d3d12 | - (uses win32, not run yet) | - (same) | - |
| vk | - | - | - |

## gfx gaps (for the gfx maintainer)

Met here so far, each also in ioq3-rs's list (its `GFX_STATUS.md`, gaps 1-28, which are gfx's,
so the rest are expected here too):

- ioq3-rs gap 1: no fullscreen, display mode list, desktop size, window position or minimize
  (workaround: a desktop-sized window, the size from GetSystemMetrics on Windows only).
- ioq3-rs gap 2: no gamma ramp (workaround: overbright as end-of-frame blend passes, phase 5).
- ioq3-rs gap 3: no read back of the color or depth buffer (workaround on Windows: the color from
  the screen through GDI, one frame late and only what is visible; no depth).
- ioq3-rs gap 9: the win32 window's `gfx_window_get_width` / `_height` give the requested size,
  not the client area (r_mode 4: 800x600 reported, 1291x941 client; sdl / glfw report their real
  1307x980). Workaround: the client rectangle from Windows.
- ioq3-rs gap 11: the sdl window maps keys by SDL keycode (layout) without a scancode: on this
  Swedish layout the key left of 1 (`§`) arrives as GFX_KEY_UNKNOWN there, where win32 / glfw
  map it by position to GFX_KEY_GRAVE (the console key). The SDL path makes it K_WORLD_53, which
  is no console key either, so the console does not open from it on the SDL path or the sdl gfx
  window alike (bind a key to `toggleconsole`, or Shift+Esc).
- ioq3-rs gap 12: no focus query, the win32 window takes the focus before a handler is set
  (workaround: GetForegroundWindow's process on Windows).
- ioq3-rs gap 13: no minimize / restore event (win32's 0x0 resize is used).
- ioq3-rs gap 14: no key ups on focus loss, a key held over it comes back as KEY_PRESS
  (workaround in gfx_input.c).
- ioq3-rs gap 28, seen on glfw too: at r_mode 4 the glfw window's client area (1307x980) runs
  under the taskbar - a screen capture of it shows the taskbar over its bottom rows. ioq3-rs
  recorded this for the win32 window only.

New here (ioq3-rs has the same behaviour, unrecorded):

- The sdl window numbers the mouse buttons past middle as SDL does (`get_button` returns SDL's
  number: X1 = 4 = GFX_MOUSE_BUTTON_5), the win32 / glfw windows one lower (X1 =
  GFX_MOUSE_BUTTON_4), so one physical button is two gfx buttons. Workaround: gfx_input.c
  renumbers by window backend (X1 is K_MOUSE4 on every window, as on the SDL path; ioq3-rs, by
  its code, gives K_MOUSE5 for X1 on the sdl window).
- The win32 window's WM_INPUT reports a raw mouse input without motion (button or wheel
  transitions, which raw input also delivers while grabbed) as an error, "not RELATIVE or ABSOLUTE
  mouse move: 0" - a warning per click in game through the error callback. Workaround: that
  message goes to the developer console (`gfx_glimp.c`).
- The win32 window drops every character whose UTF-16 low byte is 0x80 or more (`å ä ö §`, ...)
  with "invalid event codepoint": WM_CHAR packs the code unit into a `char tmp[2]` and jks's
  `utf16_decode1` widens the signed `char` bytes, so 0xA7 becomes 0xFFFFFFA7 and `utf8_encode`
  refuses it. No workaround (the sdl / glfw windows deliver them).
- `gfx_set_mouse_position` takes screen coordinates on the win32 window (SetCursorPos) and window
  coordinates on the sdl / glfw ones - gfx_input.c does not warp the cursor.

Two of ioq3-rs's gaps do not apply to a static build: gap 6 (clipboard, cursors,
`gfx_set_mouse_position`, window icon missing from gfx.dll's exports - the static library has
the whole C API) and gap 8 (only a Debug gfx in the gfx tree - gfx is compiled here in ioq3's
configuration). Compiling gfx's sources gives gfx's own `/W3` warnings (C4244 / C4267
conversions, `HAVE_STDINT_H` redefined by `config.h`, `NDEBUG` redefined in `gl4.c`, `APIENTRY`
redefined), as gfx's own build does.

## Open problems / accepted differences from renderer_opengl1

- renderer_gfx draws nothing but clears until phase 3 (2D) / 4 (3D); `cl_renderer opengl1` plays.
- Non-ASCII characters (`å ä ö`) cannot be typed on the win32 window (gfx gap above); the sdl /
  glfw windows and the SDL path type them.
- On release of a grabbed mouse the cursor goes back to where the grab started, not to the window
  center as on the SDL path (gfx_set_mouse_position gap above; ioq3-rs does the same).
- Fullscreen frames have no overbright on screen until phase 5 (darker than renderer_opengl1
  with its hardware ramp); fullscreen screenshots already have the ramp applied by
  `R_GammaCorrect`.
- Not ioq3-gfx: the RelWithDebInfo client (main's `build/` too) crashes with an access
  violation in JIT-compiled QVM code right after `ui.qvm` loads, whatever the renderer. Run
  with `+set vm_ui 1 +set vm_cgame 1 +set vm_game 1` (the interpreter) or the dll modules
  (`+set sv_pure 0 +set vm_ui 0 +set vm_cgame 0 +set vm_game 0`).

## Testing notes

- Fresh home per run: `+set fs_homepath <dir>`; game data hard-linked from `build/`'s
  `baseq3/pak0-8.pk3` into `build_gfx/RelWithDebInfo/baseq3/`.
- `r_fullscreen` defaults to 1: pass `+set r_fullscreen 0` for a window.
- Every `+set` on the command line is applied at startup (`Com_StartupVariable`), whatever its
  position, so a backend switch later in the script needs `+seta` (run in order):
  `+set r_clear 1 +wait 100 +screenshot +seta r_gfxWindow glfw +vid_restart +wait 100 +screenshot`.
- Phase 1 check: `ioquake3 +set fs_homepath <dir> +set vm_ui 1 +set vm_cgame 1 +set vm_game 1
  +set logfile 2 +set r_mode 4 +set r_fullscreen 0 +set r_clear 1 +wait 100 +screenshot ...`
  through the sdl, glfw and win32 windows; screenshots (`screenshots/shot000n.tga`) must be the
  clear color (255,0,128) - (255,0,255) in fullscreen, where R_GammaCorrect doubles it. Also
  `+devmap q3dm1` fullscreen, `+set r_gfxDevice bogus`, and `+set cl_renderer opengl1`.
- Phase 2 check (a PowerShell script, not in the tree): start `+devmap q3dm1` windowed with
  `+set in_keyboardDebug 1 +set cl_showMouseRate 1 +set cl_mouseAccel 0.0001` (showMouseRate
  prints only with mouse acceleration) and binds that `echo` (F5, MOUSE2, MOUSE4, MWHEELUP,
  F6 `toggleconsole`); inject with `keybd_event( vk, MapVirtualKey( vk, 0 ), ... )` and
  `mouse_event`; take the focus with a WinForms window of the script, after an injected F24 key
  up (Windows lets the process of the last input set the foreground); WM_CLOSE to quit; read
  `qconsole.log`. Never inject Esc / CapsLock here (this machine swaps them); in game, console
  text without a slash is sent as chat (`con_autochat`). Windows 11's `Start-Process notepad`
  returns a launcher, not the Notepad window - do not use it as the focus thief.
- The game console goes to stderr and `qconsole.log`; gfx's `[gfx] using ...` lines go to
  stdout (ioquake3 is a GUI program on Windows: `Start-Process -RedirectStandardOutput`).

## Performance (timedemo)

Not measured yet (phase 5).
