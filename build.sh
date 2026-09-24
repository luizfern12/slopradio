#!/usr/bin/env bash
#
# build.sh - Build script for LaraRadio (appLaraRadio)
#
# The build does four things:
#   1. Compile Qt translation files (*.ts) into *.qm with lrelease
#      (required: resources.qrc embeds them and CMake has no rule to make them)
#   2. Configure the project with CMake
#   3. Build the appLaraRadio binary
#   4. Print the path to the resulting binary
#
# Usage:
#   ./build.sh                 Build in Release mode (default)
#   ./build.sh --debug         Build in Debug mode
#   ./build.sh --clean         Wipe the build directory first
#   ./build.sh -j 4            Build with 4 parallel jobs (default: nproc)
#   ./build.sh -G "Unix Makefiles"   Use a specific CMake generator
#   ./build.sh --install       Build and install to /usr/local (needs sudo)
#   ./build.sh --help          Show this help
#
# Environment overrides:
#   BUILD_DIR    Build directory (default: build)
#   CXX          Compiler to use (e.g. CXX=clang++ ./build.sh)

set -euo pipefail

# ---------------------------------------------------------------------------
# Defaults and argument parsing
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"

BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
BUILD_TYPE="Release"
GENERATOR=""
JOBS="$(nproc 2>/dev/null || echo 2)"
CLEAN=0
DO_INSTALL=0

usage() {
    sed -n '2,28p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)            BUILD_TYPE="Debug" ;;
        --release)          BUILD_TYPE="Release" ;;
        --clean)            CLEAN=1 ;;
        --install)          DO_INSTALL=1 ;;
        --help|-h)          usage ;;
        -j|--jobs)          JOBS="$2"; shift ;;
        -G|--generator)     GENERATOR="$2"; shift ;;
        *)                  echo "Unknown option: $1" >&2; usage ;;
    esac
    shift
done

# ---------------------------------------------------------------------------
# Prerequisite checks
# ---------------------------------------------------------------------------

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Error: '$1' not found in PATH." >&2
        echo "       Install it and try again." >&2
        echo "       Ubuntu: sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qt6-tools-dev libtag1-dev" >&2
        exit 1
    fi
}

require_cmd cmake
require_cmd lrelease

# ---------------------------------------------------------------------------
# Step 1: Compile translations (*.ts -> *.qm)
# ---------------------------------------------------------------------------

LANG_DIR="${PROJECT_DIR}/languages"

compile_translations() {
    local ts_files
    ts_files="$(ls "${LANG_DIR}"/*.ts 2>/dev/null || true)"

    if [[ -z "${ts_files}" ]]; then
        echo ">> No .ts files found in ${LANG_DIR}; skipping translations."
        return
    fi

    echo ">> Compiling translations:"
    for ts in ${ts_files}; do
        qm="${ts%.ts}.qm"
        echo "   lrelease ${ts}"
        lrelease -silent "${ts}" -qm "${qm}"
    done
}

# ---------------------------------------------------------------------------
# Step 2: Configure
# ---------------------------------------------------------------------------

configure() {
    local args=(
        -S "${PROJECT_DIR}"
        -B "${BUILD_DIR}"
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    )

    if [[ -n "${GENERATOR}" ]]; then
        args+=(-G "${GENERATOR}")
    fi

    echo ">> Configuring (${BUILD_TYPE}):"
    echo "   cmake ${args[*]}"
    cmake "${args[@]}"
}

# ---------------------------------------------------------------------------
# Step 3: Build
# ---------------------------------------------------------------------------

build() {
    echo ">> Building with ${JOBS} parallel job(s)..."
    cmake --build "${BUILD_DIR}" -j "${JOBS}"
}

# ---------------------------------------------------------------------------
# Step 4: Locate the binary
# ---------------------------------------------------------------------------

binary_path() {
    local binary="${BUILD_DIR}/appLaraRadio"
    if [[ ! -x "${binary}" ]]; then
        binary="$(find "${BUILD_DIR}" -maxdepth 3 -type f -name appLaraRadio -perm -u+x 2>/dev/null | head -1)"
    fi
    if [[ -n "${binary}" && -x "${binary}" ]]; then
        echo "${binary}"
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

main() {
    echo "== LaraRadio build =="
    echo "   Build dir: ${BUILD_DIR}"
    echo "   Type:      ${BUILD_TYPE}"

    compile_translations

    if [[ "${CLEAN}" -eq 1 && -d "${BUILD_DIR}" ]]; then
        echo ">> Cleaning ${BUILD_DIR}"
        rm -rf "${BUILD_DIR}"
    fi

    configure
    build

    local binary
    binary="$(binary_path)"
    if [[ -n "${binary}" ]]; then
        echo
        echo "== Build complete =="
        echo "   Binary: ${binary}"
        echo "   Run:    ${binary}"
    else
        echo
        echo "== Build complete (binary not found in ${BUILD_DIR}) ==" >&2
    fi

    if [[ "${DO_INSTALL}" -eq 1 ]]; then
        echo ">> Installing..."
        cmake --install "${BUILD_DIR}"
    fi
}

main "$@"