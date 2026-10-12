# ioq3 on gfx - status

ioq3 drawing, opening its window and reading its input through the gfx library
(`$code_root_dir/Code2/General/gfx/gfx_dll/gfx`), behind the `USE_GFX` CMake option (default on).
Work happens on the `gfx` branch; `main` stays the plain fork. The design follows ioq3-rs's
finished renderer_gfx (`$code_root_dir/Code2/Rust/ioq3-rs`, `gfx` branch, its `GFX_STATUS.md`).
This file is the current snapshot; the history is in git.

**Done.** Phases 0-6 are complete on Windows: every device and window gfx builds here works
(Backends), a full game on gl4 matches renderer_opengl1, and gl4 is as fast as renderer_opengl1.
The x11 window is Linux only and not built or run here (no Linux machine, no WSL). Phase 7
(renderergl2 features) is only done when asked. Merging `gfx` into `main` is the user's decision.

**Last run:** ioq3-rs gaps 1 (most of it) and 11 fixed in gfx ("Fixed in gfx"): `r_fullscreen`
is borderless fullscreen on the monitor (win32, glfw, sdl), `minimize` works, and the sdl window
gives the key left of 1 on a Nordic layout as the console key. gfx_glimp.c sets the mode after
creating the window and minimizes through gfx. Checked on the three windows and on d3d11, d3d12
and vk (validation layer clean, no TDR); screen-captured screenshots kept only from runs where the
game held the foreground throughout. Before it: gap 24 (d3d11 rings), gaps 12-14, the day's A/B
timedemo, gaps 26, 25, the win32 client size, 28, 9, 27 and steps 1-3.

**Next:** nothing in phases 0-6. Possible follow-ups, none required: the x11 window and the
Linux build on a Linux machine (`cmake/gfx.cmake`'s Linux branch follows gfx's CMakeLists.txt;
gfx's x11 window has the focus helper but no minimize events yet), gfx fix step 4 (read back,
gap 3) with a Linux build, phase 7 when asked.

## Phases

- [x] **0. Setup** - `CMakeLists.txt` (`USE_GFX`, includes), `cmake/gfx.cmake`,
  `cmake/renderer_gfx.cmake`, `code/renderer_gfx/*.c|h` (copies of `code/renderergl1/`),
  `code/renderer_gfx/gfx_public.h` (`GfxGetRefAPI`), `gfx_glimp.c` / `.h` (backend selection,
  gfxinfo lines), `tr_init.c` / `tr_local.h` copies (the two cvars, gfxinfo, `GfxGetRefAPI`),
  `code/client/cl_main.c` (`CL_InitRef`), `code/renderer_gfx/shaders/` (ioq3-rs's, plus
  `generate_shaders.ps1` / `.sh`), `.gitignore`, `.gitattributes`, README, this file.
  Checkpoint: both configurations build; with `USE_GFX` the client runs renderer_opengl1 - done.
- [x] **1. Window and device** - `code/renderer_gfx/gfx_glimp.c` (GLimp_* on gfx, the D3D ->
  win32 rule, desktop size / screen read on Windows, `GfxGLimp_GetWindow`),
  `gfx_glimp.h`, `gfx_public.h` (`GfxGLimp_GetWindow`), `gfx_gl.c` / `gfx_gl.h` (new: the qgl
  calls on gfx, the extension entry points), `tr_local.h` (qgl names declared as functions),
  `cmake/renderer_gfx.cmake` (gfx_gl.c in, `SDL_RENDERER_SOURCES` out), `code/sdl/sdl_input.c`
  (`IN_GfxWindowWithoutSDL`: no SDL video needed, `gfx_window_poll_events` each frame), README.
  Checkpoint: a cleared frame on gl4 - done.
- [x] **2. Input** - `code/gfx/gfx_input.c` / `.h` (new, compiled into the client by
  `cmake/renderer_gfx.cmake`): event ring, key map, chars, relative mouse with grab / release,
  focus with key release, resize, close; `code/sdl/sdl_input.c` (gfx dispatch, SDL device / quit
  events taken before gfx polls, `IN_IsConsoleKey` shared); README.
  Checkpoint: keys, text, mouse, focus, close on the sdl, glfw and win32 windows - done.
- [x] **3. 2D** - `code/renderer_gfx/gfx_draw.c` / `.h` (new: one program, rings, pipeline-key
  cache, bind caching, draw submission), `gfx_shader.c` / `.h` (new: `.gfx` parsing, program
  linking, shader folders), `gfx_gl.c` / `.h` (texture staging and upload, glTexSubImage2D,
  glDrawElements / glArrayElement / glBegin..glEnd, clears through a bound pipeline, MVP,
  uniforms with polygon offset / clip plane / depth remap), `gfx_glimp.c` (fatal error when the
  shaders cannot load), `cmake/renderer_gfx.cmake` (the two sources), README, `GFX_TESTING.md`
  (new). Checkpoint: console and menus on gl4 - done.
- [x] **4. 3D** - no new code (phase 3 ported ioq3-rs's final gfx_gl / gfx_draw). Checked against
  renderer_opengl1 on gl4: world, curves, sky, md3 models, fog, dynamic lights, marks, blob and
  stencil shadows, mirror and portal camera, `r_primitives 1` / `3`, `r_showtris` /
  `r_shownormals`, `vid_restart` in a map, `screenshot` / `screenshotJPEG` / video capture.
  Checkpoint: a full game on q3dm1 with four bots - done.
- [x] **5. Parity and speed** - `gfx_gl.c` / `.h` (`GfxGL_Overbright`; glReadPixels takes the
  overbright shift out), `gfx_glimp.c` (GLimp_EndFrame draws it before the swap). Frame
  comparisons windowed (both `r_ignorehwgamma` values) and fullscreen; `timedemo` on gl4 in
  Release at parity with renderer_opengl1 - done.
- [x] **6. All backends** - no new code. Every device on every window gfx makes it on, checked on
  q3dm0 (Backends) with the Khronos validation layer for vk and the TDR check after each GPU run;
  device / window switches with `vid_restart`; timedemo per device. x11 is Linux only - done on
  Windows.
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
  gles3 2048. Clears, viewport, depth range and scissor go to the device; textures and draws
  are below. Unit 1 counts as off unless `qglActiveTextureARB` is set (r_ext_multitexture), the
  pointer test standing for ioq3-rs's `multitexture` flag.
- **Textures** (`gfx_gl.c`, as ioq3-rs). glTexImage2D (renderergl1 always passes RGBA bytes)
  stages each level, reduced to the internal format (luminance repeats red, the RGB formats get
  alpha 1); the staged texture becomes one gfx R8G8B8A8 texture with all its levels when another
  texture is bound or uploaded, or at the next draw - renderergl1 sets filter and wrap after the
  images, so the gfx texture gets them. Filters map to gfx's min / mag / mip filterings (a texture
  with only level 0 samples it without mips), GL_CLAMP is edge clamping, anisotropy above 1 is
  passed on. A new image re-creates the gfx texture; glTexSubImage2D patches the staging, or
  uploads whole rows from row `yoffset` (RE_UploadCinematic's frames; part of a row: one warning,
  ioq3-rs gap 18). A parameter change after the gfx texture exists (r_textureMode at run time)
  cannot reach it (ioq3-rs gap 16): one warning. Where C differs: only one texture is ever staged,
  so the staging is one buffer in `gfxGl.staging` (grown to the largest image with ri.Malloc, kept
  until GLimp_Shutdown, also used to reduce a non-RGBA cinematic frame) instead of a Vec per
  texture.
- **Draws** (`gfx_draw.c`, the port of ioq3-rs's `gfx_draw.rs` as it ended, after its phases 4-6).
  One program (`generic.vs` / `generic.fs`: GL's texture environments of both units and the alpha
  test from the uniform block `Uniforms { mat4 uMVP; vec4 uParams; vec4 uClipPlane; vec4 uDepth }`;
  a unit that is off samples a 1x1 white texture) - one, because passes drawn with depthFunc
  GL_EQUAL must compute the exact positions of the pass that wrote the depth (ioq3-rs gap 21).
  One vertex format (`gfxVertex_t`: xyz f32x3, rgba u8x4, st0 / st1 f32x2 - 32 bytes);
  glDrawElements gathers the vertices its indices use (all of the locked range with
  glLockArraysEXT) from the client arrays - a disabled color / texture coordinate array gives the
  current glColor / glTexCoord - and rebases the indices; glBegin / glEnd turns strips, fans,
  polygons and quads into indexed triangle lists and line strips into line lists; glArrayElement
  feeds the immediate vertices. Vertices, u32 indices and uniforms go into three gfx stream
  buffers made once (16 / 8 / 8 MB rings wrapping when full, ioq3-rs gap 19; on every device,
  d3d11 included since gap 24 was fixed in gfx); identical consecutive uniforms are written once; indices
  carry the base vertex (gap 20). Pipelines are found by a 24-byte `gfxPipelineKey_t` (primitive,
  blend factors or off, color mask, depth test / write / func, stencil func / ref / masks / ops or
  off, cull, fill mode, scissor - gfx's enum values as bytes, no padding) in a 2048-slot
  open-addressing table hashed with FNV-1a; blend / depth-stencil / rasterizer states are shared
  through small arrays searched when a pipeline is made. A draw with the pipeline key or uniform
  offset of the one before skips the lookup and the gfx bind (reset each frame and by glClear's
  bind). A key gfx refuses is reported once and its draws dropped. Blend: a color factor's alpha
  half is the matching `*_ALPHA` factor (D3D11 refuses `*_COLOR` for alpha), GL_SRC_ALPHA_SATURATE
  is GL_SRC_ALPHA (gap 17). Where C differs: fixed tables and scratch arrays instead of HashMaps
  and growing Vecs (the draw scratch holds `3 * SHADER_MAX_INDEXES` indices and
  `SHADER_MAX_INDEXES` vertices, a larger draw is dropped with one warning), the immediate
  vertices are kept as `gfxVertex_t` (glEnd draws them in place, no copy), and GfxDraw_Draw
  rebases the caller's index scratch in place.
- **Polygon offset, clip plane, clip depth in the uniforms** (ioq3-rs gaps 4 / 5 / 23; in with
  phase 3 as the shaders already take them). glPolygonOffset with GL_POLYGON_OFFSET_FILL (and
  GL_FILL) is a constant depth bias the vertex shader adds to clip-space z:
  `(factor * 16 + units) * 2 / 2^24` (no slope term in a vertex shader). glClipPlane keeps the
  plane in eye coordinates (times the inverse of the modelview it is set under); each draw turns
  it into object coordinates (`plane * modelview`) for the fragment discard; off, it is
  (0, 0, 0, 1). `uDepth.y` is 1 on d3d11 / d3d12 / vk, where generic.vs makes z `(z + w) / 2`.
- **Clears obey GL's masks.** gfx clears with the masks and scissor test of the bound pipeline,
  so glClear binds the pipeline of GL's current color mask, depth mask and scissor test first
  (depth test on with GL_ALWAYS, as gfx only applies the depth mask with the test on).
- **GLimp on gfx** (`gfx_glimp.c`): the window is R8G8B8A8 + D24S8 (`r_ext_multisample` as the
  sample count), titled `CLIENT_WINDOW_TITLE`; `r_mode` as in the GL renderers, then the window's
  real size: gfx makes its windows dpi aware and grows a small one to most of the work area, so
  glConfig takes the size gfx gave (`gfx sized the window to W H`, gfx's client area on every
  window). `r_fullscreen` makes a desktop-sized window and turns it borderless fullscreen on its
  monitor (`gfx_window_set_mode`, the desktop's mode, no display mode change; 1920x1080 here on the
  win32, glfw and sdl windows; a window backend without it keeps the desktop-sized window).
  `r_mode -2` windowed is a desktop-sized window (desktop size from GetSystemMetrics on Windows,
  640x480 elsewhere for now; the win32 / glfw windows cut it down to the work area less their
  frame, 1904x993 here); toggling `r_fullscreen` runs `vid_restart`; `r_swapInterval` applied at
  creation; `minimize` minimizes the window (`gfx_window_minimize`). A D3D
  `r_gfxDevice` with another `r_gfxWindow` uses the win32 window (console message), as gfx makes
  D3D devices only there. A failed window falls back to the default backends (gl4 / sdl), then
  to `r_mode 3`. gfx's error reports go to the console as warnings (`gfx_error_callback`).
  sdl_glimp's cvars (`r_allowResize`, `r_centerWindow`, ...) stay registered, unused.
- **Gamma** (ioq3-rs gap 2): gfx has no gamma ramp. `deviceSupportsGamma` only with `r_gamma 1`
  and `r_ignorehwgamma 0`; GLimp_SetGamma keeps the ramp's overbright shift
  (`gfxGl.overbrightPasses`; any other ramp: a note that `r_gamma` needs `vid_restart`, and
  `tr.overbrightBits`). GLimp_EndFrame applies it before the swap (`GfxGL_Overbright`, as
  ioq3-rs's `gfx_gl_overbright`): the frame doubled once per overbright bit by a full-window
  triangle blended (GL_DST_COLOR, GL_ONE) with alpha, depth and stencil writes off - `dst + dst`,
  clamped as the ramp clamps. It goes through GfxDraw_Draw like any draw (its pipeline joins the
  key cache); GL's emulated state is not touched, the viewport is set back to GL's. Only
  fullscreen has overbright bits (R_SetColorMappings: never in a window), as with
  renderer_opengl1's hardware ramp.
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
  as K_MOUSE4 / 5, then K_AUX as the SDL formula), which is gfx's number plus one on every gfx
  window. Focus: IN_GfxInit takes `gfx_window_has_focus` (the win32 window takes the focus
  before the handler is set) and IN_Init's `com_unfocused` follows it; FOCUS_IN / FOCUS_OUT after
  that; gfx sends the key ups of the keys still down before FOCUS_OUT, as SDL does, so a KEY_PRESS
  is always a repeat. MINIMIZE / RESTORE set `com_minimized`. Resize: only a size other than the
  last one, only with `r_allowResize`, then the SDL path's delayed `vid_restart`; win32's 0x0
  size of a minimized window is ignored. CLOSE runs
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
- **glReadPixels from the screen** (ioq3-rs gap 3; here from phase 1, so screenshots can check
  each phase). On Windows, glReadPixels of GL_RGB / GL_RGBA bytes reads the gfx window's client
  area from the screen (GDI BitBlt + GetDIBits of this thread's `ioquake3` window, bottom-up rows
  as GL gives them): the frame presented last, and a covered window shows what covers it. Depth
  reads (flares) and other platforms get zeros (one warning). The callers apply the gamma ramp
  themselves (`R_GammaCorrect`), and the screen has the overbright passes in it where a GL back
  buffer has not, so glReadPixels shifts them out again (`v >> n`, as ioq3-rs): fullscreen
  screenshots and video frames match renderer_opengl1's.
- **Shaders.** `code/renderer_gfx/shaders/` is ioq3-rs's `shaders/` unchanged: `generic.vs.gfxs` /
  `generic.fs.gfxs` and the compiled `shaders/` (gl3, gl4, gles3), `shaders_d3/` (d3d11; d3d12
  loads it), `shaders_gles3_dark/` (compiled from a folder named so: gfx's compiler adds its
  Windows gles3 gamma fix), `shaders_vk/` (SPIR-V). `.gitattributes` marks `*.gfx` binary. A
  post-build step of ioquake3 copies the four folders next to it as `shaders/<dialect>/` (ioq3-rs's
  dist layout; also installed). `generate_shaders.ps1` / `.sh` build `wc_compiler_rs` with rustc
  into `build_shaders/` and recompile every folder (vk only with glslangValidator); run with
  ioq3-rs's glslang 16.6 they reproduce all 8 files byte for byte. `gfx_shader.c` (the port of
  ioq3-rs's `gfx_shader.rs`) reads them from `shaders/<dialect>/` in the client's folder
  (`Sys_BinaryPath`, called directly: renderer_gfx is in the client), then under `fs_basepath` -
  where ioq3-rs's second folder is its workspace's `shaders/`, which the C build does not need, as
  the post-build copy puts them next to the client. A missing or bad shader is a fatal GLimp_Init
  error naming the paths tried.
- **Builds** go to `build_gfx` (`USE_GFX` on) and `build_nogfx` (off), `build/` is main's. Only
  Visual Studio 18 2026 is installed here, so the generator is `-G "Visual Studio 18 2026" -A x64`
  (the skill's commands name VS 17 2022).

## Backends (Windows; this gfx build has devices gl3 gl4 gles3 vk d3d11 d3d12, windows win32 glfw sdl)

"2D": the console, the main menu and a cinematic draw like renderer_opengl1, `vid_restart` in
and out, clean quit; "+ input": phase 2's input check passes (keys, chars, console key, mouse
motion / buttons / wheel, focus loss, close); "3D": phase 4's checks and the full q3dm1 game
match renderer_opengl1, and fullscreen overbright (phase 5); "timedemo": Performance; "q3dm0":
the mirror and portal camera views, the video sign and the HUD draw like gl4's (phase 6), start
and quit cleanly. Input belongs to the window, not the device, so phase 2's check covers every
device on its window. Every GPU run was followed by the TDR check: no event 4101, d3d12 and vk
included. The x11 window and the Linux build are untested here (no Linux machine, no WSL;
`cmake/gfx.cmake`'s Linux branch follows gfx's CMakeLists.txt).

| | sdl | glfw | win32 |
| --- | --- | --- | --- |
| gl4 (baseline) | 2D + input + 3D + timedemo | 2D + input + q3dm0 | 2D + input + q3dm0 + timedemo |
| gl3 | q3dm0 + timedemo | q3dm0 | q3dm0 |
| gles3 | q3dm0 + timedemo (a little darker: `shaders_gles3_dark`) | q3dm0 (plain shaders) | q3dm0 (darker, as sdl) |
| d3d11 | uses win32 (gfx makes D3D only there; console message) | uses win32 | q3dm0 + timedemo |
| d3d12 | uses win32 | uses win32 | q3dm0 + timedemo (no debug layer: Graphics Tools missing) |
| vk | q3dm0 + timedemo, validation layer clean | q3dm0, same | q3dm0, same |

`vid_restart` switches devices and windows at run time (checked gl4 / sdl -> d3d11 / win32 -> vk
/ sdl -> gles3 / glfw -> gl4 / sdl in one map, with the validation layer on).

## gfx gaps (for the gfx maintainer)

Met here so far, each also in ioq3-rs's list (its `GFX_STATUS.md`, gaps 1-28, which are gfx's,
so the rest are expected here too):

- ioq3-rs gap 1, what is left of it: no display mode list (exclusive fullscreen at another
  resolution), desktop size query or window position - fullscreen is borderless at the desktop's
  mode, and `r_mode -2` windowed still takes the desktop size from GetSystemMetrics (Windows) or
  640x480.
- ioq3-rs gap 2: no gamma ramp (workaround: overbright as end-of-frame blend passes).
- ioq3-rs gap 3: no read back of the color or depth buffer (workaround on Windows: the color from
  the screen through GDI, one frame late and only what is visible; no depth, so `r_flares 1`'s
  depth test reads zeros - one warning - and flares show only within ~28 units of the eye).
- Worked around by design since phase 3, as in ioq3-rs (not each seen failing here): gaps 4
  (no depth bias) and 5 (no clip planes) - in the shaders; 16 (sampling fixed at texture
  creation); 17 (no GL_SRC_ALPHA_SATURATE); 18 (`gfx_set_texture_data` takes whole rows); 19
  (unsynchronized stream buffers: rings sized for many frames); 20 (no base vertex: indices
  rebased); 21 (no `invariant` / `precise`: one program); 22 (no push constants: a ring write and
  `gfx_bind_constant` per uniform change); 23 (GL clip depth on D3D / Vulkan: remapped in
  generic.vs).

New here (ioq3-rs has the same behaviour, unrecorded):

- `gfx_set_mouse_position` takes screen coordinates on the win32 window (SetCursorPos) and window
  coordinates on the sdl / glfw ones - gfx_input.c does not warp the cursor.

Two of ioq3-rs's gaps do not apply to a static build: gap 6 (clipboard, cursors,
`gfx_set_mouse_position`, window icon missing from gfx.dll's exports - the static library has
the whole C API) and gap 8 (only a Debug gfx in the gfx tree - gfx is compiled here in ioq3's
configuration). Compiling gfx's sources gives gfx's own `/W3` warnings (C4244 / C4267
conversions, `HAVE_STDINT_H` redefined by `config.h`, `NDEBUG` redefined in `gl4.c`, `APIENTRY`
redefined), as gfx's own build does.

### Fixed in gfx

Found here or by ioq3-rs, fixed in gfx's sources (`gfx_dll/gfx`, and the same in `gfx_benilla`
and wc_clean_new's `lib/gfx` copy), so ioq3-rs's gfx.dll gets them too once rebuilt:

- **Step 1** (gfx `ed29d8f`): the win32 window decodes WM_CHAR through the ANSI code page - its
  window class is ANSI (`RegisterClassExA`), so `wparam` is a code-page byte, not a UTF-16 unit
  (`€` comes as 0x80 on code page 1252); the old signed `char tmp[2]` UTF-16 decode dropped every
  byte of 0x80 and up with "invalid event codepoint". `å ä ö § €` now type on the win32 window.
  A raw mouse input without motion (a click or the wheel while grabbed) is skipped quietly
  instead of reported as "not RELATIVE or ABSOLUTE mouse move"; `GfxGLimp_Error`'s filter for it
  is gone. Characters of a double-byte or UTF-8 ANSI code page still need more than one WM_CHAR
  and are dropped (as before).
- **Step 2** (gfx `68aad90`): `vk_dtr` destroys the VMA allocator before the device, so VMA's
  memory blocks are freed - the validation layer's VUID-vkDestroyDevice-device-05137 (two leaked
  VkDeviceMemory) at every vk quit and `vid_restart` is gone. `VMA_ASSERT_LEAK` (in
  `vk_mem_alloc.cpp`) reports a leak found then through `GFX_ERROR` (a `gfx: vk: leak at device
  destruction` warning here) instead of asserting, so a gfx without `NDEBUG` does not abort.
  renderer_gfx deletes every gfx object it made before the device; none is reported.
- **Step 3** (gfx `6831cd8`): the sdl window numbers the mouse buttons past middle one lower than
  SDL (X1 = `GFX_MOUSE_BUTTON_4`), as the win32, glfw and x11 windows do - it gave SDL's own
  number (X1 = `GFX_MOUSE_BUTTON_5`), so one physical button was two gfx buttons. gfx_input.c's
  renumbering by window backend (`gfxInput.sdlWindow`) is gone: X1 / X2 are K_MOUSE4 / K_MOUSE5
  on every window, as on the SDL path. ioq3-rs's gfx_input.rs gets X1 = K_MOUSE4 on the sdl window
  with no change (it gave K_MOUSE5 there). Other gfx programs reading side buttons on the sdl
  window see them one lower (the wow clients name them by gfx's enum, so X1 is "Button4" on
  every window now; gfx_benilla's sdl window already numbered them so).
- **ioq3-rs gap 27** (gfx `e078365`): the vk device signals the submit and waits in the present
  with one semaphore per swapchain image (`present_semaphores`, indexed by the acquired image;
  made with the swapchain, kept and grown across recreations, destroyed in `vk_dtr`) instead of a
  render-finished semaphore per frame in flight, which the present engine could still hold - the
  validation layer's VUID-vkQueueSubmit-pSignalSemaphores-00067 at every vk start is gone, so a
  vk run with the layer now logs nothing.
- **ioq3-rs gap 9** (gfx `dad38d3`): the win32 window takes its client area as its size once it
  is made (`gfx_win32_create_window`), so the device is created at the size it draws at and
  `gfx_window_get_width` / `_height` give it before the window is shown, as the sdl / glfw
  windows do - they gave the size asked for (r_mode 4: 800x600 for a 1291x941 client) until the
  first WM_SIZE at show. `GfxGLimp_Win32ClientSize`, the client rectangle read from Windows, is
  gone (the window lookup stays for glReadPixels' screen read). Follow-up (gfx `5d99e25`): taking
  the size at creation left the first show's WM_SIZE without a change, so the resize event the
  win32 window sent then was gone - callers keeping their own size rely on it (wc_clean_new's
  render size); it is sent when the window is shown, or at the first event pump for a handler set
  later, as the sdl / glfw windows do. ioq3 ignores it (the size it already has).
- **ioq3-rs gap 28** (gfx `48fc2d3`, seen here on glfw): windows open inside the monitor's work
  area. The glfw window grew its client area to 95% of the work area, its frame came on top and
  the system's cascade placed it, so at r_mode 4 its bottom rows ran under the taskbar (1307x980
  client, frame to y 1064 on a 1032-pixel work area); it is now made hidden, shrunk (shape kept,
  never below the size asked for) until its frame covers that 95%, centred in the work area and
  shown at the end of creation as before - 1255x941, frame y 26-999. The win32 window, centred on
  the whole screen, fitted only with a thin taskbar along the bottom; it is centred in the work
  area now. A size asked for that does not fit the work area is cut down to it (below, the win32
  client size). sdl's window was inside already and is unchanged.
- **ioq3-rs gap 26** (gfx `ee04d0f`): a vk device that fails to initialize is deleted instead of
  crashing the process. `vk_dtr` ran on the half-made device and called `DeviceWaitIdle` and the
  other device functions before `load_device_proc` had loaded them (a call through NULL), so
  renderer_gfx's fallback never ran; it now destroys only the VkDevice (an instance function)
  when the device functions were never loaded, and frees command buffers only from a pool that
  was made. Checked with SwiftShader as the only Vulkan driver (refused then, gap 25): "no
  suitable physical device found", then `Falling back on r_gfxDevice gl4, r_gfxWindow sdl` on the
  sdl and win32 windows, validation layer clean.
- **ioq3-rs gap 25** (gfx `d7fe379`): the vk device takes a physical device without geometry
  shaders or anisotropic sampling when no device has both, so a software Vulkan (SwiftShader,
  which has no geometry shaders) is no longer refused; a device with both is still chosen first,
  and one without them drops `GFX_DEVICE_OPT1_GEOMETRY_SHADER` and reports `max_anisotropy` 1
  (renderer_gfx's anisotropy then stays off there). With Chrome's SwiftShader as the only driver
  vk draws q3dm0 (`devmap`, screenshot, `vid_restart`, quit) with the validation layer clean;
  with SwiftShader listed before the AMD driver the GPU is still chosen (1000 frames in 7.6 s,
  SwiftShader alone 39.5 s). So vk runs can start on the CPU now (GFX_TESTING.md, GPU safety).
- **The win32 window's client size** (gfx `d426403`): the size asked for is the client area, as on
  the sdl / glfw windows, and the client area is what grows (shape kept), the frame
  (AdjustWindowRectEx) counted in the 95% of the work area as on glfw - it grew the outer size and
  the frame came out of the client: r_mode 4 gave 1291x941 (1.37:1), now 1255x941 (4:3), glfw's
  size and place. With it the win32 and glfw windows cut a size that does not fit the work area
  with its frame (`r_fullscreen` / `r_mode -2`'s desktop-sized window) down to it: the system cut
  only the height to the screen and the frame pushed the client's bottom rows off the screen and
  under the taskbar (1920x1080 asked: 1920x1061 client to y 1092; before the client-size change
  win32 had 1904x1041 to y 1072), now 1904x993 inside the work area. Checked with a test program
  (win32 and glfw give the same size and place for 800x600, 1600x900, 1280x1024, 1920x1080,
  3840x2160) and in the game (gl4, d3d11, vk with the validation layer, the input check on win32;
  `r_mode -2` on win32 and glfw with the whole HUD in the screenshot). The timedemo row "gl4,
  win32 window" below was measured at the old 1904x1041.
- **ioq3-rs gaps 12-14** (gfx `92ef95c`): `gfx_window_has_focus` (and `gfx_dll_window_has_focus`)
  answers from a flag every backend keeps with or without a handler, set from the system when
  the win32 / glfw / sdl window is made or shown; `GFX_EVENT_MINIMIZE` / `GFX_EVENT_RESTORE`
  (after `GFX_EVENT_CLOSE`, the old values unchanged) come when that changes - win32 WM_SIZE, glfw's
  iconify callback, sdl's window events; x11 has none yet; on a focus loss the keys still down
  get their KEY_UP before FOCUS_OUT (`gfx_window_focus_event`, also used by x11), so a key held
  over the switch is not left down and its next press is a KEY_DOWN, and the glfw / sdl windows
  send a key up only for a key gfx has down (both release held keys themselves after a focus loss;
  glfw's came out twice). gfx_input.c lost `IN_GfxWindowHasOSFocus`, `IN_GfxReleaseKeys` and
  `keysDown`, and the 0x0 resize no longer sets `com_minimized`. Note: glfw's own focus flag (its
  starting value) means active in its thread, so a glfw window started behind another program
  reports the focus where win32 / sdl do not.
- **ioq3-rs gap 24** (gfx `32c0bfd`): d3d11 writes a `GFX_BUFFER_STREAM` buffer as a ring -
  appended to with D3D11_MAP_WRITE_NO_OVERWRITE, renamed with D3D11_MAP_WRITE_DISCARD only when a
  write starts it over at offset 0, the unsynchronized stream contract of the other devices -
  where every write discarded the whole buffer (a fresh copy per draw, the bytes outside the write
  undefined). A constant buffer is appended to only where `MapNoOverwriteOnDynamicConstantBuffer`
  allows (asked at device creation; Windows 8+ drivers have it); DYNAMIC buffers keep DISCARD.
  gfx_draw.c's d3d11 mode (one-draw buffers written at offset 0) is gone: d3d11 uses the rings of
  the other devices. Release timedemo 708 -> 856 fps; q3dm1 / the mirror / the portal camera match
  gl4 as closely as before (the portal view's spread is animation timing: a device against itself
  differs as much).
- **ioq3-rs gaps 1 (most of it) and 11** (gfx `ec9c846`): `gfx_window_set_mode` (windowed /
  borderless fullscreen on the window's monitor at the desktop's mode, ported from gfx_benilla)
  and `gfx_window_minimize` on the win32, glfw and sdl windows (a no-op on the others, x11 among
  them for now). GfxGLimp_SetMode turns an `r_fullscreen` window borderless fullscreen - the
  monitor's 1920x1080, where it was a 1904x993 framed window inside the work area - and
  GLimp_Minimize minimizes; `vid_restart` toggles between the two. Display modes, a desktop size
  query and window position are still missing (gfx gaps). The sdl window maps a keysym gfx has no
  name for by the key's position (gfx_benilla's scancode table): the key left of 1 on this
  Swedish layout is GFX_KEY_GRAVE, the console key, as on the win32 / glfw windows, where it was
  GFX_KEY_UNKNOWN; letters stay by layout there, as on the SDL path. Checked on the three windows
  (and d3d11, d3d12, vk with the validation layer on win32): fullscreen frames identical to the
  win32 gl4 one, `r_fullscreen` 0 -> 1 -> 0 with `vid_restart`, `minimize`, the key. The
  timedemo tables below measured `r_fullscreen 1` on win32 / glfw before this, at 1904x993.

### Recommended order for the gfx fixes

All of these are in gfx's shared sources, so ioq3-rs's gfx.dll has them too (only gap 3 was in
its `GFX_STATUS.md`); one fix in gfx serves both ioq3 versions. Cheapest and safest first; steps
1-3 are done, step 4 is open:

1. **Done: the win32 characters and the raw-input message** (Fixed in gfx, step 1).
2. **Done: the vk VMA leak** (Fixed in gfx, step 2).
3. **Done: the sdl window's mouse buttons** (Fixed in gfx, step 3).
4. **Read back (gap 3)** - not breaking (a new function), but the most work: every device, a
   gfx.dll export, ioq3-rs's bindings. On Windows it only gains flares' depth test (`r_flares`,
   off by default), as color comes from the screen; on Linux screenshots and video capture get
   zeros, so with a Linux build next, color read back comes first.

Not recommended: making `gfx_set_mouse_position` take window coordinates on win32. It breaks
callers passing screen coordinates, gfx's own included (`gfx_win32_ungrab_cursor` converts to
screen coordinates first; the d3d / gl_win32 / vk_win32 wrappers forward to it), to fix
something no caller here hits - document the difference instead.

## Open problems / accepted differences from renderer_opengl1

- renderer_gfx draws at the window's real (dpi-scaled) size, where renderer_opengl1 draws at the
  `r_mode` size and Windows scales its window up: the same picture, sharper.
- `r_textureMode` changed in a running game reaches only images loaded after a `vid_restart`
  (gap 16, one warning).
- On release of a grabbed mouse the cursor goes back to where the grab started, not to the window
  center as on the SDL path (gfx_set_mouse_position gap above; ioq3-rs does the same).
- `screenshot` / `screenshotJPEG` / video capture read the screen (Windows only; the frame
  presented last; a covered or dimmed window - a Windows Firewall prompt dims the desktop -
  captures that). Flares (`r_flares 1`, off by default) have no depth to test (gap 3).
- `r_primitives 3` draws untextured world surfaces: renderergl1's R_ArrayElementDiscrete passes
  0 / 1 instead of GL_TEXTURE0_ARB / GL_TEXTURE1_ARB to glMultiTexCoord2fARB, which renderer_gfx
  ignores as GL does (ioq3-rs: the same; renderer_opengl1 crashes in the driver there).
- `r_primitives 1` on renderer_gfx matches the default path and renderer_opengl1's default;
  renderer_opengl1's own `r_primitives 1` frame differs from its default on this AMD driver
  (darker, bluer lightmapped walls) - the driver's, not renderer_gfx's.
- gles3 is a little darker than gl3 / gl4 on the sdl and win32 windows: gfx's compiler adds its
  Windows gles3 gamma fix to `shaders_gles3_dark` (glfw's gles3 uses the plain shaders), as in
  ioq3-rs.
- d3d11 is a little slower than d3d12 / gl4 (856 fps against ~905-930 in Release timedemo).
- Not checked: md4 / iqm models (baseq3 has none) and the x11 window (Linux).
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
- Phase 3 check: windowed `+set r_ignorehwgamma 1 +wait 200 +togglemenu +wait 100 +screenshot
  +cinematic idlogo.RoQ +wait 150 +screenshot +devmap q3dm1 +wait 300 +screenshot +togglemenu
  +wait 60 +screenshot`, once with renderer_gfx and once with `+set cl_renderer opengl1`, the TGAs
  compared side by side (renderer_opengl1's 800x600 scaled to renderer_gfx's 1307x980); a fresh
  home starts on the CD key menu, `togglemenu` opens the main menu. The window switch:
  `+togglemenu +wait 60 +screenshot +seta r_gfxWindow win32 +vid_restart +wait 150 +togglemenu
  +wait 60 +screenshot`, then glfw. How to run is in [GFX_TESTING.md](GFX_TESTING.md).
- Phase 4 / 5 checks (both renderers, `screenshotJPEG`, compared scaled side by side; ioq3-rs's
  views): `+devmap q3dm0 +wait 200 +setviewpos -1148 -1100 30 270` (mirror), `-1152 -1700 30 270`
  (portal camera); `+set bot_enable 0 +devmap q3dm12 +wait 150 +noclip +setviewpos -660 -720
  -200 270` (fog); an `exec`ed cfg (copied into `<home>/baseq3`) with `give all`, `weapon 8`,
  `+attack` (plasma lights and marks; `+lookdown` aims at the floor), `cg_thirdperson 1` and
  `cg_shadows 2` (stencil shadows - the idle animation moves them, so compare a few shots), a
  full game with `addbot sarge 2` x4, `r_primitives 1` / `3`, `r_showtris 1`, `r_shownormals 1`,
  `r_vertexlight 1` + `vid_restart`; fullscreen overbright with `+set r_fullscreen 1 +set r_mode
  -2 +set r_ignorehwgamma 0`. `video <name>` only works while a demo plays (`demo four`).
- The command line is cut at 1024 characters (`main`'s `commandLine[MAX_STRING_CHARS]` in
  sys_main.c): long scripts go into a cfg, run with `+exec`. `+set nextdemo quit` ends a
  timedemo run.
- Screenshots read the screen: the check script keeps the game window topmost
  (SetWindowPos HWND_TOPMOST, no activation) so the terminal does not cover it.
- The game console goes to stderr and `qconsole.log`; gfx's `[gfx] using ...` lines go to
  stdout (ioquake3 is a GUI program on Windows: `Start-Process -RedirectStandardOutput`).

## Performance (timedemo)

`timedemo 1`, `demo four` (pak8's four.dm_68, 1260 frames), fullscreen 1920x1080 (`r_mode -2`,
one overbright bit), `r_swapInterval 0`, `com_maxfps 0`, Release build, cgame / ui as dlls
(`sv_pure 0`, `vm_cgame 0`, `vm_ui 0`), `+set nextdemo quit`, this machine (AMD GPU), sdl window
unless noted, renderers run alternately:

| renderer | fps |
| --- | --- |
| renderer_opengl1 | 907 - 952 (4 runs) |
| renderer_gfx gl4 | 910 - 933 (4 runs) |
| renderer_gfx gl4, win32 window | 942 |
| renderer_gfx gl3 | 897 |
| renderer_gfx gles3 | 902 |
| renderer_gfx vk | 950 |
| renderer_gfx d3d12 (win32) | 916 |
| renderer_gfx d3d11 (win32), one-draw buffers (before gap 24 was fixed) | 692 |
| renderer_gfx d3d11 (win32), rings (gap 24 fixed, 5 runs) | 841 - 872 |

gl4 is at parity with renderer_opengl1: both are CPU bound at ~1.1 ms a frame, and the runs
spread by a few percent either way; the other devices are one run each. No Release-gfx step was needed (ioq3-rs's gap 8): gfx is
compiled in the build's configuration.

Today's gfx fixes against the gfx and ioq3 before the first of them (gfx `4463b85`, ioq3
`4b08076d`, built from `git archive` into a scratch folder), Release, the settings above, 5 runs
each, before and after alternating, means (runs spread ±3-5%):

| device / window | drawn before | drawn after | fps before | fps after |
| --- | --- | --- | --- | --- |
| gl4 / sdl | 1920x1080 | 1920x1080 | 935 | 926 |
| gl3 / sdl | 1920x1080 | 1920x1080 | 895 | 910 |
| gles3 / sdl | 1920x1080 | 1920x1080 | 899 | 911 |
| vk / sdl | 1920x1080 | 1920x1080 | 926 | 926 |
| gl4 / win32 | 1904x1041 | 1904x993 | 915 | 928 |
| d3d11 / win32 | 1904x1041 | 1904x993 | 675 | 697 |
| d3d12 / win32 | 1904x1041 | 1904x993 | 889 | 888 |
| vk / win32 | 1904x1041 | 1904x993 | 916 | 932 |
| gl4 / glfw | 1920x1061 | 1904x993 | 932 | 929 |
| vk / glfw | 1920x1061 | 1904x993 | 926 | 934 |

No device or window got slower: the sdl rows, drawn at the same size, move -0.9% to +1.6%; the
win32 / glfw desktop-sized windows draw ~5% fewer pixels now (the work-area cut), their small gains
within the same spread. No TDR. Gaps 12-14 came after this run; they touch only events.

Gap 24 (d3d11 stream buffers as rings), the same settings, desktop-sized win32 window, 5 runs each:
d3d11 with the old one-draw buffers (new gfx, every write at offset 0 still discards) 701 - 714,
mean 708; with the rings 841 - 872, mean 856 (+21%); d3d12 905, gl4 928 in the same session.
