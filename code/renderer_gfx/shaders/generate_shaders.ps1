# generate_shaders.ps1 - recompiles renderer_gfx's shaders (new file; ioq3-rs's `cargo xtask
# shaders` in PowerShell).
#
# Every <name>.<vs|fs>.gfxs here is compiled with gfx's shader compiler (wc_compiler_rs) into
# the four folders renderer_gfx loads, as <name>.<vs|fs>.gfx:
#   shaders/             gl3, gl4, gles3
#   shaders_d3/          d3d11 (d3d12 loads it too)
#   shaders_gles3_dark/  gl3, gl4, gles3 compiled from a folder named so: the compiler then adds
#                        its gamma fix for gles3 on Windows
#   shaders_vk/          vk (SPIR-V; needs glslangValidator - GLSLANG_VALIDATOR, or
#                        glslangValidator / glslang on PATH - else the folder is left as is)
#
# wc_compiler_rs is built with plain rustc into <ioq3>/build_shaders/ (cargo would write a
# Cargo.lock into the gfx tree, which is only read).
#
#   .\code\renderer_gfx\shaders\generate_shaders.ps1
#   $env:GLSLANG_VALIDATOR = "C:\tools\glslang\bin\glslang.exe"; .\code\renderer_gfx\shaders\generate_shaders.ps1

$ErrorActionPreference = "Stop"

$ShadersDir = $PSScriptRoot
$Ioq3Dir = (Resolve-Path "$ShadersDir/../../..").Path
$WorkDir = Join-Path $Ioq3Dir "build_shaders"

if (-not $env:code_root_dir) {
	Write-Host "code_root_dir is not set: gfx's shader compiler is not found" -ForegroundColor Red
	exit 1
}
$CompilerSrc = Join-Path $env:code_root_dir "Code2/General/gfx/wc_compiler_rs/src/main.rs"
$Compiler = Join-Path $WorkDir "wc_compiler_rs.exe"

# subfolder, -x backends, compiled from a folder named like the subfolder, needs glslang
$Dialects = @(
	@{ Dir = "shaders";            Backends = "gl3,gl4,gles3"; NamedDir = $false; SpirV = $false },
	@{ Dir = "shaders_d3";         Backends = "d3d11";         NamedDir = $false; SpirV = $false },
	@{ Dir = "shaders_gles3_dark"; Backends = "gl3,gl4,gles3"; NamedDir = $true;  SpirV = $false },
	@{ Dir = "shaders_vk";         Backends = "vk";            NamedDir = $false; SpirV = $true }
)

$Glslang = $env:GLSLANG_VALIDATOR
if (-not $Glslang) {
	foreach ($name in "glslangValidator", "glslang") {
		$found = Get-Command $name -ErrorAction SilentlyContinue
		if ($found) { $Glslang = $found.Source; break }
	}
}

New-Item -ItemType Directory -Force $WorkDir | Out-Null
if (-not (Test-Path $Compiler) -or (Get-Item $Compiler).LastWriteTime -lt (Get-Item $CompilerSrc).LastWriteTime) {
	Write-Host "building $Compiler" -ForegroundColor Yellow
	& rustc --edition 2024 -O --cap-lints allow --crate-name wc_compiler_rs $CompilerSrc -o $Compiler
	if ($LASTEXITCODE -ne 0) {
		Write-Host "rustc failed" -ForegroundColor Red
		exit 1
	}
}

$Sources = Get-ChildItem $ShadersDir -Filter "*.gfxs" | Sort-Object Name
$Compiled = 0
$Failed = 0

foreach ($dialect in $Dialects) {
	if ($dialect.SpirV -and -not $Glslang) {
		Write-Host "glslangValidator not found (GLSLANG_VALIDATOR / PATH): $($dialect.Dir) left as is" -ForegroundColor Yellow
		continue
	}

	$outDir = Join-Path $ShadersDir $dialect.Dir
	New-Item -ItemType Directory -Force $outDir | Out-Null

	foreach ($source in $Sources) {
		if ($source.Name -notmatch '\.(vs|fs)\.gfxs$') {
			Write-Host "$($source.Name): not <name>.<vs|fs>.gfxs, skipped" -ForegroundColor Yellow
			continue
		}
		$kind = $Matches[1]
		$output = Join-Path $outDir ($source.BaseName + ".gfx")

		# the gles3_dark build reads its source by bare name from a folder named so
		if ($dialect.NamedDir) {
			$cwd = Join-Path $WorkDir $dialect.Dir
			New-Item -ItemType Directory -Force $cwd | Out-Null
			Copy-Item $source.FullName $cwd -Force
			$input_ = $source.Name
		} else {
			$cwd = $ShadersDir
			$input_ = $source.FullName
		}

		if ($dialect.SpirV) { $env:GLSLANG_VALIDATOR = $Glslang }
		Push-Location $cwd
		& $Compiler -t $kind -x $dialect.Backends -i $input_ -o $output
		$ok = $LASTEXITCODE -eq 0
		Pop-Location

		if ($ok) {
			$Compiled++
			Write-Host "  $($source.Name) -> $output" -ForegroundColor Green
		} else {
			$Failed++
			Write-Host "  failed: $($source.Name) ($($dialect.Dir))" -ForegroundColor Red
		}
	}
}

if ($Failed -gt 0) {
	Write-Host "$Failed shader(s) failed, $Compiled compiled" -ForegroundColor Red
	exit 1
}
Write-Host "$Compiled shader(s) compiled" -ForegroundColor Green
