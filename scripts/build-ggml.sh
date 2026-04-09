#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build-ggml.sh — Clone, compile, and install the ggml tensor library.
#
# Usage:
#   ./scripts/build-ggml.sh [--prefix /usr/local] [--jobs 8] [--gpu]
#
# Options:
#   --prefix DIR   Install prefix (default: /usr/local)
#   --jobs N       Parallel build jobs (default: number of CPU cores)
#   --gpu          Enable CUDA backend (requires CUDA toolkit)
#   --vulkan       Enable Vulkan backend (requires Vulkan SDK)
#   --metal        Enable Metal backend (macOS only)
#   --clean        Remove build directory before building
#   --help         Show this help message
# ---------------------------------------------------------------------------
set -euo pipefail

GGML_REPO="https://github.com/ggml-org/ggml.git"
GGML_BRANCH="master"
BUILD_DIR="/tmp/ggml-build"
SOURCE_DIR="/tmp/ggml-source"
INSTALL_PREFIX="/usr/local"
JOBS=""
ENABLE_CUDA=0
ENABLE_VULKAN=0
ENABLE_METAL=0
CLEAN=0

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

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
	case "$1" in
		--prefix)
			INSTALL_PREFIX="$2"
			shift 2
			;;
		--jobs)
			JOBS="$2"
			shift 2
			;;
		--gpu|--cuda)
			ENABLE_CUDA=1
			shift
			;;
		--vulkan)
			ENABLE_VULKAN=1
			shift
			;;
		--metal)
			ENABLE_METAL=1
			shift
			;;
		--clean)
			CLEAN=1
			shift
			;;
		--help|-h)
			usage
			;;
		*)
			die "Unknown option: $1"
			;;
	esac
done

if [[ -z "$JOBS" ]]; then
	if command -v nproc >/dev/null 2>&1; then
		JOBS=$(nproc)
	elif command -v sysctl >/dev/null 2>&1; then
		JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
	else
		JOBS=4
	fi
fi

# ---------------------------------------------------------------------------
# Prerequisites
# ---------------------------------------------------------------------------

for cmd in git cmake make; do
	command -v "$cmd" >/dev/null 2>&1 || die "Required command not found: $cmd"
done

# ---------------------------------------------------------------------------
# Clone / update source
# ---------------------------------------------------------------------------

if [[ "$CLEAN" -eq 1 ]]; then
	write_step "Cleaning previous build..."
	rm -rf "$BUILD_DIR" "$SOURCE_DIR"
fi

if [[ -d "$SOURCE_DIR/.git" ]]; then
	write_step "Updating existing ggml source in $SOURCE_DIR..."
	cd "$SOURCE_DIR"
	git fetch origin
	git checkout "$GGML_BRANCH"
	git pull origin "$GGML_BRANCH"
else
	write_step "Cloning ggml from $GGML_REPO..."
	rm -rf "$SOURCE_DIR"
	git clone --branch "$GGML_BRANCH" --depth 1 "$GGML_REPO" "$SOURCE_DIR"
fi

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------

write_step "Configuring ggml build..."

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

CMAKE_ARGS=(
	-DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"
	-DCMAKE_BUILD_TYPE=Release
	-DBUILD_SHARED_LIBS=ON
	-DGGML_BUILD_EXAMPLES=OFF
	-DGGML_BUILD_TESTS=OFF
)

if [[ "$ENABLE_CUDA" -eq 1 ]]; then
	write_step "CUDA backend enabled."
	CMAKE_ARGS+=(-DGGML_CUDA=ON)
fi

if [[ "$ENABLE_VULKAN" -eq 1 ]]; then
	write_step "Vulkan backend enabled."
	CMAKE_ARGS+=(-DGGML_VULKAN=ON)
fi

if [[ "$ENABLE_METAL" -eq 1 ]]; then
	write_step "Metal backend enabled."
	CMAKE_ARGS+=(-DGGML_METAL=ON)
fi

cmake "$SOURCE_DIR" "${CMAKE_ARGS[@]}"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

write_step "Building ggml with $JOBS parallel jobs..."
cmake --build . --config Release -j "$JOBS"

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

write_step "Installing ggml to $INSTALL_PREFIX..."
if [[ -w "$INSTALL_PREFIX" ]]; then
	cmake --install .
else
	write_step "Requires elevated permissions for $INSTALL_PREFIX — using sudo."
	sudo cmake --install .
fi

# ---------------------------------------------------------------------------
# Copy headers to addon libs/ directory (optional, for OF project generator)
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADDON_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ADDON_GGML_ROOT="$ADDON_ROOT/ofxGgml"

if [[ -d "$ADDON_GGML_ROOT" ]]; then
	write_step "Copying ggml headers to $ADDON_GGML_ROOT/libs/ggml/include..."
	mkdir -p "$ADDON_GGML_ROOT/libs/ggml/include"
	cp -v "$SOURCE_DIR/include/"*.h "$ADDON_GGML_ROOT/libs/ggml/include/"
fi

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------

write_step "Verifying installation..."
if pkg-config --exists ggml 2>/dev/null; then
	write_step "pkg-config: ggml version $(pkg-config --modversion ggml)"
elif [[ -f "$INSTALL_PREFIX/lib/libggml.so" ]] || [[ -f "$INSTALL_PREFIX/lib/libggml.dylib" ]]; then
	write_step "Library found in $INSTALL_PREFIX/lib/"
else
	write_step "Warning: could not verify ggml installation. You may need to set LD_LIBRARY_PATH."
fi

write_step "Done!  ggml has been built and installed to $INSTALL_PREFIX."
write_step ""
write_step "Next steps:"
write_step "  1. Run scripts/download-model.sh to fetch a GGUF model."
write_step "  2. Build your OF project with ofxGgml."
