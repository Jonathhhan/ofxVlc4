#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRESET="oceanside-4k"
OUTPUT_DIR=""
FORCE=0

write_step() {
	printf '==> %s\n' "$1"
}

die() {
	printf '%s\n' "$1" >&2
	exit 1
}

usage() {
	cat <<'EOF'
Usage:
  bash ofxVlc4360Example/download-360-example-media.sh [options]

Options:
  --preset NAME              Preset to download
                             Supported: oceanside-4k, dji-mini-2
  --output-dir PATH          Override the destination folder
  --force                    Re-download even if the target file already exists
  --help, -h                 Show this help text

Examples:
  bash ofxVlc4360Example/download-360-example-media.sh
  bash ofxVlc4360Example/download-360-example-media.sh --preset dji-mini-2
  bash ofxVlc4360Example/download-360-example-media.sh --output-dir ./bin/data --force
EOF
}

download_to_file() {
	local url="$1"
	local target_path="$2"
	local user_agent="ofxVlc4 360 example downloader"

	if command -v curl >/dev/null 2>&1; then
		curl -L --fail --silent --show-error -A "$user_agent" -o "$target_path" "$url"
		return 0
	fi

	if command -v wget >/dev/null 2>&1; then
		wget --quiet --user-agent="$user_agent" -O "$target_path" "$url"
		return 0
	fi

	die "Could not find a supported downloader. Install curl or wget."
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--preset)
			PRESET="${2:-}"
			[[ -n "$PRESET" ]] || die "--preset requires a value"
			shift 2
			;;
		--output-dir)
			OUTPUT_DIR="${2:-}"
			[[ -n "$OUTPUT_DIR" ]] || die "--output-dir requires a value"
			shift 2
			;;
		--force)
			FORCE=1
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

PRESET_KEY="$(printf '%s' "$PRESET" | tr '[:upper:]' '[:lower:]')"
case "$PRESET_KEY" in
	oceanside-4k)
		FILE_NAME="oceanside-beach-360-4k.webm"
		DOWNLOAD_URL="https://commons.wikimedia.org/wiki/Special:Redirect/file/3D%20360%204K%20Motion%20TIme-lapse%20-%20Oceanside%20Beach.webm"
		SOURCE_PAGE="https://commons.wikimedia.org/wiki/File:3D_360_4K_Motion_TIme-lapse_-_Oceanside_Beach.webm"
		ATTRIBUTION="Wikimedia Commons / archived Vimeo source, free file page listed above"
		NOTES="Higher quality 3840x2160 360 sample; larger download."
		;;
	dji-mini-2)
		FILE_NAME="dji-mini-2-360-view.webm"
		DOWNLOAD_URL="https://commons.wikimedia.org/wiki/Special:Redirect/file/DJI%20Mini%202%20360%20view%20video.webm"
		SOURCE_PAGE="https://commons.wikimedia.org/wiki/File:DJI_Mini_2_360_view_video.webm"
		ATTRIBUTION="Wikimedia Commons / Wikideas1 (CC license on source page)"
		NOTES="Smaller, quicker 1920x1080 360 sample."
		;;
	*)
		die "Unknown preset '$PRESET'. Supported presets: dji-mini-2, oceanside-4k"
		;;
esac

if [[ -z "$OUTPUT_DIR" ]]; then
	OUTPUT_DIR="${SCRIPT_DIR}/bin/data"
fi

mkdir -p "$OUTPUT_DIR"
TARGET_PATH="${OUTPUT_DIR}/${FILE_NAME}"
TEMP_PATH="${TARGET_PATH}.part"

if [[ -f "$TARGET_PATH" && "$FORCE" -eq 0 ]]; then
	write_step "File already exists: $TARGET_PATH"
	printf 'Use --force to download it again.\n'
	exit 0
fi

write_step "Downloading preset '$PRESET_KEY'"
printf 'File: %s\n' "$FILE_NAME"
printf 'Notes: %s\n' "$NOTES"
printf 'Source: %s\n' "$SOURCE_PAGE"
printf 'Attribution: %s\n' "$ATTRIBUTION"

rm -f "$TEMP_PATH"
download_to_file "$DOWNLOAD_URL" "$TEMP_PATH"
mv -f "$TEMP_PATH" "$TARGET_PATH"

write_step "Saved sample media"
printf '%s\n' "$TARGET_PATH"
