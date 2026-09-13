#!/usr/bin/env bash

set -euo pipefail

# ========================================
# publish-chocolatey.sh
# Publish guchho to the Chocolatey community feed.
#
# Usage:
#   bash scripts/publish-chocolatey.sh              # publish
#   bash scripts/publish-chocolatey.sh --dry-run    # simulate only
#
# The Chocolatey push requires the package checksum + API key.
# Provide the key via CHOCO_API_KEY, or configure it once with:
#   choco apikey add -s https://push.chocolatey.org/ -k YOUR_KEY
# ========================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

CHOCO_DIR="${ROOT_DIR}/package/chocolatey/guchho"

DRY_RUN=""
if [[ "${1:-}" == "--dry-run" ]]; then
    DRY_RUN="true"
    echo "** DRY RUN MODE -- no packages will be published **"
    echo
elif [[ $# -gt 0 ]]; then
    echo "Error: Unknown option: $1"
    echo
    echo "Run:"
    echo "  bash scripts/publish-chocolatey.sh [--dry-run]"
    echo
    exit 1
fi

# ========================================
# Read version
# ========================================

if ! command -v node >/dev/null 2>&1; then
    echo "[ERROR] Node.js was not found. Install it first: https://nodejs.org"
    exit 1
fi

VERSION=$(node -p "require('${ROOT_DIR}/package/npm/guchho/package.json').version")
echo "Publishing version: ${VERSION}"
echo

# ========================================
# Validate .nupkg exists
# ========================================

echo "========================================"
echo "Validating package"
echo "========================================"
echo

NUPKG="${CHOCO_DIR}/guchho.${VERSION}.nupkg"

if [[ ! -f "${NUPKG}" ]]; then
    echo "  MISSING: ${NUPKG}"
    echo
    echo "Error: Package not found. Run build first:"
    echo "  bash scripts/build-chocolatey.sh"
    exit 1
fi

SIZE=$(du -h "${NUPKG}" | cut -f1)
echo "  OK: guchho.${VERSION}.nupkg (${SIZE})"
echo

# ========================================
# Check required tools
# ========================================

if ! command -v choco >/dev/null 2>&1; then
    echo "[ERROR] Chocolatey was not found. Install it first: https://chocolatey.org/install"
    exit 1
fi

# ========================================
# Push to Chocolatey
# ========================================

echo "========================================"
echo "Publishing to Chocolatey"
echo "========================================"
echo

PUSH_CMD=(choco push "${NUPKG}" --source https://push.chocolatey.org/)

if [[ -n "${CHOCO_API_KEY:-}" ]]; then
    PUSH_CMD+=(--api-key "${CHOCO_API_KEY}")
fi

echo "> ${PUSH_CMD[*]}"
echo

if [[ "${DRY_RUN}" == "true" ]]; then
    echo "(dry run) Package not actually pushed."
    echo
else
    "${PUSH_CMD[@]}"
    echo
    echo "  Done."
    echo
fi

# ========================================
# Summary
# ========================================

echo "========================================"
echo "PUBLISH COMPLETE"
echo "========================================"
echo
echo "Published: guchho@${VERSION}"
echo
echo "Users can now install:"
echo "  choco install guchho"
echo
if [[ -z "${CHOCO_API_KEY:-}" ]]; then
    echo "Note: if the push failed with an auth error, configure your key:"
    echo "  choco apikey add -s https://push.chocolatey.org/ -k <API_KEY>"
    echo
fi