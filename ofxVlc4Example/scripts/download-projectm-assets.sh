#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXAMPLE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PRESETS_REPO_URL="https://github.com/projectM-visualizer/presets-cream-of-the-crop.git"
TEXTURES_REPO_URL="https://github.com/projectM-visualizer/presets-milkdrop-texture-pack.git"
DOWNLOAD_PRESETS=0
DOWNLOAD_TEXTURES=0
KEEP_TEMP=0

write_step() {
	printf '==> %s\n' "$1"
}

die() {
	printf '%s\n' "$1" >&2
	exit 1
}

require_command() {
	if ! command -v "$1" >/dev/null 2>&1; then
		die "Required command not found: $1"
	fi
}

ensure_dir() {
	mkdir -p "$1"
}

reset_dir() {
	rm -rf "$1"
	mkdir -p "$1"
}

copy_repo_contents() {
	local source_dir="$1"
	local target_dir="$2"
	reset_dir "$target_dir"
	find "$source_dir" -mindepth 1 -maxdepth 1 ! -name '.git' -exec cp -a {} "$target_dir"/ \;
}

usage() {
	cat <<'EOF'
Usage:
  bash ofxVlc4Example/scripts/download-projectm-assets.sh [options]

Options:
  --presets                   Download the Cream of the Crop preset pack
  --textures                  Download the full Milkdrop texture pack
  --keep-temp                 Keep the temporary clone directory

Examples:
  bash ofxVlc4Example/scripts/download-projectm-assets.sh
  bash ofxVlc4Example/scripts/download-projectm-assets.sh --presets
  bash ofxVlc4Example/scripts/download-projectm-assets.sh --textures
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--presets)
			DOWNLOAD_PRESETS=1
			shift
			;;
		--textures)
			DOWNLOAD_TEXTURES=1
			shift
			;;
		--keep-temp)
			KEEP_TEMP=1
			shift
			;;
		--help|-h)
			usage
			exit 0
			;;
		*)
			die "Unknown argument: $1"
			;;
	esac
done

require_command git
require_command mktemp

TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/ofxVlc4-projectm-assets.XXXXXXXX")"
PRESETS_CLONE_DIR="${TEMP_ROOT}/presets"
TEXTURES_CLONE_DIR="${TEMP_ROOT}/textures"

cleanup() {
	if [[ "$KEEP_TEMP" -eq 0 ]]; then
		rm -rf "$TEMP_ROOT"
	else
		write_step "Keeping temporary files at ${TEMP_ROOT}"
	fi
}
trap cleanup EXIT

write_step "Using temporary download root: ${TEMP_ROOT}"

if [[ "$DOWNLOAD_PRESETS" -eq 0 && "$DOWNLOAD_TEXTURES" -eq 0 ]]; then
	DOWNLOAD_PRESETS=1
	DOWNLOAD_TEXTURES=1
	write_step "No asset selection provided, downloading the full preset and texture packs"
fi

if [[ "$DOWNLOAD_PRESETS" -eq 1 ]]; then
	write_step "Cloning Cream of the Crop preset pack"
	git clone --depth 1 "$PRESETS_REPO_URL" "$PRESETS_CLONE_DIR"
fi

if [[ "$DOWNLOAD_TEXTURES" -eq 1 ]]; then
	write_step "Cloning full Milkdrop texture pack"
	git clone --depth 1 "$TEXTURES_REPO_URL" "$TEXTURES_CLONE_DIR"
fi

LOCAL_DATA_ROOT="${EXAMPLE_ROOT}/bin/data"
ensure_dir "$LOCAL_DATA_ROOT"

if [[ "$DOWNLOAD_PRESETS" -eq 1 ]]; then
	write_step "Installing presets into ${EXAMPLE_ROOT}/bin/data/presets"
	copy_repo_contents "$PRESETS_CLONE_DIR" "${LOCAL_DATA_ROOT}/presets"
fi

if [[ "$DOWNLOAD_TEXTURES" -eq 1 ]]; then
	write_step "Installing textures into ${EXAMPLE_ROOT}/bin/data/textures"
	copy_repo_contents "$TEXTURES_CLONE_DIR" "${LOCAL_DATA_ROOT}/textures"
	# VLC's internal projectM module looks for texture files in a 'textures/' subdirectory
	# relative to the preset directory (i.e. bin/data/presets/textures/).  Install a copy
	# there so both the standalone ofxProjectM component and the libvlc projectm audio
	# visualizer can resolve texture references from Milkdrop presets.
	write_step "Installing textures into ${EXAMPLE_ROOT}/bin/data/presets/textures (for VLC's internal projectM)"
	ensure_dir "${LOCAL_DATA_ROOT}/presets"
	copy_repo_contents "$TEXTURES_CLONE_DIR" "${LOCAL_DATA_ROOT}/presets/textures"
fi

write_step "Done"
if [[ "$DOWNLOAD_PRESETS" -eq 1 ]]; then
	printf 'Installed preset pack from %s\n' "$PRESETS_REPO_URL"
fi
if [[ "$DOWNLOAD_TEXTURES" -eq 1 ]]; then
	printf 'Installed texture pack from %s\n' "$TEXTURES_REPO_URL"
	printf '  -> bin/data/textures          (ofxProjectM standalone)\n'
	printf '  -> bin/data/presets/textures  (libvlc projectM audio visualizer)\n'
fi
