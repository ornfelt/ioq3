# ioq3 on gfx - status

ioq3 drawing, opening its window and reading its input through the gfx library
(`$code_root_dir/Code2/General/gfx/gfx_dll/gfx`), behind the `USE_GFX` CMake option (default on).
Work happens on the `gfx` branch; `main` stays the plain fork. The design follows ioq3-rs's
finished renderer_gfx (`$code_root_dir/Code2/Rust/ioq3-rs`, `gfx` branch, its `GFX_STATUS.md`).
This file is the current snapshot; the history is in git.

**Last run:** phase 1. renderer_gfx no longer compiles sdl_glimp.c / sdl_gamma.c: `gfx_glimp.c`
creates the gfx window and device for `r_gfxDevice` / `r_gfxWindow` (with the fallbacks), swaps,
shuts down and hands the window to the client (`GfxGLimp_GetWindow()`); `gfx_gl.c` / `.h` are the
back end's `qgl*` calls on gfx (GL state kept as data, clears / viewport / scissor on the device,
draws and uploads recorded and dropped), and `glReadPixels` reads the window from the screen.
`sdl_input.c` runs without SDL video under a non-SDL gfx window and pumps its events. Checked on
gl4: `r_clear 1` frames are the clear color (255,0,128) on the sdl, glfw and win32 windows
(screenshots), `vid_restart` switches between them, fullscreen and a q3dm1 load run and quit
cleanly, an unknown `r_gfxDevice` falls back to gl4, and `cl_renderer opengl1` still plays q3dm1.

**Next:** phase 2 - `code/gfx/gfx_input.c` / `.h` (new, client, `USE_GFX`): gfx's event handler
into a fixed ring, key map to `K_*`, char events, relative mouse with grab / release, focus,
resize, close; `sdl_input.c` dispatches `IN_ProcessEvents`, `IN_ActivateMouse` /
`IN_DeactivateMouse`, `IN_Frame`'s focus test and `IN_Init` to it while `GfxGLimp_GetWindow()`
is not NULL. Read ioq3-rs's `code/gfx/gfx_input.rs`, its "Input on gfx" decision and its phase 2
commit (`200764d`) first.

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
- [ ] **2. Input** - `code/gfx/gfx_input.c` / `.h` (new; compiled into the client under
  `USE_GFX`): event ring filled by gfx's handler, key map to `K_*`, chars, relative mouse with
  grab / release, focus, resize, close; `sdl_input.c` dispatches `IN_ProcessEvents`,
  `IN_ActivateMouse` / `IN_DeactivateMouse`, `IN_Frame`'s focus test and `IN_Init` to it when
  `GfxGLimp_GetWindow()` is not NULL (`com_unfocused` is 1 until then: SDL_GetWindowFlags of no
  window); SDL keeps joysticks / gamepads and the audio, and its gamepad hot-plug / SDL_QUIT
  events must be taken before gfx's SDL window polls SDL's queue.
  Checkpoint: keys, text, mouse, focus, close on the sdl, glfw and win32 windows.
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
- **Client input on a gfx window** (`sdl_input.c`, under `USE_GFX`): `IN_GfxWindowWithoutSDL()`
  is the gfx window when it is not an SDL one; then `IN_Init` does not require SDL video, and
  `IN_Frame` calls `gfx_window_poll_events` to keep the window alive (its events are read in
  phase 2). An SDL gfx window shares the client's SDL2, so its events reach `IN_ProcessEvents`
  as the GL renderers' window does.
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

Phase 1 only clears (`r_clear 1`), so "clears" means: window and device made, the clear color
shown, `vid_restart` in and out, clean quit. The other devices wait for phase 6 (GPU safety:
each feature on gl4 first). The x11 window and the Linux build are untested here (no Linux
machine; `cmake/gfx.cmake`'s Linux branch follows gfx's CMakeLists.txt).

| | sdl | glfw | win32 |
| --- | --- | --- | --- |
| gl4 (baseline) | clears | clears | clears |
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
- ioq3-rs gap 28, seen on glfw too: at r_mode 4 the glfw window's client area (1307x980) runs
  under the taskbar - a screen capture of it shows the taskbar over its bottom rows. ioq3-rs
  recorded this for the win32 window only.

Two of ioq3-rs's gaps do not apply to a static build: gap 6 (clipboard, cursors,
`gfx_set_mouse_position`, window icon missing from gfx.dll's exports - the static library has
the whole C API) and gap 8 (only a Debug gfx in the gfx tree - gfx is compiled here in ioq3's
configuration). Compiling gfx's sources gives gfx's own `/W3` warnings (C4244 / C4267
conversions, `HAVE_STDINT_H` redefined by `config.h`, `NDEBUG` redefined in `gl4.c`, `APIENTRY`
redefined), as gfx's own build does.

## Open problems / accepted differences from renderer_opengl1

- renderer_gfx draws nothing but clears until phase 3 (2D) / 4 (3D); `cl_renderer opengl1` plays.
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
- The game console goes to stderr and `qconsole.log`; gfx's `[gfx] using ...` lines go to
  stdout (ioquake3 is a GUI program on Windows: `Start-Process -RedirectStandardOutput`).

## Performance (timedemo)

Not measured yet (phase 5).
