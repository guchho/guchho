#!/usr/bin/env bash

set -euo pipefail

# ========================================
# publish-winget.sh
# Submit the generated manifests to
# microsoft/winget-pkgs.
#
# WinGet has no feed to push to: a package
# is published by opening a pull request.
# This script validates the manifests from
# scripts/build-winget.sh and then calls
# `wingetcreate submit`, which forks the
# community repository and opens the PR.
#
# Usage:
#   bash scripts/publish-winget.sh              # submit
#   bash scripts/publish-winget.sh --dry-run    # validate and print only
#
# Requirements:
#   - manifests from scripts/build-winget.sh
#   - wingetcreate (winget install Microsoft.WingetCreate)
#   - a GitHub token for wingetcreate
#
# Prefer WINGET_CREATE_GITHUB_TOKEN: wingetcreate reads it itself and it
# never appears in the command line or logs. --token is only forwarded
# when passed explicitly.
# ========================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PACKAGE_ID="Guchho.Guchho"
DEFAULT_MANIFEST_ROOT="${ROOT_DIR}/package/winget/manifests/g/Guchho/Guchho"
FALLBACK_MANIFEST_ROOT="${ROOT_DIR}/dist/winget/manifests/g/Guchho/Guchho"

# ========================================
# Options
# ========================================

VERSION=""
MANIFEST_DIR=""
TOKEN=""
TOKEN_FROM_ARG=0
PR_TITLE=""
VALIDATE=1
DRY_RUN=0

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

usage() {
    cat <<'EOF'
Usage: bash scripts/publish-winget.sh [options]

Submit the manifests produced by scripts/build-winget.sh to
microsoft/winget-pkgs with `wingetcreate submit`. This opens a pull
request; nothing is uploaded to a package feed.

Options:
  --version <x.y.z>     Version to submit (default: package/npm/guchho/package.json)
  --manifest <path>     Manifest directory (default: the version directory under
                        package/winget/manifests/g/Guchho/Guchho/)
  --token <token>       GitHub token passed explicitly to wingetcreate. Prefer
                        WINGET_CREATE_GITHUB_TOKEN instead: wingetcreate reads it
                        itself and it is not logged.
  --pr-title <title>    Pull request title (default: wingetcreate chooses one)
  --no-validate         Skip `winget validate` even when winget is installed
  --dry-run             Validate and print the submit command; do not submit
  -h, --help            Show this help and exit

The environment variable WINGET_CREATE_GITHUB_TOKEN is left for wingetcreate
to pick up; it is never printed by this script.
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
        --manifest)
            [[ $# -ge 2 ]] || error "--manifest needs a value"
            MANIFEST_DIR="$2"
            shift 2
            ;;
        --manifest=*)
            MANIFEST_DIR="${1#*=}"
            shift
            ;;
        --token)
            [[ $# -ge 2 ]] || error "--token needs a value"
            TOKEN="$2"
            TOKEN_FROM_ARG=1
            shift 2
            ;;
        --token=*)
            TOKEN="${1#*=}"
            TOKEN_FROM_ARG=1
            shift
            ;;
        --pr-title)
            [[ $# -ge 2 ]] || error "--pr-title needs a value"
            PR_TITLE="$2"
            shift 2
            ;;
        --pr-title=*)
            PR_TITLE="${1#*=}"
            shift
            ;;
        --no-validate)
            VALIDATE=0
            shift
            ;;
        --dry-run)
            DRY_RUN=1
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
# Read version
# ========================================

if ! command -v node >/dev/null 2>&1; then
    error "Node.js was not found. Install it first: https://nodejs.org"
fi

if [[ -z "${VERSION}" ]]; then
    VERSION=$(node -p "require('${ROOT_DIR_WIN}/package/npm/guchho/package.json').version")
fi

if [[ ! "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+([-.][0-9A-Za-z.-]+)?$ ]]; then
    error "Version is not a version number: ${VERSION}"
fi

echo "========================================"
echo " SUBMIT GUCHHO TO WINGET"
echo "========================================"
echo

if [[ "${DRY_RUN}" -eq 1 ]]; then
    echo "** DRY RUN MODE -- nothing will be submitted **"
    echo
fi

echo "Package  : ${PACKAGE_ID}"
echo "Version  : ${VERSION}"
echo

# ========================================
# Locate the manifests
# ========================================

if [[ -z "${MANIFEST_DIR}" ]]; then
    MANIFEST_DIR="${DEFAULT_MANIFEST_ROOT}/${VERSION}"
    if [[ ! -d "${MANIFEST_DIR}" && -d "${FALLBACK_MANIFEST_ROOT}/${VERSION}" ]]; then
        MANIFEST_DIR="${FALLBACK_MANIFEST_ROOT}/${VERSION}"
    fi
fi

[[ -d "${MANIFEST_DIR}" ]] || error "Manifest directory not found: ${MANIFEST_DIR}
        Generate it first:
          bash scripts/build-winget.sh"

VERSION_FILE="${MANIFEST_DIR}/${PACKAGE_ID}.yaml"
INSTALLER_FILE="${MANIFEST_DIR}/${PACKAGE_ID}.installer.yaml"
LOCALE_FILE="${MANIFEST_DIR}/${PACKAGE_ID}.locale.en-US.yaml"

for expected in "${VERSION_FILE}" "${INSTALLER_FILE}" "${LOCALE_FILE}"; do
    [[ -f "${expected}" ]] || error "Missing manifest: ${expected}
        Regenerate the manifests with:
          bash scripts/build-winget.sh"
done

MANIFEST_DIR_WIN="$(to_winpath "${MANIFEST_DIR}")"

log "Manifests : ${MANIFEST_DIR}"
echo

# ========================================
# Validate
# ========================================

echo "========================================"
echo "[1/3] Validating the manifests"
echo "========================================"
echo

if [[ "${VALIDATE}" -eq 1 ]] && command -v winget >/dev/null 2>&1; then
    log "Running winget validate ..."
    if ! winget validate --manifest "${MANIFEST_DIR_WIN}"; then
        error "winget validate rejected the manifests in ${MANIFEST_DIR}.
        Update the Windows Package Manager, or re-run with --no-validate
        to inspect them manually."
    fi
    echo
elif [[ "${VALIDATE}" -eq 1 ]]; then
    warn "winget was not found; skipping validation. Run it on a Windows host with:"
    echo "  winget validate --manifest '${MANIFEST_DIR_WIN}'"
    echo
else
    log "Validation skipped (--no-validate)."
    echo
fi

# ========================================
# Build the submit command
# ========================================

echo "========================================"
echo "[2/3] Preparing the submission"
echo "========================================"
echo

if ! command -v wingetcreate >/dev/null 2>&1; then
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        warn "wingetcreate was not found; install it before publishing:"
        echo "  winget install Microsoft.WingetCreate"
        echo
    else
        error "wingetcreate was not found. Install it first:
          winget install Microsoft.WingetCreate"
    fi
fi

SUBMIT_CMD=(wingetcreate submit)
DISPLAY_CMD=(wingetcreate submit)

if [[ -n "${PR_TITLE}" ]]; then
    SUBMIT_CMD+=(--prtitle "${PR_TITLE}")
    DISPLAY_CMD+=(--prtitle "${PR_TITLE}")
fi

if [[ "${TOKEN_FROM_ARG}" -eq 1 ]]; then
    SUBMIT_CMD+=(--token "${TOKEN}")
    DISPLAY_CMD+=(--token '***')
    warn "--token may be logged by wingetcreate; prefer WINGET_CREATE_GITHUB_TOKEN."
    echo
fi

SUBMIT_CMD+=("${MANIFEST_DIR_WIN}")
DISPLAY_CMD+=("${MANIFEST_DIR_WIN}")

if [[ "${TOKEN_FROM_ARG}" -eq 0 && -z "${WINGET_CREATE_GITHUB_TOKEN:-}" ]]; then
    warn "No token supplied; wingetcreate will prompt for GitHub login."
    echo "  Set WINGET_CREATE_GITHUB_TOKEN (or pass --token) for non-interactive use."
    echo
else
    log "Token     : $([[ "${TOKEN_FROM_ARG}" -eq 1 ]] && echo 'from --token' || echo 'from WINGET_CREATE_GITHUB_TOKEN')"
fi

log "PR title  : $([[ -n "${PR_TITLE}" ]] && echo "${PR_TITLE}" || echo '(wingetcreate default)')"

DISPLAY_LINE=""
for arg in "${DISPLAY_CMD[@]}"; do
    if [[ "${arg}" == "***" ]]; then
        DISPLAY_LINE+="*** "
    else
        printf -v quoted '%q' "${arg}"
        DISPLAY_LINE+="${quoted} "
    fi
done
log "Command   : ${DISPLAY_LINE% }"
echo

# ========================================
# Submit
# ========================================

echo "========================================"
echo "[3/3] Submitting"
echo "========================================"
echo

if [[ "${DRY_RUN}" -eq 1 ]]; then
    echo "Would run the command above to open a pull request against"
    echo "microsoft/winget-pkgs."
    echo
    echo "Run again without --dry-run to submit."
    echo
    exit 0
fi

if ! "${SUBMIT_CMD[@]}"; then
    error "wingetcreate submit failed. Check the output above, then retry."
fi

echo

echo "========================================"
echo " SUBMISSION COMPLETE"
echo "========================================"
echo
echo "Submitted: ${PACKAGE_ID} ${VERSION} -> microsoft/winget-pkgs"
echo
echo "The pull request must be reviewed and merged before users can install with:"
echo "  winget install ${PACKAGE_ID}"
echo
