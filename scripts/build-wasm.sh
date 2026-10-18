#!/usr/bin/env bash

set -euo pipefail

# ========================================
# Guchho WASM Build
# ========================================
#
# Builds the production WebAssembly engine with the wasm32-emscripten preset
# and copies the two artifacts a JavaScript package needs — the glue module
# and the .wasm binary — into package/npm/@guchho/wasm/{lib,bin}. No
# --preload-file and no assertions: project files reach the engine through a
# NODEFS mount the JS wrapper performs, never through a bundled .data file.
#
# Environment prerequisites:
#
#   EMSDK        the emsdk install directory (emsdk_env.sh already sourced,
#                or at least emcc on PATH). CI pins the SDK to 6.0.12
#                (setup-emsdk in .github/workflows/npm-publish.yml); use the
#                same version locally so the glue and binary match CI's.
#                On Windows Git Bash:
#                  export EMSDK_PYTHON="C:\\Python314\\python.exe"
#                  source "$TEMP/opencode/emsdk/emsdk_env.sh"
#   EMSDK_PYTHON the Python emsdk should drive (Windows only)
#   Ninja        on PATH (e.g. $LOCALAPPDATA/Microsoft/WinGet/Links)
#
# Usage:
#
#   bash scripts/build-wasm.sh              # configure + build + sync
#   bash scripts/build-wasm.sh --no-sync    # build only, skip the copy
#   bash scripts/build-wasm.sh --clean      # remove the build dir first

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/wasm32-emscripten"
PKG_DIR="${ROOT_DIR}/package/npm/@guchho/wasm"

SYNC=true
CLEAN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-sync) SYNC=false; shift ;;
        --clean)   CLEAN=true; shift ;;
        -h|--help)
            sed -n '3,31p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Error: unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# ========================================
# Tool checks
# ========================================

if [[ -z "${EMSDK:-}" ]]; then
    echo "Error: EMSDK is not set. Install emsdk and source emsdk_env.sh first." >&2
    echo "  https://emscripten.org/docs/getting_started/downloads.html" >&2
    exit 1
fi

if ! command -v emcc >/dev/null 2>&1; then
    echo "Error: emcc is not on PATH. Source \$EMSDK/emsdk_env.sh first." >&2
    exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "Error: ninja is not on PATH." >&2
    exit 1
fi

# ========================================
# Configure + build
# ========================================

if [[ "${CLEAN}" == true ]]; then
    rm -rf "${BUILD_DIR}"
fi

echo "Configuring wasm32-emscripten..."
cmake --preset wasm32-emscripten -S "${ROOT_DIR}" -B "${BUILD_DIR}"

echo "Building..."
cmake --build "${BUILD_DIR}" --parallel

# ========================================
# Artifacts
# ========================================

GLUE="${BUILD_DIR}/bin/guchho.js"
WASM="${BUILD_DIR}/bin/guchho.wasm"

if [[ ! -f "${GLUE}" || ! -f "${WASM}" ]]; then
    echo "Error: expected ${GLUE} and ${WASM} after the build." >&2
    exit 1
fi

# The size budget is the one Phase 0 measured against: a gzip'd .wasm over
# ~3 MB means something new got linked in that should not ship.
WASM_BYTES=$(wc -c < "${WASM}")
WASM_GZIP_BYTES=$(gzip -9 -c "${WASM}" | wc -c)
echo "guchho.wasm: ${WASM_BYTES} bytes raw, ${WASM_GZIP_BYTES} bytes gzip -9"

if [[ "${WASM_GZIP_BYTES}" -gt $((3 * 1024 * 1024)) ]]; then
    echo "Error: guchho.wasm exceeds the 3 MiB gzip size budget." >&2
    exit 1
fi

# ========================================
# Sync into the npm package
# ========================================

if [[ "${SYNC}" == true ]]; then
    mkdir -p "${PKG_DIR}/lib" "${PKG_DIR}/bin"
    cp "${GLUE}" "${PKG_DIR}/lib/guchho.js"
    cp "${WASM}" "${PKG_DIR}/lib/guchho.wasm"
    echo "Synced into ${PKG_DIR}/lib/"
fi

echo "Done."
