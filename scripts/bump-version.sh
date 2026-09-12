#!/usr/bin/env bash

set -euo pipefail

# ========================================
# bump-version.sh
#
# Update the version across all packages:
#   - CMakeLists.txt
#   - package/npm/guchho/package.json
#   - package/npm/@guchho/*/package.json
#
# Usage:
#   ./scripts/bump-version.sh <version>
#
# Example:
#   ./scripts/bump-version.sh 1.2.3
# ========================================


# ========================================
# Project root
# ========================================

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"


# ========================================
# Helpers
# ========================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

info() {
    echo -e "${GREEN}[bump]${NC} $*"
}

warn() {
    echo -e "${YELLOW}[bump]${NC} $*" >&2
}

error() {
    echo -e "${RED}[bump]${NC} $*" >&2
    exit 1
}


# ========================================
# Validate arguments
# ========================================

if [[ $# -ne 1 ]]; then
    error "Usage: $0 <version>\n  Example: $0 1.2.3"
fi

VERSION="$1"

# Validate semver format (basic: X.Y.Z with optional pre-release/build metadata)
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?(\+[a-zA-Z0-9.]+)?$ ]]; then
    error "Invalid version format: '$VERSION'\n  Expected semver like: 1.2.3, 0.1.0-beta.1, 1.0.0+build.1"
fi

info "Bumping version to ${VERSION}"


# ========================================
# Update CMakeLists.txt
# ========================================

CMAKE_FILE="$ROOT_DIR/CMakeLists.txt"

if [[ -f "$CMAKE_FILE" ]]; then
    OLD_CMAKE_VERSION=$(grep -oP 'VERSION\s+\K[0-9]+\.[0-9]+\.[0-9]+' "$CMAKE_FILE" || true)
    if [[ -n "$OLD_CMAKE_VERSION" ]]; then
        sed -i "s/VERSION ${OLD_CMAKE_VERSION}/VERSION ${VERSION}/" "$CMAKE_FILE"
        info "  CMakeLists.txt: ${OLD_CMAKE_VERSION} -> ${VERSION}"
    else
        warn "  CMakeLists.txt: could not find VERSION field to update"
    fi
fi


# ========================================
# Update main guchho package.json
# ========================================

MAIN_PKG="$ROOT_DIR/package/npm/guchho/package.json"

if [[ -f "$MAIN_PKG" ]]; then
    OLD_MAIN_VERSION=$(grep -oP '"version"\s*:\s*"\K[^"]+' "$MAIN_PKG" | head -1 || true)

    # Update package version using single-quoted sed
    sed -i '0,/"version": "'"${OLD_MAIN_VERSION}"'"/s//"version": "'"${VERSION}"'"/' "$MAIN_PKG"

    # Update all @guchho/* optional dependency versions
    sed -i '/"@guchho\//s/"[0-9][^"]*"/"'"${VERSION}"'"/g' "$MAIN_PKG"

    info "  guchho/package.json: ${OLD_MAIN_VERSION} -> ${VERSION}"
fi


# ========================================
# Update @guchho/* platform package.json files
# ========================================

PLATFORM_DIR="$ROOT_DIR/package/npm/@guchho"

if [[ -d "$PLATFORM_DIR" ]]; then
    for pkg_dir in "$PLATFORM_DIR"/*/; do
        pkg_json="$pkg_dir/package.json"
        if [[ -f "$pkg_json" ]]; then
            pkg_name=$(basename "$pkg_dir")
            OLD_PKG_VERSION=$(grep -oP '"version"\s*:\s*"\K[^"]+' "$pkg_json" | head -1 || true)
            sed -i '0,/"version": "'"${OLD_PKG_VERSION}"'"/s//"version": "'"${VERSION}"'"/' "$pkg_json"
            info "  @guchho/${pkg_name}: ${OLD_PKG_VERSION} -> ${VERSION}"
        fi
    done
fi


# ========================================
# Summary
# ========================================

echo ""
info "Done! Version bumped to ${VERSION} across all packages."
