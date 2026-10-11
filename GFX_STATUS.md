# ioq3 on gfx - status

ioq3 drawing, opening its window and reading its input through the gfx library
(`$code_root_dir/Code2/General/gfx/gfx_dll/gfx`), behind the `USE_GFX` CMake option (default on).
Work happens on the `gfx` branch; `main` stays the plain fork. The design follows ioq3-rs's
finished renderer_gfx (`$code_root_dir/Code2/Rust/ioq3-rs`, `gfx` branch, its `GFX_STATUS.md`).
This file is the current snapshot; the history is in git.

**Last run:** phase 0. The `gfx` branch; `USE_GFX`; gfx built from source as a static library
(`cmake/gfx.cmake`); `code/renderer_gfx/` (renderergl1's files copied, `gfx_glimp.c` with the
backend selection) as a static library linked into ioquake3 (`cmake/renderer_gfx.cmake`);
`CL_InitRef` calling `GfxGetRefAPI` for `cl_renderer gfx` (the default); the `r_gfxDevice` /
`r_gfxWindow` cvars; ioq3-rs's shaders with their post-build copy and generate scripts. Both
configurations build (RelWithDebInfo, VS 18 2026, no new warnings); with `USE_GFX` the client
ran q3dm1 on `cl_renderer opengl1`, on `cl_renderer gfx` (still sdl_glimp's GL window and
renderergl1's GL calls), and an unknown `cl_renderer` fell back to renderer_gfx; gfxinfo lists
the gfx backends and an unknown `r_gfxWindow` falls back to sdl.

**Next:** phase 1 - `code/renderer_gfx/gfx_glimp.c` takes over GLimp_* from sdl_glimp.c /
sdl_gamma.c (window and device for the chosen backends, modes, fullscreen, swap, the window
handoff `GfxGLimp_GetWindow()`), and `gfx_gl.c` / `.h` stand in for the qgl pointers, until a
cleared frame shows on gl4. Read ioq3-rs's `gfx_glimp.rs`, `gfx_gl.rs` and its phase 1 commit
(`d9ad84e`) first.

## Phases

- [x] **0. Setup** - `CMakeLists.txt` (`USE_GFX`, includes), `cmake/gfx.cmake`,
  `cmake/renderer_gfx.cmake`, `code/renderer_gfx/*.c|h` (copies of `code/renderergl1/`),
  `code/renderer_gfx/gfx_public.h` (`GfxGetRefAPI`), `gfx_glimp.c` / `.h` (backend selection,
  gfxinfo lines), `tr_init.c` / `tr_local.h` copies (the two cvars, gfxinfo, `GfxGetRefAPI`),
  `code/client/cl_main.c` (`CL_InitRef`), `code/renderer_gfx/shaders/` (ioq3-rs's, plus
  `generate_shaders.ps1` / `.sh`), `.gitignore`, `.gitattributes`, README, this file.
  Checkpoint: both configurations build; with `USE_GFX` the client runs renderer_opengl1 - done.
- [ ] **1. Window and device** - `gfx_glimp.c` replaces `sdl_glimp.c` / `sdl_gamma.c` in
  `cmake/renderer_gfx.cmake`: GLimp_Init / Shutdown / EndFrame / SetGamma / Minimize /
  LogComment on gfx (R8G8B8A8 + D24S8, `r_ext_multisample` samples, `r_mode`, `r_fullscreen` as
  a desktop-sized window, `r_swapInterval`, the real window size), `GfxGLimp_GetWindow()`;
  `gfx_gl.c` / `.h` provide the `qgl*` names renderergl1 uses (68 of them; the `QGL_*_PROCS`
  pointers of `renderercommon/qgl.h` and the multitexture / lock-arrays ones), first as state
  plus clear / viewport / scissor; the extension tests in `tr_init.c` read gfx_gl.
  `sdl_input.c`: `IN_Init` gets no SDL window while renderer_gfx runs, so it must not need one.
  Checkpoint: a cleared frame on gl4.
- [ ] **2. Input** - `code/gfx/gfx_input.c` / `.h` (new; compiled into the client under
  `USE_GFX`): event ring filled by gfx's handler, key map to `K_*`, chars, relative mouse with
  grab / release, focus, resize, close; `sdl_input.c` dispatches `IN_ProcessEvents`,
  `IN_ActivateMouse` / `IN_DeactivateMouse`, `IN_Frame`'s focus test and `IN_Init` to it when
  `GfxGLimp_GetWindow()` is not NULL; SDL keeps joysticks / gamepads and the audio.
  Checkpoint: keys, text, mouse, focus, close on the sdl, glfw and win32 windows.
- [ ] **3. 2D** - `gfx_draw.c` / `.h` (programs, rings, pipeline-key cache, draw submission),
  `gfx_shader.c` (loading `shaders/<dialect>/*.gfx`), `gfx_gl.c` (texture staging and upload,
  glTexSubImage2D, glDrawElements / glArrayElement / glBegin..glEnd, clears through a bound
  pipeline). Checkpoint: console and menus on gl4; `GFX_TESTING.md` from here on.
- [ ] **4. 3D** - polygon offset and clip plane as uniforms, the one generic program, bind
  caching, world / curves / models / sky / fog / dlights / marks / portals / mirrors,
  `screenshot` / `screenshotJPEG` / video capture from the screen. Checkpoint: a full game on
  q3dm1 with bots.
- [ ] **5. Parity and speed** - frame comparisons against renderer_opengl1, overbright as blend
  passes, `timedemo` on gl4 in a Release build.
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
  RelWithDebInfo build, so `NDEBUG` and no `GL_CALL_DEBUG`). Code includes `"gfx/window.h"` &
  co. (`${GFX_DIR}/include` is the public include dir). No gfx.dll, no `gfx_dll_*` wrapper - and
  so ioq3-rs's Release-gfx step (its gap 8) is not needed here. GLFW: FetchContent of 3.4 on
  Windows as gfx does, from the gfx build's checkout (`build/_deps/glfw-src`) when it exists,
  so no download; `find_package(glfw3)` on Linux. The gfx tree is only read.
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
  the client's input share one copy of gfx. The client keeps `USE_RENDERER_DLOPEN`.
- **`CL_InitRef` hook** (`cl_main.c`, under `USE_GFX`): `cl_renderer` defaults to `gfx`;
  `cl_renderer gfx` calls renderer_gfx's `GfxGetRefAPI` (`code/renderer_gfx/gfx_public.h`; its own
  name, as the client's dlopen code declares a local `GetRefAPI`), any other name loads
  `renderer_<name>` as before, and a dll that fails to load resets `cl_renderer` to `gfx` and
  uses the built-in renderer (`using the built-in renderer_gfx`). The `main` code path is kept
  unchanged under `#else`. `refexport_t` / `refimport_t` are unchanged.
- **renderer_gfx = renderergl1's files, copied.** `code/renderer_gfx/` holds a copy of every
  `code/renderergl1/*.c` / `*.h` (first line `/* renderer_gfx: a copy of renderergl1/<file> */`);
  changes in them are marked `// renderer_gfx:`. So far: `tr_init.c` (`r_gfxDevice` /
  `r_gfxWindow`, gfxinfo's gfx lines, `GfxGetRefAPI`), `tr_local.h` (the two cvars). Until phase
  1 the window and the GL calls are still sdl_glimp.c's (`SDL_RENDERER_SOURCES`), so
  `cl_renderer gfx` is renderergl1 linked statically.
- **Backend cvars** (`gfx_glimp.c`): `r_gfxDevice` (default `gl4`) and `r_gfxWindow` (default
  `sdl`), archived and latched like `r_mode`; names are gfx's own (`gfx_device_backend_name` /
  `gfx_window_backend_name`, case-insensitive); an unknown name or one this gfx build lacks
  (`gfx_has_*_backend`) falls back to the default with a console warning. gfxinfo prints the
  built devices / windows and the selected pair.
- **Shaders.** `code/renderer_gfx/shaders/` is ioq3-rs's `shaders/` unchanged: `generic.vs.gfxs` /
  `generic.fs.gfxs` and the compiled `shaders/` (gl3, gl4, gles3), `shaders_d3/` (d3d11; d3d12
  loads it), `shaders_gles3_dark/` (compiled from a folder named so: gfx's compiler adds its
  Windows gles3 gamma fix), `shaders_vk/` (SPIR-V). `.gitattributes` marks `*.gfx` binary. A
  post-build step of ioquake3 copies the four folders next to it as `shaders/<dialect>/` (ioq3-rs's
  dist layout; also installed). `generate_shaders.ps1` / `.sh` build `wc_compiler_rs` with rustc
  into `build_shaders/` and recompile every folder (vk only with glslangValidator); run with
  ioq3-rs's glslang 16.6 they reproduce all 8 files byte for byte.
- **Builds** go to `build_gfx` (`USE_GFX` on) and `build_nogfx` (off), `build/` is main's. Only
  Visual Studio 18 2026 is installed here, so the generator is `-G "Visual Studio 18 2026" -A x64`
  (the skill's commands name VS 17 2022).

## Backends (Windows; this gfx build has devices gl3 gl4 gles3 vk d3d11 d3d12, windows win32 glfw sdl)

Nothing drawn through gfx yet (phase 1). The x11 window and the Linux build are untested here
(no Linux machine; `cmake/gfx.cmake`'s Linux branch follows gfx's CMakeLists.txt).

| | sdl | glfw | win32 |
| --- | --- | --- | --- |
| gl4 (baseline) | - | - | - |
| gl3 | - | - | - |
| gles3 | - | - | - |
| d3d11 | - | - | - |
| d3d12 | - | - | - |
| vk | - | - | - |

## gfx gaps (for the gfx maintainer)

None new so far. ioq3-rs's list (its `GFX_STATUS.md`, gaps 1-28) is gfx's, so expect them here
too; each is recorded here, with its ioq3-rs number, once this tree runs into it. Two of them do
not apply to a static build: gap 6 (clipboard, cursors, `gfx_set_mouse_position`, window icon
missing from gfx.dll's exports - the static library has the whole C API) and gap 8 (only a Debug
gfx in the gfx tree - gfx is compiled here in ioq3's configuration). Compiling gfx's sources
gives gfx's own `/W3` warnings (C4244 / C4267 conversions, `HAVE_STDINT_H` redefined by
`config.h`, `NDEBUG` redefined in `gl4.c`, `APIENTRY` redefined), as gfx's own build does.

## Open problems / accepted differences from renderer_opengl1

- Not ioq3-gfx: the RelWithDebInfo client (main's `build/` too) crashes with an access
  violation in JIT-compiled QVM code right after `ui.qvm` loads, whatever the renderer. Run
  with `+set vm_ui 1 +set vm_cgame 1 +set vm_game 1` (the interpreter) or the dll modules
  (`+set sv_pure 0 +set vm_ui 0 +set vm_cgame 0 +set vm_game 0`).

## Testing notes

- Fresh home per run: `+set fs_homepath <dir>`; game data hard-linked from `build/`'s
  `baseq3/pak0-8.pk3` into `build_gfx/RelWithDebInfo/baseq3/`.
- Phase 0 check: `ioquake3 +set fs_homepath <dir> +set vm_ui 1 +set vm_cgame 1 +set vm_game 1
  +set logfile 2 +set r_mode 4 +devmap q3dm1 +wait 200 +screenshot +wait 20 +quit` with
  `+set cl_renderer opengl1`, without (renderer_gfx), with `+set r_gfxWindow bogus` and with
  `+set cl_renderer nosuch`; read `<dir>/baseq3/qconsole.log`, look at `screenshots/shot0000.tga`.
- The game console goes to stderr and `qconsole.log`; gfx's `[gfx] using ...` lines go to
  stdout (ioquake3 is a GUI program on Windows: `Start-Process -RedirectStandardOutput`).

## Performance (timedemo)

Not measured yet (phase 5).
