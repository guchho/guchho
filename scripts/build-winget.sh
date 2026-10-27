#!/usr/bin/env bash

set -euo pipefail

# ========================================
# build-winget.sh
# Generate WinGet manifests for Guchho.
#
# WinGet packages are submitted to the
# microsoft/winget-pkgs repository, so this
# script does not publish anything. It:
#
#   1. Reads the version from the npm package
#   2. Downloads the official win32-x64 release
#      archive (or accepts a local copy)
#   3. Verifies the archive and locates
#      guchho.exe inside it
#   4. Writes the three manifest files under
#      dist/winget/manifests/g/Guchho/Guchho/
#      <version>/
#   5. Validates them with `winget validate`
#      when winget is available
#
# The release's bin/ also carries the three
# MinGW runtime DLLs beside guchho.exe. The
# manifests use a zip installer with a nested
# portable binary, and ArchiveBinariesDependOn-
# Path keeps those DLLs resolvable, so the
# installed command is exactly the released
# binary -- not one rebuilt for WinGet.
#
# Usage:
#   bash scripts/build-winget.sh [options]
# ========================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PACKAGE_ID="Guchho.Guchho"
PACKAGE_LOCALE="en-US"
PLATFORM="win32-x64"
ARCHITECTURE="x64"
MANIFEST_VERSION="1.12.0"

DEFAULT_OUTDIR="${ROOT_DIR}/dist/winget"

# ========================================
# Options
# ========================================

VERSION=""
ARCHIVE=""
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
Usage: bash scripts/build-winget.sh [options]

Generate WinGet manifests for Guchho from the official win32-x64 release
archive. Nothing is uploaded; the manifests are written locally for
review and submission to microsoft/winget-pkgs.

Options:
  --version <x.y.z>   Version to package (default: package/npm/guchho/package.json)
  --archive <path>    Use a local release zip instead of downloading it
  --output <dir>      Output directory (default: dist/winget)
  --no-validate       Skip `winget validate` even when winget is installed
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
        --archive)
            [[ $# -ge 2 ]] || error "--archive needs a value"
            ARCHIVE="$2"
            shift 2
            ;;
        --archive=*)
            ARCHIVE="${1#*=}"
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

if ! command -v unzip >/dev/null 2>&1; then
    error "unzip was not found. It inspects the release archive: https://infozip.sourceforge.net"
fi

SHA256_TOOL=""
if command -v sha256sum >/dev/null 2>&1; then
    SHA256_TOOL="sha256sum"
elif command -v shasum >/dev/null 2>&1; then
    SHA256_TOOL="shasum"
else
    error "No SHA256 tool found (sha256sum or shasum). WinGet requires the installer hash."
fi

if [[ -z "${ARCHIVE}" ]] && ! command -v curl >/dev/null 2>&1; then
    error "curl was not found. It downloads the release archive: https://curl.se"
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
echo " GUCHHO WINGET MANIFESTS"
echo "========================================"
echo
echo "Package  : ${PACKAGE_ID}"
echo "Version  : ${VERSION}"
echo "Platform : ${PLATFORM}"
echo "Output   : ${OUTDIR}"
echo

# One temporary workspace for the whole run, removed by the EXIT trap.
WORK="$(mktemp -d)"

# ========================================
# Obtain the release archive
# ========================================

echo "========================================"
echo "[1/4] Locating the release archive"
echo "========================================"
echo

ARCHIVE_NAME="guchho-${VERSION}-${PLATFORM}.zip"
RELEASE_TAG="v${VERSION}"
RELEASE_URL="https://github.com/guchho/guchho/releases/download/${RELEASE_TAG}/${ARCHIVE_NAME}"

if [[ -z "${ARCHIVE}" ]]; then
    ARCHIVE="${WORK}/${ARCHIVE_NAME}"

    log "Downloading:"
    echo "  ${RELEASE_URL}"
    echo

    if ! curl -fsSL --retry 3 -o "${ARCHIVE}" "${RELEASE_URL}"; then
        error "Could not download ${RELEASE_URL}
        The ${RELEASE_TAG} release must exist and carry the ${ARCHIVE_NAME}
        asset before the WinGet manifests can be generated. Publish it first,
        or pass --archive <path> after downloading it from the workflow."
    fi
else
    [[ -f "${ARCHIVE}" ]] || error "Archive not found: ${ARCHIVE}"
    log "Using local archive: ${ARCHIVE}"
    warn "InstallerUrl still points at ${RELEASE_URL}; the SHA256 is computed from the local archive. Make sure it is the exact asset attached to that release."
fi

if [[ ! -s "${ARCHIVE}" ]]; then
    error "Archive is empty: ${ARCHIVE}"
fi

if ! unzip -t -q "${ARCHIVE}" >/dev/null 2>&1; then
    error "${ARCHIVE} is not a valid zip archive (corrupt or truncated)."
fi

ARCHIVE_LABEL="$(basename "${ARCHIVE}")"

ARCHIVE_SHA256="$(sha256_hex "${ARCHIVE}" | tr '[:lower:]' '[:upper:]')"

echo
log "SHA256: ${ARCHIVE_SHA256}"
echo

# ========================================
# Locate guchho.exe inside the archive
# ========================================

echo "========================================"
echo "[2/4] Inspecting the archive layout"
echo "========================================"
echo

ENTRIES_FILE="${WORK}/entries.txt"
unzip -Z1 "${ARCHIVE}" > "${ENTRIES_FILE}"

EXE_ENTRY="$(grep -E '(^|/)guchho\.exe$' "${ENTRIES_FILE}" | sort | head -1 || true)"

if [[ -z "${EXE_ENTRY}" ]]; then
    error "guchho.exe was not found inside ${ARCHIVE_LABEL}. The archive layout has changed."
fi

if [[ "$(grep -E '(^|/)guchho\.exe$' "${ENTRIES_FILE}" | wc -l | tr -d ' ')" -gt 1 ]]; then
    error "${ARCHIVE_LABEL} contains more than one guchho.exe; refusing to guess which one to install."
fi

if [[ "${EXE_ENTRY}" == */* ]]; then
    EXE_DIR_ENTRY="${EXE_ENTRY%/*}"
else
    EXE_DIR_ENTRY=""
fi

DLL_COUNT=0
while IFS= read -r entry; do
    [[ -n "${entry}" ]] || continue
    [[ "${entry}" == *.dll ]] || continue

    if [[ "${entry}" == */* ]]; then
        entry_dir="${entry%/*}"
    else
        entry_dir=""
    fi

    if [[ "${entry_dir}" == "${EXE_DIR_ENTRY}" ]]; then
        DLL_COUNT=$(( DLL_COUNT + 1 ))
    fi
done < "${ENTRIES_FILE}"

rm -f "${ENTRIES_FILE}"

if [[ "${DLL_COUNT}" -eq 0 ]]; then
    error "No runtime DLLs found beside ${EXE_ENTRY} in ${ARCHIVE_LABEL}.
        guchho.exe cannot load without its MinGW runtime, and an archive with
        only guchho.exe is stale. Rebuild it with npm-publish.yml."
fi

log "Executable : ${EXE_ENTRY}"
log "Runtime DLLs: ${DLL_COUNT} beside it"
echo

# ========================================
# Write the manifests
# ========================================

echo "========================================"
echo "[3/4] Writing manifests"
echo "========================================"
echo

VERSION_DIR="${OUTDIR}/manifests/g/Guchho/Guchho/${VERSION}"
mkdir -p "${VERSION_DIR}"

VERSION_FILE="${VERSION_DIR}/${PACKAGE_ID}.yaml"
INSTALLER_FILE="${VERSION_DIR}/${PACKAGE_ID}.installer.yaml"
LOCALE_FILE="${VERSION_DIR}/${PACKAGE_ID}.locale.${PACKAGE_LOCALE}.yaml"

cat > "${VERSION_FILE}" <<EOF
# yaml-language-server: \$schema=https://aka.ms/winget-manifest.version.${MANIFEST_VERSION}.schema.json

PackageIdentifier: ${PACKAGE_ID}
PackageVersion: ${VERSION}
DefaultLocale: ${PACKAGE_LOCALE}
ManifestType: version
ManifestVersion: ${MANIFEST_VERSION}
EOF

cat > "${INSTALLER_FILE}" <<EOF
# yaml-language-server: \$schema=https://aka.ms/winget-manifest.installer.${MANIFEST_VERSION}.schema.json

PackageIdentifier: ${PACKAGE_ID}
PackageVersion: ${VERSION}
InstallerType: zip
NestedInstallerType: portable
NestedInstallerFiles:
- RelativeFilePath: ${EXE_ENTRY}
  PortableCommandAlias: guchho
ArchiveBinariesDependOnPath: true
Installers:
- Architecture: ${ARCHITECTURE}
  InstallerUrl: ${RELEASE_URL}
  InstallerSha256: ${ARCHIVE_SHA256}
ManifestType: installer
ManifestVersion: ${MANIFEST_VERSION}
EOF

cat > "${LOCALE_FILE}" <<EOF
# yaml-language-server: \$schema=https://aka.ms/winget-manifest.defaultLocale.${MANIFEST_VERSION}.schema.json

PackageIdentifier: ${PACKAGE_ID}
PackageVersion: ${VERSION}
PackageLocale: ${PACKAGE_LOCALE}
Publisher: Guchho
PublisherUrl: https://github.com/guchho
PublisherSupportUrl: https://github.com/guchho/guchho/issues
Author: CodeHemu
PackageName: Guchho
PackageUrl: https://github.com/guchho/guchho
License: MIT
LicenseUrl: https://github.com/guchho/guchho/blob/main/LICENSE
Copyright: Copyright (c) 2026 CodeHemu
ShortDescription: Fast build system for web applications.
Description: Guchho is an all-in-one bundler, compiler, and build tool. It parses HTML, JavaScript, TypeScript, JSX, CSS and assets, and produces a deployable output directory with a single command.
Moniker: guchho
Tags:
- bundler
- build-tool
- javascript
- typescript
- web
ReleaseNotesUrl: https://github.com/guchho/guchho/releases
ManifestType: defaultLocale
ManifestVersion: ${MANIFEST_VERSION}
EOF

log "Wrote:"
echo "  ${VERSION_FILE}"
echo "  ${INSTALLER_FILE}"
echo "  ${LOCALE_FILE}"
echo

# ========================================
# Validate
# ========================================

echo "========================================"
echo "[4/4] Validating"
echo "========================================"
echo

VERSION_DIR_WIN="$(to_winpath "${VERSION_DIR}")"

if [[ "${VALIDATE}" -eq 1 ]] && command -v winget >/dev/null 2>&1; then
    log "Running winget validate ..."
    if ! winget validate --manifest "${VERSION_DIR_WIN}"; then
        error "winget validate rejected the generated manifests.
        Update the Windows Package Manager, or re-run with --no-validate
        to inspect them manually."
    fi
    echo
elif [[ "${VALIDATE}" -eq 1 ]]; then
    warn "winget was not found; skipping validation. Run it on a Windows host with:"
    echo "  winget validate --manifest '${VERSION_DIR_WIN}'"
    echo
else
    log "Validation skipped (--no-validate)."
    echo
fi

# ========================================
# Report
# ========================================

echo "========================================"
echo " BUILD COMPLETE"
echo "========================================"
echo
echo "Manifests: ${VERSION_DIR}"
echo
echo "Test the local install:"
echo "  winget install --manifest '${VERSION_DIR_WIN}'"
echo
echo "To publish, submit these files to microsoft/winget-pkgs under:"
echo "  manifests/g/Guchho/Guchho/${VERSION}/"
echo
echo "Nothing was uploaded by this script."
echo
