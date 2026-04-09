#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# download-model.sh — Download a GGUF model for use with ofxGgml.
#
# Usage:
#   ./scripts/download-model.sh [--model URL] [--output DIR] [--name FILE]
#
# Options:
#   --model  URL   Direct URL to a GGUF model file.
#                  Default: TinyLlama 1.1B Chat Q4_0
#   --output DIR   Directory to save the model (default: bin/data/models/)
#   --name   FILE  Output file name (default: derived from URL)
#   --list         List recommended models and exit
#   --help         Show this help message
#
# Recommended models (small enough for development):
#   TinyLlama 1.1B Chat Q4_0  (~600 MB)
#   Phi-2 Q4_0                 (~1.6 GB)
#   Gemma 2B Q4_0              (~1.4 GB)
# ---------------------------------------------------------------------------
set -euo pipefail

# Default: TinyLlama 1.1B Chat in Q4_0 quantization (~600 MB)
DEFAULT_MODEL_URL="https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_0.gguf"
MODEL_URL=""
OUTPUT_DIR=""
OUTPUT_NAME=""

write_step() {
	printf '==> %s\n' "$1"
}

die() {
	printf 'Error: %s\n' "$1" >&2
	exit 1
}

usage() {
	sed -n '2,/^# ---/{ /^# ---/d; s/^# //; s/^#//; p }' "$0"
	exit 0
}

list_models() {
	cat <<'EOF'
Recommended GGUF models for development / testing:

  TinyLlama 1.1B Chat Q4_0  (~600 MB)
    https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_0.gguf

  TinyLlama 1.1B Chat Q8_0  (~1.1 GB, higher quality)
    https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q8_0.gguf

  Phi-2 Q4_0  (~1.6 GB)
    https://huggingface.co/TheBloke/phi-2-GGUF/resolve/main/phi-2.Q4_0.gguf

  Gemma 2B Instruct Q4_0  (~1.4 GB)
    https://huggingface.co/second-state/Gemma-2b-it-GGUF/resolve/main/gemma-2b-it-Q4_0.gguf

Usage:
  ./scripts/download-model.sh --model <URL>
EOF
	exit 0
}

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
	case "$1" in
		--model)
			MODEL_URL="$2"
			shift 2
			;;
		--output)
			OUTPUT_DIR="$2"
			shift 2
			;;
		--name)
			OUTPUT_NAME="$2"
			shift 2
			;;
		--list)
			list_models
			;;
		--help|-h)
			usage
			;;
		*)
			die "Unknown option: $1"
			;;
	esac
done

# Defaults.
if [[ -z "$MODEL_URL" ]]; then
	MODEL_URL="$DEFAULT_MODEL_URL"
	write_step "No --model specified, using default: TinyLlama 1.1B Chat Q4_0"
fi

if [[ -z "$OUTPUT_DIR" ]]; then
	# Try to find the example's bin/data directory.
	SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
	ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
	GUI_EXAMPLE="$ADDON_ROOT/ofxGgml/ofxGgmlGuiExample/bin/data"
	if [[ -d "$(dirname "$GUI_EXAMPLE")" ]]; then
		OUTPUT_DIR="$GUI_EXAMPLE/models"
	else
		OUTPUT_DIR="$(pwd)/models"
	fi
fi

if [[ -z "$OUTPUT_NAME" ]]; then
	OUTPUT_NAME="$(basename "$MODEL_URL")"
fi

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------

DOWNLOAD_CMD=""
if command -v curl >/dev/null 2>&1; then
	DOWNLOAD_CMD="curl"
elif command -v wget >/dev/null 2>&1; then
	DOWNLOAD_CMD="wget"
else
	die "Neither curl nor wget found. Please install one."
fi

# ---------------------------------------------------------------------------
# Download
# ---------------------------------------------------------------------------

mkdir -p "$OUTPUT_DIR"
OUTPUT_PATH="$OUTPUT_DIR/$OUTPUT_NAME"

if [[ -f "$OUTPUT_PATH" ]]; then
	write_step "Model already exists at $OUTPUT_PATH"
	write_step "Delete it first if you want to re-download."
	exit 0
fi

write_step "Downloading model..."
write_step "  URL:  $MODEL_URL"
write_step "  Dest: $OUTPUT_PATH"
write_step ""

if [[ "$DOWNLOAD_CMD" == "curl" ]]; then
	curl -L --progress-bar -o "$OUTPUT_PATH" "$MODEL_URL"
elif [[ "$DOWNLOAD_CMD" == "wget" ]]; then
	wget --show-progress -O "$OUTPUT_PATH" "$MODEL_URL"
fi

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------

if [[ ! -s "$OUTPUT_PATH" ]]; then
	rm -f "$OUTPUT_PATH"
	die "Downloaded file is empty. Check the URL and try again."
fi

FILE_SIZE=$(wc -c < "$OUTPUT_PATH" 2>/dev/null || echo 0)
write_step "Download complete!  Size: $(numfmt --to=iec "$FILE_SIZE" 2>/dev/null || echo "$FILE_SIZE bytes")"
write_step "Model saved to: $OUTPUT_PATH"
write_step ""
write_step "Next steps:"
write_step "  1. Build ggml with scripts/build-ggml.sh (if not done)."
write_step "  2. Build and run your OF project with ofxGgml."
write_step "  3. Point the model path to: $OUTPUT_PATH"
