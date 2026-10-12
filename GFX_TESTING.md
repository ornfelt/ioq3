# Testing renderer_gfx on the gfx backends

How to build ioq3 with and without the gfx library, run renderer_gfx, pick its gfx device and
window backend, and check the GPU afterwards. What works on which backend, and why, is in
[GFX_STATUS.md](GFX_STATUS.md). The commands are PowerShell on Windows; Linux notes are at the
end.

## Build

`code_root_dir` must be set: `cmake/gfx.cmake` compiles gfx from
`$env:code_root_dir\Code2\General\gfx\gfx_dll\gfx` (`-DGFX_DIR=<folder>` picks another gfx tree).
gfx is compiled as a static library in the configuration ioq3 is built in, so a RelWithDebInfo or
Release build gets a release gfx - no gfx.dll, no separate gfx build.

```powershell
$W = "$env:code_root_dir/Code2/C/ioq3"
cmake -S $W -B $W/build_gfx -G "Visual Studio 18 2026" -A x64                 # USE_GFX on (the default)
cmake --build $W/build_gfx --config RelWithDebInfo
cmake -S $W -B $W/build_nogfx -G "Visual Studio 18 2026" -A x64 -DUSE_GFX=OFF  # what main builds
cmake --build $W/build_nogfx --config RelWithDebInfo
```

`build_gfx\RelWithDebInfo\` then holds `ioquake3.exe` (with renderer_gfx and gfx linked in),
`renderer_opengl1.dll` / `renderer_opengl2.dll`, `SDL2.dll` and `shaders\<dialect>\*.gfx` (the
post-build copy of `code\renderer_gfx\shaders\`). Use `--config Release` for timings.

Game data: hard-link or copy `pak0.pk3` .. `pak8.pk3` into `build_gfx\RelWithDebInfo\baseq3\`
(from `build\`'s `baseq3`, a `main` build, or a `2024\baseq3` folder).

## Run

```powershell
$B = "$env:code_root_dir/Code2/C/ioq3/build_gfx/RelWithDebInfo"
& "$B/ioquake3.exe" +set fs_homepath "$env:TEMP\ioq3-gfx-test" +set vm_ui 1 +set vm_cgame 1 +set vm_game 1 `
    +set r_fullscreen 0 +set r_mode 4 +set r_gfxDevice gl4 +set r_gfxWindow sdl +devmap q3dm1
```

- `fs_homepath` keeps the run's `q3config.cfg` away from your normal one: `cl_renderer`, the two
  backend cvars, `r_fullscreen` and the `vm_` cvars are archived.
- `vm_* 1` runs the QVMs in the interpreter. The RelWithDebInfo client (main's too) crashes in
  JIT-compiled QVM code right after `ui.qvm` loads, whatever the renderer (GFX_STATUS.md, open
  problems).
- `r_fullscreen` defaults to 1: borderless fullscreen on the monitor, at the desktop's mode (on
  the win32, glfw and sdl windows). A screenshot reads the screen, so keep the game in front while
  it is taken (a covered window captures what covers it). `r_fullscreen 0 +set r_mode 4` gives a
  window; gfx grows a small window to most of the screen, so it ends up bigger than the mode
  (`gfx sized the window to W H` in the console).
- With `USE_GFX` the default `cl_renderer` is `gfx`. `+set cl_renderer opengl1` (or `opengl2`)
  loads a GL renderer dll instead, for comparisons; it ignores the gfx cvars.

## Picking the backends

| cvar | values | default |
| --- | --- | --- |
| `r_gfxDevice` | `gl4`, `gl3`, `gles3`, `vk`, `d3d11`, `d3d12` | `gl4` |
| `r_gfxWindow` | `sdl`, `glfw`, `win32` (Windows), `x11` (Linux) | `sdl` |

- Both are latched: in a running game type `r_gfxWindow glfw` then `vid_restart` in the console
  (Shift+Esc). On the command line, a backend switch after the start needs `+seta` (every `+set`
  is applied at startup, whatever its position):
  `+wait 100 +screenshot +seta r_gfxWindow win32 +vid_restart +wait 150 +screenshot`.
- gfx makes D3D devices only on its win32 window: `d3d11` / `d3d12` with another window use
  win32 and say so. An unknown name or an unbuilt backend falls back to gl4 / sdl with a warning.
- The console prints `gfx device: <device>, window: <window>`; `gfxinfo` lists the backends this
  gfx build has. gfx's own `[gfx] using ...` lines go to stdout, which a GUI program only shows
  when redirected (`Start-Process -RedirectStandardOutput`).
- **GL4 first.** Every feature is checked on gl4 before any other device; vk and d3d12 then only
  as "Vulkan and D3D12: GPU safety" says. What works where is GFX_STATUS.md's Backends table.
- gles3 is a little darker than gl3 / gl4 on the sdl and win32 windows (`shaders_gles3_dark`,
  gfx's Windows gles3 gamma fix); glfw's gles3 uses the plain shaders.

## Scripted checks

A check is a short run that ends with `+quit`, with a fresh home, reading `qconsole.log`
(`+set logfile 2`) and the screenshots (`<home>\baseq3\screenshots\shot*.tga`) afterwards:

```powershell
$H = "$env:TEMP\ioq3-gfx-check"; Remove-Item $H -Recurse -Force -ErrorAction SilentlyContinue
$p = Start-Process "$B/ioquake3.exe" -WorkingDirectory $B -PassThru `
    -RedirectStandardOutput "$H-stdout.txt" -RedirectStandardError "$H-stderr.txt" `
    -ArgumentList "+set fs_homepath $H +set vm_ui 1 +set vm_cgame 1 +set vm_game 1 +set logfile 2 +set r_mode 4 +set r_fullscreen 0 +set r_ignorehwgamma 1 +wait 200 +togglemenu +wait 60 +screenshot +devmap q3dm1 +wait 300 +screenshot +quit"
$p.WaitForExit(90000)
```

- The same script with `+set cl_renderer opengl1` gives renderer_opengl1's frames to compare. It
  draws at the `r_mode` size (800x600) where renderer_gfx draws at the window's real size, so
  scale one to the other before comparing.
- `r_ignorehwgamma 1` makes renderer_opengl1 use the same light scale as renderer_gfx in a
  window; with its hardware gamma ramp it halves the lightmaps and its capture misses the ramp.
- Screenshots are read from the screen (gfx has no read back): keep the window uncovered while
  the script runs, or the capture shows what covers it.
- `+togglemenu` opens the main menu (a fresh home starts on the CD key menu), `+cinematic
  idlogo.RoQ` plays a cinematic through RE_StretchRaw.
- Injected input for the input checks: GFX_STATUS.md, testing notes.
- The command line is cut at 1024 characters: put a longer script into a cfg, copy it into
  `<home>\baseq3\` and run it with `+exec <name>.cfg` (`wait` works there as on the command line).
- A Windows Firewall prompt for a newly built `ioquake3.exe` dims the whole desktop, and so the
  screen-read screenshots: close it (Cancel is enough) before comparing frames.
- The views and cfgs of the 3D checks (mirror, portal camera, fog, dynamic lights, marks,
  shadows, a full game with bots) are in GFX_STATUS.md, testing notes.

## Timedemo

In a Release build (`cmake --build $W/build_gfx --config Release`, the paks in
`build_gfx\Release\baseq3\`), fullscreen at the desktop size, no frame cap, the game modules as
dlls, quitting when the demo ends:

```powershell
$R = "$env:code_root_dir/Code2/C/ioq3/build_gfx/Release"
foreach ($renderer in "gfx", "opengl1") {
    $H = "$env:TEMP\ioq3-timedemo-$renderer"; Remove-Item $H -Recurse -Force -ErrorAction SilentlyContinue
    $p = Start-Process "$R/ioquake3.exe" -WorkingDirectory $R -PassThru -ArgumentList "+set fs_homepath $H +set cl_renderer $renderer +set logfile 2 +set sv_pure 0 +set vm_cgame 0 +set vm_ui 0 +set r_fullscreen 1 +set r_mode -2 +set r_swapInterval 0 +set com_maxfps 0 +set timedemo 1 +set nextdemo quit +demo four"
    $p.WaitForExit(120000) | Out-Null
    Select-String -Path "$H\baseq3\qconsole.log" -Pattern "fps"
}
```

Run the renderers alternately a few times; the numbers are in GFX_STATUS.md, Performance.

## Vulkan and D3D12: GPU safety

Faulty Vulkan or D3D12 use can hang or reset the GPU driver, not only the game. Test them only
after the same feature works on gl4, and then:

- run a new vk change on SwiftShader first, a Vulkan on the CPU that cannot touch the GPU. Chrome
  and Edge ship it; point the Vulkan loader at its manifest for the run, so it is the only driver:

  ```powershell
  $env:VK_DRIVER_FILES = (Get-ChildItem "C:\Program Files\Google\Chrome\Application\*\vk_swiftshader_icd.json" | Select-Object -First 1).FullName
  # ... the scripted run with +set r_gfxDevice vk (slow: ~25 fps for q3dm0 at r_mode 4) ...
  Remove-Item Env:VK_DRIVER_FILES
  ```

  gfx's vk device takes it since gfx `d7fe379` (it has no geometry shaders); then the GPU;
- run with the Khronos validation layer. ioq3-rs built one (its `GFX_TESTING.md` says how) in
  `Code2\Rust\ioq3-rs\target\tools\vvl`; the client started from a shell with these set loads it:

  ```powershell
  $env:VK_ADD_LAYER_PATH = "$env:code_root_dir\Code2\Rust\ioq3-rs\target\tools\vvl\build\layers\Release"
  $env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"
  $env:VK_KHRONOS_VALIDATION_LOG_FILENAME = "$env:TEMP\ioq3-gfx-vvl.txt"
  # ... the scripted run with +set r_gfxDevice vk ...
  Remove-Item Env:VK_ADD_LAYER_PATH, Env:VK_INSTANCE_LAYERS, Env:VK_KHRONOS_VALIDATION_LOG_FILENAME
  ```

  A run logs no message (gfx's last one, `VUID-vkQueueSubmit-pSignalSemaphores-00067`, is fixed:
  GFX_STATUS.md, Fixed in gfx). Any message is new - stop and look. D3D12's debug layer needs Windows'
  Graphics Tools (`d3d12SDKLayers.dll`), not installed here, and a Debug build (gfx asks for it
  only without `NDEBUG`);
- keep runs short and scripted (`+quit` after a few hundred frames), one at a time, and stop at
  the first validation error or device-lost;
- afterwards look for a driver reset (TDR) in the System log:

  ```powershell
  Get-WinEvent -FilterHashtable @{ LogName = 'System'; Id = 4101 } -MaxEvents 5 -ErrorAction SilentlyContinue
  ```

  On Linux: `sudo -n dmesg | grep -cE "ring gfx timeout|GPU reset"` stays 0.

## Linux

`cmake -S . -B build_gfx -DCMAKE_BUILD_TYPE=Release && cmake --build build_gfx` (and the same
with `-DUSE_GFX=OFF` into `build_nogfx`). gfx's x11 window and the jkg device are compiled
instead of the win32 / D3D ones, against the system SDL2 and GLFW (`find_package(glfw3)`).
Untested so far: there is no Linux machine here.
