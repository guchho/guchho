#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PACKAGE_ID="Guchho.Guchho"
DEFAULT_OUTDIR="${ROOT_DIR}/package/winget"
WINGET_PKGS_REPO="microsoft/winget-pkgs"

VERSION=""
OUTDIR="${DEFAULT_OUTDIR}"
DRY_RUN=0
VALIDATE=1
BRANCH_NAME=""
PR_TITLE=""

log() { echo "[Guchho] $*"; }
warn() { echo "[WARN] $*" >&2; }
error() { echo; echo "[ERROR] $*" >&2; echo; exit 1; }

usage() {
    cat <<'EOF'
Usage: bash scripts/publish-winget.sh [options]

Submit manifests to microsoft/winget-pkgs using gh CLI.

Options:
  --version <x.y.z>   Version to publish
  --output <dir>      Directory holding manifests/ (default: package/winget)
  --branch <name>     Branch name (default: guchho-<version>)
  --pr-title <title>  PR title (default: Add Guchho version <version>)
  --no-validate       Skip winget validate
  --dry-run           Print commands without executing
  -h, --help          Show help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) VERSION="$2"; shift 2;;
        --version=*) VERSION="${1#*=}"; shift;;
        --output) OUTDIR="$2"; shift 2;;
        --output=*) OUTDIR="${1#*=}"; shift;;
        --branch) BRANCH_NAME="$2"; shift 2;;
        --branch=*) BRANCH_NAME="${1#*=}"; shift;;
        --pr-title) PR_TITLE="$2"; shift 2;;
        --pr-title=*) PR_TITLE="${1#*=}"; shift;;
        --no-validate) VALIDATE=0; shift;;
        --dry-run) DRY_RUN=1; shift;;
        -h|--help) usage; exit 0;;
        *) echo "[ERROR] Unknown option: $1" >&2; usage >&2; exit 2;;
    esac
done

if [[ -z "${VERSION}" ]]; then
    VERSION=$(node -p "require('${ROOT_DIR}/package/npm/guchho/package.json').version")
fi
[[ -z "${BRANCH_NAME}" ]] && BRANCH_NAME="guchho-${VERSION}"
[[ -z "${PR_TITLE}" ]] && PR_TITLE="Add Guchho version ${VERSION}"
MANIFEST_SUBPATH="manifests/g/Guchho/Guchho/${VERSION}"

if ! command -v gh >/dev/null 2>&1; then error "gh CLI not found"; fi
if ! gh auth status >/dev/null 2>&1; then error "gh not authenticated"; fi

VERSION_DIR="${OUTDIR}/${MANIFEST_SUBPATH}"
[[ ! -d "${VERSION_DIR}" ]] && error "Manifests not found at ${VERSION_DIR}"

if [[ "${DRY_RUN}" -eq 1 ]]; then
    log "Would fork ${WINGET_PKGS_REPO}, clone, copy ${VERSION_DIR} to ${MANIFEST_SUBPATH}, commit, push, create PR"
    exit 0
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

gh repo fork "${WINGET_PKGS_REPO}" --clone=false 2>&1 | tail -1 || true
GITHUB_USER="$(gh api user -q .login)"
log "Fork: ${GITHUB_USER}/winget-pkgs"
git clone --depth 1 "https://github.com/${GITHUB_USER}/winget-pkgs.git" "${WORK_DIR}/winget-pkgs" >/dev/null 2>&1
cd "${WORK_DIR}/winget-pkgs"
git remote add upstream "https://github.com/${WINGET_PKGS_REPO}.git" 2>/dev/null || true
git fetch upstream master --depth 1 >/dev/null 2>&1
git checkout -b "${BRANCH_NAME}" upstream/master >/dev/null 2>&1
mkdir -p "${MANIFEST_SUBPATH}"
cp -r "${VERSION_DIR}/"* "${MANIFEST_SUBPATH}/"
git add "${MANIFEST_SUBPATH}"
git -c user.email=bot@users.noreply.github.com -c user.name="bot" commit -m "${PR_TITLE}" >/dev/null 2>&1
git push -u origin "${BRANCH_NAME}" >/dev/null 2>&1
PR_URL=$(gh pr create --repo "${WINGET_PKGS_REPO}" --base master --head "${GITHUB_USER}:${BRANCH_NAME}" --title "${PR_TITLE}" --body "Add ${PACKAGE_ID} ${VERSION}")
echo "PR: ${PR_URL}"
