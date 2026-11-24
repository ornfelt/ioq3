#!/usr/bin/env bash
# generate_shaders.sh - recompiles renderer_gfx's shaders (new file; ioq3-rs's `cargo xtask
# shaders` in bash, the same as generate_shaders.ps1).
#
# Every <name>.<vs|fs>.gfxs here is compiled with gfx's shader compiler (wc_compiler_rs) into
# the three folders renderer_gfx loads, as <name>.<vs|fs>.gfx:
#   shaders/             gl3, gl4, gles3
#   shaders_d3/          d3d11 (d3d12 loads it too)
#   shaders_vk/          vk (SPIR-V; needs glslangValidator - GLSLANG_VALIDATOR, or
#                        glslangValidator / glslang on PATH - else the folder is left as is)
#
# wc_compiler_rs is built with plain rustc into <ioq3>/build_shaders/ (cargo would write a
# Cargo.lock into the gfx tree, which is only read).
#
#   ./code/renderer_gfx/shaders/generate_shaders.sh
#   GLSLANG_VALIDATOR=/opt/glslang/bin/glslang ./code/renderer_gfx/shaders/generate_shaders.sh

set -u

shaders_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ioq3_dir="$(cd "$shaders_dir/../../.." && pwd)"
work_dir="$ioq3_dir/build_shaders"

if [ -z "${code_root_dir:-}" ]; then
	echo -e "\e[31mcode_root_dir is not set: gfx's shader compiler is not found\e[0m" >&2
	exit 1
fi
compiler_src="$code_root_dir/Code2/General/gfx/wc_compiler_rs/src/main.rs"
compiler="$work_dir/wc_compiler_rs"

# subfolder:backends:needs glslang
dialects=(
	"shaders:gl3,gl4,gles3:0"
	"shaders_d3:d3d11:0"
	"shaders_vk:vk:1"
)

glslang="${GLSLANG_VALIDATOR:-}"
if [ -z "$glslang" ]; then
	glslang="$(command -v glslangValidator || command -v glslang || true)"
fi

mkdir -p "$work_dir"
if [ ! -x "$compiler" ] || [ "$compiler" -ot "$compiler_src" ]; then
	echo -e "\e[33mbuilding $compiler\e[0m"
	if ! rustc --edition 2024 -O --cap-lints allow --crate-name wc_compiler_rs "$compiler_src" -o "$compiler"; then
		echo -e "\e[31mrustc failed\e[0m" >&2
		exit 1
	fi
fi

compiled=0
failed=0

for dialect in "${dialects[@]}"; do
	IFS=: read -r dir backends spirv <<< "$dialect"

	if [ "$spirv" = 1 ] && [ -z "$glslang" ]; then
		echo -e "\e[33mglslangValidator not found (GLSLANG_VALIDATOR / PATH): $dir left as is\e[0m"
		continue
	fi

	out_dir="$shaders_dir/$dir"
	mkdir -p "$out_dir"

	for source in "$shaders_dir"/*.gfxs; do
		name="$(basename "$source")"
		case "$name" in
			*.vs.gfxs) kind=vs ;;
			*.fs.gfxs) kind=fs ;;
			*)
				echo -e "\e[33m$name: not <name>.<vs|fs>.gfxs, skipped\e[0m"
				continue ;;
		esac
		output="$out_dir/${name%.gfxs}.gfx"

		if (cd "$shaders_dir" && GLSLANG_VALIDATOR="$glslang" "$compiler" -t "$kind" -x "$backends" -i "$source" -o "$output"); then
			compiled=$((compiled + 1))
			echo -e "\e[32m  $name -> $output\e[0m"
		else
			failed=$((failed + 1))
			echo -e "\e[31m  failed: $name ($dir)\e[0m" >&2
		fi
	done
done

if [ "$failed" -gt 0 ]; then
	echo -e "\e[31m$failed shader(s) failed, $compiled compiled\e[0m" >&2
	exit 1
fi
echo -e "\e[32m$compiled shader(s) compiled\e[0m"
