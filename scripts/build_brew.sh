#!/usr/bin/env bash

set -euo pipefail

# ========================================
# build_brew.sh
# Generate the Homebrew formula for Guchho.
#
# The formula is a source build: it fetches
# the official v<version> GitHub tag tarball
# and runs the project's CMake build. Homebrew
# itself supplies CMake, so no prebuilt macOS
# artifact has to be published first.
#
# Steps:
#   1. Read the version from the npm package
#   2. Download the v<version> source tarball
#      (or accept a local copy)
#   3. Compute its SHA256
#   4. Write Formula/guchho.rb under
#      dist/homebrew/
#   5. Validate the formula with ruby -c
#
# Output: dist/homebrew/Formula/guchho.rb
#
# Usage:
#   bash scripts/build_brew.sh [options]
# ========================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

HOMEBREW_TAP_REPO="guchho/guchho"
FORMULA_NAME="guchho"
FORMULA_CLASS="Guchho"
DEFAULT_OUTDIR="${ROOT_DIR}/dist/homebrew"

# ========================================
# Options
# ========================================

VERSION=""
TARBALL=""
OUTDIR="${DEFAULT_OUTDIR}"
VALIDATE=1

WORK=""

# ========================================
# Helpers
# ========================================

# Node on Windows cannot resolve MSYS POSIX paths (/c/...).
# cygpath -m converts them to native paths (C:/Users/...).
to_winpath() {
    local p="$1"
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "${p}"
    else
        echo "${p}"
    fi
}

ROOT_DIR_WIN="$(to_winpath "${ROOT_DIR}")"

log() {
    echo "[Guchho] $*"
}

warn() {
    echo "[WARN] $*" >&2
}

error() {
    echo
    echo "[ERROR] $*" >&2
    echo
    exit 1
}

cleanup() {
    if [[ -n "${WORK}" && -d "${WORK}" ]]; then
        rm -rf "${WORK}"
    fi
}

trap cleanup EXIT

usage() {
    cat <<'EOF'
Usage: bash scripts/build_brew.sh [options]

Generate a Homebrew formula for Guchho from the official v<version>
source tag tarball. The formula builds from source with CMake; nothing
is uploaded.

Options:
  --version <x.y.z>   Version to package (default: package/npm/guchho/package.json)
  --tarball <path>    Use a local source tarball instead of downloading it
  --output <dir>      Output directory (default: dist/homebrew)
  --no-validate       Skip the ruby -c syntax check
  -h, --help          Show this help and exit
EOF
}

# ========================================
# Argument parsing
# ========================================

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            [[ $# -ge 2 ]] || error "--version needs a value"
            VERSION="$2"
            shift 2
            ;;
        --version=*)
            VERSION="${1#*=}"
            shift
            ;;
        --tarball)
            [[ $# -ge 2 ]] || error "--tarball needs a value"
            TARBALL="$2"
            shift 2
            ;;
        --tarball=*)
            TARBALL="${1#*=}"
            shift
            ;;
        --output)
            [[ $# -ge 2 ]] || error "--output needs a value"
            OUTDIR="$2"
            shift 2
            ;;
        --output=*)
            OUTDIR="${1#*=}"
            shift
            ;;
        --no-validate)
            VALIDATE=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "[ERROR] Unknown option: $1" >&2
            echo >&2
            usage >&2
            exit 2
            ;;
    esac
done

# ========================================
# Check required tools
# ========================================

if ! command -v node >/dev/null 2>&1; then
    error "Node.js was not found. Install it first: https://nodejs.org"
fi

SHA256_TOOL=""
if command -v sha256sum >/dev/null 2>&1; then
    SHA256_TOOL="sha256sum"
elif command -v shasum >/dev/null 2>&1; then
    SHA256_TOOL="shasum"
else
    error "No SHA256 tool found (sha256sum or shasum). Homebrew formulas require the tarball hash."
fi

if [[ -z "${TARBALL}" ]] && ! command -v curl >/dev/null 2>&1; then
    error "curl was not found. It downloads the source tarball: https://curl.se"
fi

sha256_hex() {
    if [[ "${SHA256_TOOL}" == "sha256sum" ]]; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

# ========================================
# Read version
# ========================================

if [[ -z "${VERSION}" ]]; then
    VERSION=$(node -p "require('${ROOT_DIR_WIN}/package/npm/guchho/package.json').version")
fi

if [[ ! "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+([-.][0-9A-Za-z.-]+)?$ ]]; then
    error "Version is not a version number: ${VERSION}"
fi

echo "========================================"
echo " GUCHHO HOMEBREW FORMULA"
echo "========================================"
echo
echo "Formula  : ${FORMULA_NAME}"
echo "Version  : ${VERSION}"
echo "Output   : ${OUTDIR}"
echo

# One temporary workspace for the whole run, removed by the EXIT trap.
WORK="$(mktemp -d)"

# ========================================
# Obtain the source tarball
# ========================================

echo "========================================"
echo "[1/4] Locating the source tarball"
echo "========================================"
echo

TARBALL_URL="https://github.com/${HOMEBREW_TAP_REPO}/archive/refs/tags/v${VERSION}.tar.gz"

if [[ -z "${TARBALL}" ]]; then
    TARBALL="${WORK}/${FORMULA_NAME}-${VERSION}.tar.gz"

    log "Downloading:"
    echo "  ${TARBALL_URL}"
    echo

    if ! curl -fsSL --retry 3 -o "${TARBALL}" "${TARBALL_URL}"; then
        error "Could not download ${TARBALL_URL}
        The v${VERSION} tag must exist before the Homebrew formula can be
        generated. Push the tag, or pass --tarball <path>."
    fi
else
    [[ -f "${TARBALL}" ]] || error "Tarball not found: ${TARBALL}"
    log "Using local tarball: ${TARBALL}"
fi

if [[ ! -s "${TARBALL}" ]]; then
    error "Tarball is empty: ${TARBALL}"
fi

# A tag that does not exist returns an HTML error page, but curl -f already
# rejects that. Check the gzip magic too, so a proxy or a stale cached file
# cannot turn into a formula whose source will never unpack.
if command -v od >/dev/null 2>&1; then
    MAGIC="$(od -An -tx1 -N2 "${TARBALL}" | tr -d ' \n')"
    if [[ "${MAGIC}" != "1f8b" ]]; then
        error "${TARBALL} is not a gzip archive (expected magic 1f8b, got ${MAGIC})."
    fi
fi

TARBALL_SHA256="$(sha256_hex "${TARBALL}")"

echo
log "SHA256: ${TARBALL_SHA256}"
echo

# ========================================
# Write the formula
# ========================================

echo "========================================"
echo "[2/4] Writing the formula"
echo "========================================"
echo

FORMULA_DIR="${OUTDIR}/Formula"
FORMULA_FILE="${FORMULA_DIR}/${FORMULA_NAME}.rb"
mkdir -p "${FORMULA_DIR}"

cat > "${FORMULA_FILE}" <<EOF
class ${FORMULA_CLASS} < Formula
  desc "Fast build system for web applications"
  homepage "https://github.com/${HOMEBREW_TAP_REPO}"
  url "${TARBALL_URL}"
  sha256 "${TARBALL_SHA256}"
  license "MIT"
  head "https://github.com/${HOMEBREW_TAP_REPO}.git", branch: "main"

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on "cmake" => :build

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-DCMAKE_BUILD_TYPE=Release",
           "-DBUILD_TESTING=OFF",
           *std_cmake_args
    system "cmake", "--build", "build", "--parallel"
    system "cmake", "--install", "build"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/guchho --version")
  end
end
EOF

log "Wrote:"
echo "  ${FORMULA_FILE}"
echo

# ========================================
# Validate
# ========================================

echo "========================================"
echo "[3/4] Validating"
echo "========================================"
echo

FORMULA_FILE_WIN="$(to_winpath "${FORMULA_FILE}")"

if [[ "${VALIDATE}" -eq 1 ]] && command -v ruby >/dev/null 2>&1; then
    log "Running ruby -c ..."
    if ! ruby -c "${FORMULA_FILE_WIN}"; then
        error "ruby -c rejected ${FORMULA_FILE}"
    fi
    echo
elif [[ "${VALIDATE}" -eq 1 ]]; then
    warn "ruby was not found; skipping the syntax check."
    echo
else
    log "Validation skipped (--no-validate)."
    echo
fi

# ========================================
# Report
# ========================================

echo "========================================"
echo "[4/4] BUILD COMPLETE"
echo "========================================"
echo
echo "Formula: ${FORMULA_FILE}"
echo
echo "Test a source build locally:"
echo "  brew install --build-from-source '${FORMULA_FILE_WIN}'"
echo
echo "To publish to a tap:"
echo "  bash scripts/publish_brew.sh"
echo
