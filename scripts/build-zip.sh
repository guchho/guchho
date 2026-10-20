#!/usr/bin/env bash

set -euo pipefail

# ========================================
# build-zip.sh
# Download the published Guchho platform
# packages from the official npm registry
# and package each platform on its own:
#
#   guchho-<version>-<platform>.zip
#     bin/guchho   (or bin/guchho.exe, plus
#                   the MinGW runtime DLLs on
#                   win32, exactly as published)
#     README.md    (official)
#     LICENSE      (official, when available)
#     NOTICE       (official, when available)
#
# Steps:
#   1. Discover platforms from the
#      optionalDependencies of guchho@<version>
#   2. Download each @guchho/<platform> tarball
#      and verify its npm sha512 integrity
#   3. Extract, stage, zip, restore the Unix
#      executable bit that Windows zip builds
#      drop, verify every ZIP with `unzip -t`
#      and the entry list
#   4. Write a SHA-256 checksum beside each ZIP
#
# The script never executes a downloaded
# binary, never publishes or uploads, never
# invents a file that is not published, and
# never overwrites an existing archive without
# --overwrite.
#
# Usage:
#   bash scripts/build-zip.sh [options]
# ========================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

REGISTRY="https://registry.npmjs.org"
DEFAULT_VERSION="1.0.8"

# ========================================
# Options
# ========================================

VERSION="${DEFAULT_VERSION}"
OUTDIR="${ROOT_DIR}/dist/zips"
LIST_PLATFORMS=0
DRY_RUN=0
OVERWRITE=0
REQUESTED=()

WORK=""
MAIN_PKG=""

# ========================================
# Helpers
# ========================================

log() {
    echo "[Guchho] $*"
}

warn() {
    echo "[WARN] $*" >&2
}

die() {
    echo >&2
    echo "[ERROR] $*" >&2
    echo >&2
    exit 1
}

cleanup() {
    if [[ -n "${WORK}" && -d "${WORK}" ]]; then
        rm -rf "${WORK}"
    fi
}

trap cleanup EXIT
trap 'exit 130' INT TERM

usage() {
    cat <<'EOF'
Usage: bash scripts/build-zip.sh [options]

Download the published Guchho platform packages from the official npm
registry (https://registry.npmjs.org/) and create one ZIP archive per
platform: guchho-<version>-<platform>.zip

Options:
  --version <x.y.z>     Version to package (default: 1.0.8)
  --platform <list>     Only these platforms, comma-separated and/or
                        repeated (default: all published platforms)
  --output <dir>        Output directory (default: dist/zips)
  --list-platforms      Print the published platforms and exit
  --dry-run             Resolve everything and print the plan;
                        download no tarballs, write nothing
  --overwrite           Replace archives that already exist
  -h, --help            Show this help and exit

Each archive contains the published bin/ tree (the platform's own
executable name and permissions), README.md, and the official LICENSE
and NOTICE files when the published packages carry them. Every tarball
is checked against its npm dist.integrity before extraction, every ZIP
is checked with `unzip -t` plus an entry-list check, and a SHA-256
checksum is written next to each ZIP.

Nothing is executed, published, or uploaded.
EOF
}

# ========================================
# Argument parsing
# ========================================

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            [[ $# -ge 2 ]] || die "--version needs a value"
            VERSION="$2"
            shift 2
            ;;
        --version=*)
            VERSION="${1#*=}"
            shift
            ;;
        --platform)
            [[ $# -ge 2 ]] || die "--platform needs a value"
            IFS=',' read -ra _parts <<< "$2"
            for _p in "${_parts[@]}"; do
                [[ -n "${_p}" ]] && REQUESTED+=("${_p}")
            done
            shift 2
            ;;
        --platform=*)
            IFS=',' read -ra _parts <<< "${1#*=}"
            for _p in "${_parts[@]}"; do
                [[ -n "${_p}" ]] && REQUESTED+=("${_p}")
            done
            shift
            ;;
        --output)
            [[ $# -ge 2 ]] || die "--output needs a value"
            OUTDIR="$2"
            shift 2
            ;;
        --output=*)
            OUTDIR="${1#*=}"
            shift
            ;;
        --list-platforms)
            LIST_PLATFORMS=1
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --overwrite)
            OVERWRITE=1
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

if [[ ! "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+([-.][0-9A-Za-z.-]+)?$ ]]; then
    die "--version is not a version number: ${VERSION}"
fi

# One temporary workspace for the whole run,
# removed by the EXIT trap. Created before any
# network call so nothing ever writes into the
# system temp directory piecemeal.
WORK="$(mktemp -d)"

# ========================================
# Dependency check
#
# --help already exited. --list-platforms and
# --dry-run resolve metadata only, so they
# need curl alone; a real run needs the full
# toolchain. Every missing tool fails here,
# before any network call, with an install
# hint rather than a mid-run command-not-found.
# ========================================

require_cmd() {
    local cmd="$1" hint="$2"
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        die "${cmd} was not found. ${hint}"
    fi
}

require_cmd curl "Install it: https://curl.se"

SHA512_TOOL=""
SHA256_TOOL=""

if command -v openssl >/dev/null 2>&1; then
    SHA512_TOOL="openssl"
elif command -v sha512sum >/dev/null 2>&1 && command -v base64 >/dev/null 2>&1 && command -v od >/dev/null 2>&1; then
    SHA512_TOOL="coreutils"
fi

if command -v sha256sum >/dev/null 2>&1; then
    SHA256_TOOL="sha256sum"
elif command -v shasum >/dev/null 2>&1; then
    SHA256_TOOL="shasum"
fi

if [[ "${LIST_PLATFORMS}" -eq 0 && "${DRY_RUN}" -eq 0 ]]; then
    require_cmd tar "It extracts the npm tarballs: https://www.gnu.org/software/tar/"
    require_cmd zip "It creates the archives: https://infozip.sourceforge.net"
    require_cmd unzip "It verifies the archives: https://infozip.sourceforge.net"
    require_cmd base64 "It decodes dist.integrity strings (coreutils): https://www.gnu.org/software/coreutils/"
    require_cmd od "It converts digests to hex (coreutils): https://www.gnu.org/software/coreutils/"

    if [[ -z "${SHA512_TOOL}" ]]; then
        die "No sha512 tool found (openssl or sha512sum). npm tarball integrity cannot be verified without one."
    fi

    if [[ -z "${SHA256_TOOL}" ]]; then
        die "No sha256 tool found (sha256sum or shasum). ZIP checksums cannot be written without one."
    fi
fi

sha512_hex() {
    if [[ "${SHA512_TOOL}" == "openssl" ]]; then
        openssl dgst -sha512 -binary "$1" | od -An -tx1 | tr -d ' \n'
    else
        sha512sum "$1" | cut -d' ' -f1
    fi
}

sha256_hex() {
    if [[ "${SHA256_TOOL}" == "sha256sum" ]]; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

# Verify a downloaded tarball against the
# dist.integrity string npm published for it
# ("sha512-<base64>"). Both sides are compared
# as hex digests. A missing or non-sha512
# integrity field is a failure, not a reason
# to trust the download.
verify_integrity() {
    local file="$1" integrity="$2"

    if [[ "${integrity}" != sha512-* ]]; then
        warn "unsupported integrity algorithm: ${integrity%%-*}"
        return 1
    fi

    local expected_hex
    expected_hex="$(printf '%s' "${integrity#sha512-}" | base64 -d 2>/dev/null | od -An -tx1 | tr -d ' \n')" || true

    if [[ -z "${expected_hex}" ]]; then
        warn "could not decode dist.integrity"
        return 1
    fi

    local actual_hex
    actual_hex="$(sha512_hex "${file}")"

    [[ "${actual_hex}" == "${expected_hex}" ]]
}

json_field() {
    # First "key":"value" occurrence in a JSON document.
    grep -oE "\"$1\":\"[^\"]*\"" "$2" | head -1 | cut -d'"' -f4
}

# ========================================
# Platform discovery
#
# Only the optionalDependencies object of
# guchho@<version> counts. The peer
# dependency @guchho/wasm is a WebAssembly
# package, not a platform package, and must
# not end up in the list.
# ========================================

echo "========================================"
echo " GUCHHO PLATFORM ZIPS"
echo "========================================"
echo
echo "Version  : ${VERSION}"
echo "Registry : ${REGISTRY}"
echo

log "Discovering platforms from guchho@${VERSION} ..."

META_FILE="$(mktemp)"
if ! curl -fsSL --retry 3 -o "${META_FILE}" "${REGISTRY}/guchho/${VERSION}"; then
    rm -f "${META_FILE}"
    die "guchho@${VERSION} was not found on ${REGISTRY}.
        Check the version, or publish it first."
fi

if ! grep -q "\"version\":\"${VERSION}\"" "${META_FILE}"; then
    rm -f "${META_FILE}"
    die "The registry document for guchho@${VERSION} reports a different version. Refusing to continue."
fi

DISCOVERED="$(tr -d '\n' < "${META_FILE}" \
    | sed -n 's/.*"optionalDependencies"[[:space:]]*:[[:space:]]*{\([^}]*\)}.*/\1/p' \
    | grep -oE '"@guchho/[a-z0-9-]+":"[^"]*"' \
    | sed 's/"@guchho\///; s/":/|/; s/"//g' \
    | sort)"

rm -f "${META_FILE}"

if [[ -z "${DISCOVERED}" ]]; then
    die "No optionalDependencies found in guchho@${VERSION}. Nothing to package."
fi

DISCOVERED_LIST=()
while IFS='|' read -r name dep_version; do
    [[ -n "${name}" ]] && DISCOVERED_LIST+=("${name}|${dep_version}")
done <<< "${DISCOVERED}"

if [[ "${LIST_PLATFORMS}" -eq 1 ]]; then
    echo "Published platforms for guchho@${VERSION}:"
    echo
    for entry in "${DISCOVERED_LIST[@]}"; do
        echo "  ${entry%%|*}"
    done
    echo
    echo "${#DISCOVERED_LIST[@]} platform(s)."
    exit 0
fi

# ========================================
# Platform selection
# ========================================

PLATFORMS=()

if [[ ${#REQUESTED[@]} -eq 0 ]]; then
    for entry in "${DISCOVERED_LIST[@]}"; do
        PLATFORMS+=("${entry%%|*}")
    done
else
    for want in "${REQUESTED[@]}"; do
        found=0
        for entry in "${DISCOVERED_LIST[@]}"; do
            if [[ "${entry%%|*}" == "${want}" ]]; then
                PLATFORMS+=("${want}")
                found=1
                break
            fi
        done
        if [[ "${found}" -eq 0 ]]; then
            die "Platform not published for guchho@${VERSION}: ${want}
Available platforms:
$(printf '  %s\n' "${DISCOVERED_LIST[@]%%|*}")"
        fi
    done
fi

echo "Platforms : ${#PLATFORMS[@]} selected"
echo "Output    : ${OUTDIR}"
echo

# ========================================
# Shared official documents
#
# LICENSE and NOTICE are not inside the
# platform tarballs; they ship in the main
# guchho package. Download it once. If it
# cannot be fetched, the archives simply
# carry no LICENSE/NOTICE - a missing file
# is never invented or substituted.
# ========================================

fetch_main_docs() {
    local meta main_tgz integrity exdir
    meta="$(mktemp)"
    exdir="${WORK}/main"

    if ! curl -fsSL --retry 3 -o "${meta}" "${REGISTRY}/guchho/${VERSION}"; then
        rm -f "${meta}"
        return 1
    fi

    main_tgz="$(json_field tarball "${meta}")"
    integrity="$(json_field integrity "${meta}")"
    rm -f "${meta}"

    if [[ -z "${main_tgz}" ]]; then
        return 1
    fi

    mkdir -p "${exdir}"
    if ! curl -fsSL --retry 3 -o "${WORK}/main.tgz" "${main_tgz}"; then
        return 1
    fi

    if [[ -n "${integrity}" ]] && ! verify_integrity "${WORK}/main.tgz" "${integrity}"; then
        warn "guchho@${VERSION} main tarball failed integrity verification; LICENSE/NOTICE will be omitted"
        rm -f "${WORK}/main.tgz"
        return 1
    fi

    if ! tar -xzf "${WORK}/main.tgz" -C "${exdir}"; then
        return 1
    fi

    MAIN_PKG="${exdir}/package"
    [[ -d "${MAIN_PKG}" ]]
}

find_doc() {
    # $1: directory, $2..: candidate names
    local dir="$1"
    shift
    local name
    for name in "$@"; do
        if [[ -f "${dir}/${name}" ]]; then
            echo "${dir}/${name}"
            return 0
        fi
    done
    return 1
}

# ========================================
# ZIP central-directory permission repair
#
# zip(1) on Windows stores MS-DOS file attributes and
# drops the Unix mode bits, so an executable that was
# chmod 755 before zipping extracts on Linux without its
# execute bit. set_zip_exec_mode rewrites the central-
# directory entry of the staged executable to carry
# host OS "Unix" (3) and mode 0100755. Local headers
# and compressed data are untouched; unzip(1) and every
# mainstream ZIP reader take file attributes from the
# central directory. The patch is idempotent: on systems
# where zip already preserved the mode it writes the
# same bytes back.
# ========================================

read_u16le() {
    local file="$1" off="$2"
    local b0 b1
    read -r b0 b1 < <(od -An -tu1 -j "${off}" -N 2 "${file}")
    echo $(( b0 + (b1 << 8) ))
}

read_u32le() {
    local file="$1" off="$2"
    local b0 b1 b2 b3
    read -r b0 b1 b2 b3 < <(od -An -tu1 -j "${off}" -N 4 "${file}")
    echo $(( b0 + (b1 << 8) + (b2 << 16) + (b3 << 24) ))
}

write_bytes_at() {
    # write_bytes_at FILE OFFSET b0 b1 ...  (decimal bytes).
    # The bytes travel as printf octal escapes inside a
    # format string: command substitution cannot carry NUL
    # bytes, and printf '\\x..' emits those two characters
    # literally instead of the byte.
    local file="$1" off="$2"
    shift 2
    local fmt="" b
    for b in "$@"; do
        fmt+="\\$(printf '%03o' "${b}")"
    done
    printf "${fmt}" | dd of="${file}" bs=1 seek="${off}" conv=notrunc 2>/dev/null
}

set_zip_exec_mode() {
    # set_zip_exec_mode ZIPFILE ENTRY
    # Store ENTRY with Unix mode 0100755 in the central
    # directory. Returns non-zero if the archive cannot be
    # parsed, the entry is missing, or the write did not
    # read back correctly.
    local file="$1" target="$2"

    local size
    size=$(( $(wc -c < "${file}") ))

    # The EOCD record is the last PK\x05\x06 whose comment
    # length lands exactly on end-of-file.
    local eocd="" cand clen
    while IFS= read -r cand; do
        clen="$(read_u16le "${file}" $((cand + 20)))"
        if (( cand + 22 + clen == size )); then
            eocd="${cand}"
            break
        fi
    done < <(LC_ALL=C grep -aob $'PK\x05\x06' "${file}" | cut -d: -f1)

    [[ -n "${eocd}" ]] || return 1

    local total cd_off
    total="$(read_u16le "${file}" $((eocd + 10)))"
    cd_off="$(read_u32le "${file}" $((eocd + 16)))"

    local pos="${cd_off}" i sig fn_len extra_len comment_len name
    for (( i = 0; i < total; i++ )); do
        sig="$(od -An -tx1 -j "${pos}" -N 4 "${file}" | tr -d ' \n')"
        [[ "${sig}" == "504b0102" ]] || return 1
        fn_len="$(read_u16le "${file}" $((pos + 28)))"
        extra_len="$(read_u16le "${file}" $((pos + 30)))"
        comment_len="$(read_u16le "${file}" $((pos + 32)))"
        name="$(tail -c "+$((pos + 46 + 1))" "${file}" | head -c "${fn_len}")"

        if [[ "${name}" == "${target}" ]]; then
            # Version made by: low byte stays the zip spec
            # version, high byte becomes 3 (Unix).
            local spec
            spec="$(od -An -tu1 -j "${pos}" -N 1 "${file}" | tr -d ' ')"
            write_bytes_at "${file}" "${pos}" "${spec}" 3

            # External attributes = (0100755 << 16),
            # stored little-endian.
            local mode=$(( 8#100755 << 16 ))
            write_bytes_at "${file}" $((pos + 38)) \
                $(( mode & 255 )) \
                $(( (mode >> 8) & 255 )) \
                $(( (mode >> 16) & 255 )) \
                $(( (mode >> 24) & 255 ))

            # Read the header back; a short write or a full
            # disk must fail the platform, not ship a lie.
            local host attrs
            host="$(read_u16le "${file}" "${pos}")"
            attrs="$(read_u32le "${file}" $((pos + 38)))"
            (( (host >> 8) == 3 && attrs == mode )) || return 1
            return 0
        fi

        pos=$(( pos + 46 + fn_len + extra_len + comment_len ))
    done

    return 1
}

# ========================================
# Per-platform processing
# ========================================

RESULTS_NAME=()
RESULTS_STATUS=()
RESULTS_DETAIL=()

record() {
    RESULTS_NAME+=("$1")
    RESULTS_STATUS+=("$2")
    RESULTS_DETAIL+=("$3")
}

# Reject path traversal before extracting.
# npm tarballs are trusted-signed, but a
# tar that writes outside its directory is
# never acceptable regardless of source.
safe_extract() {
    local tgz="$1" dest="$2"

    if tar -tzf "${tgz}" | grep -qE '(^/|(^|/)\.\.(/|$))'; then
        warn "tarball contains an unsafe path; refusing to extract"
        return 1
    fi

    mkdir -p "${dest}"
    tar -xzf "${tgz}" -C "${dest}"
}

process_platform() {
    local platform="$1" dep_version="$2" index="$3" total="$4"

    echo "----------------------------------------"
    echo "[${index}/${total}] ${platform}"
    echo "----------------------------------------"

    # A platform pinned to a different version than requested is a
    # failure for this run, not a silent substitution.
    if [[ "${dep_version}" != "${VERSION}" ]]; then
        warn "optionalDependencies pins ${platform} to ${dep_version}, not ${VERSION} -- refusing to substitute"
        return 1
    fi

    local meta="${WORK}/${platform}.meta.json"
    if ! curl -fsSL --retry 3 -o "${meta}" "${REGISTRY}/@guchho%2f${platform}/${VERSION}"; then
        warn "no registry document for @guchho/${platform}@${VERSION} (not published?)"
        return 1
    fi

    if ! grep -q "\"version\":\"${VERSION}\"" "${meta}"; then
        warn "registry document for ${platform} reports a different version; refusing to substitute"
        return 1
    fi

    local tarball integrity
    tarball="$(json_field tarball "${meta}")"
    integrity="$(json_field integrity "${meta}")"

    if [[ -z "${tarball}" ]]; then
        warn "${platform}: registry document has no dist.tarball"
        return 1
    fi

    if [[ -z "${integrity}" ]]; then
        warn "${platform}: registry document has no dist.integrity; refusing an unverified download"
        return 1
    fi

    if [[ "${DRY_RUN}" -eq 1 ]]; then
        echo "  would download : ${tarball}"
        echo "  would verify   : ${integrity}"
        echo "  would create   : ${OUTDIR}/guchho-${VERSION}-${platform}.zip"
        return 0
    fi

    local tgz="${WORK}/${platform}.tgz"
    log "Downloading ${tarball}"
    if ! curl -fsSL --retry 3 -o "${tgz}" "${tarball}"; then
        warn "${platform}: download failed"
        return 1
    fi

    if ! verify_integrity "${tgz}" "${integrity}"; then
        warn "${platform}: sha512 integrity mismatch -- the download is not the published artifact"
        return 1
    fi
    log "Integrity OK (${integrity:0:24}...)"

    local exdir="${WORK}/${platform}/extract"
    if ! safe_extract "${tgz}" "${exdir}"; then
        warn "${platform}: extraction failed"
        return 1
    fi

    local pkg="${exdir}/package"
    if [[ ! -d "${pkg}" ]]; then
        warn "${platform}: tarball has no package/ directory; layout changed"
        return 1
    fi

    # The executable keeps the name the platform package published.
    local exe=""
    if [[ -f "${pkg}/bin/guchho.exe" ]]; then
        exe="guchho.exe"
    elif [[ -f "${pkg}/bin/guchho" ]]; then
        exe="guchho"
    else
        warn "${platform}: published package has no bin/guchho[.exe]; layout changed"
        return 1
    fi

    if [[ ! -s "${pkg}/bin/${exe}" ]]; then
        warn "${platform}: published executable is empty"
        return 1
    fi

    # ---- Stage ----
    local stage="${WORK}/${platform}/stage"
    mkdir -p "${stage}/bin"
    cp -R "${pkg}/bin/." "${stage}/bin/"

    if [[ "${exe}" == "guchho" ]]; then
        chmod 755 "${stage}/bin/guchho"
    fi

    local entries=("bin")
    local readme_src=""
    if readme_src="$(find_doc "${pkg}" README.md)"; then
        cp "${readme_src}" "${stage}/README.md"
    elif [[ -n "${MAIN_PKG}" ]] && readme_src="$(find_doc "${MAIN_PKG}" README.md)"; then
        cp "${readme_src}" "${stage}/README.md"
    else
        warn "${platform}: no official README.md found; omitting it"
    fi
    [[ -f "${stage}/README.md" ]] && entries+=("README.md")

    local doc_src=""
    if [[ -n "${MAIN_PKG}" ]]; then
        if doc_src="$(find_doc "${MAIN_PKG}" LICENSE LICENSE.txt LICENSE.md)"; then
            cp "${doc_src}" "${stage}/LICENSE"
            entries+=("LICENSE")
        else
            warn "no official LICENSE found in guchho@${VERSION}; omitting it"
        fi

        if doc_src="$(find_doc "${MAIN_PKG}" NOTICE NOTICE.txt)"; then
            cp "${doc_src}" "${stage}/NOTICE"
            entries+=("NOTICE")
        fi
    else
        warn "${platform}: main package unavailable; LICENSE/NOTICE omitted"
    fi

    # ---- Zip ----
    local zipname="guchho-${VERSION}-${platform}.zip"
    local zipfile="${WORK}/${zipname}"

    rm -f "${zipfile}"
    if ! (cd "${stage}" && zip -X -q -r "${zipfile}" "${entries[@]}"); then
        warn "${platform}: zip creation failed"
        rm -f "${zipfile}"
        return 1
    fi

    # Windows zip builds store DOS attributes and drop the
    # Unix mode; restore the executable bit before anything
    # hashes or verifies the archive. win32 archives keep
    # their DOS attributes untouched.
    if [[ "${exe}" == "guchho" ]]; then
        if ! set_zip_exec_mode "${zipfile}" "bin/guchho"; then
            warn "${platform}: could not store bin/guchho as executable in ${zipname}"
            rm -f "${zipfile}"
            return 1
        fi
    fi

    # ---- Verify ----
    if ! unzip -t -q "${zipfile}" >/dev/null 2>&1; then
        warn "${platform}: unzip -t failed on ${zipname}; archive is corrupt"
        rm -f "${zipfile}"
        return 1
    fi

    local archive_entries
    archive_entries="$(unzip -l "${zipfile}" | awk '{print $NF}')"

    local required
    for required in "bin/${exe}"; do
        if ! grep -qxF "${required}" <<< "${archive_entries}"; then
            warn "${platform}: ${zipname} is missing required entry ${required}"
            rm -f "${zipfile}"
            return 1
        fi
    done

    local staged
    for staged in "${entries[@]}"; do
        if [[ "${staged}" == "bin" ]]; then
            continue
        fi
        if ! grep -qxF "${staged}" <<< "${archive_entries}"; then
            warn "${platform}: ${zipname} is missing required entry ${staged}"
            rm -f "${zipfile}"
            return 1
        fi
    done

    # Independent of the in-place repair above: ask zipinfo
    # how the entry will actually be interpreted. Skipped
    # when zipinfo is not installed; the repair already
    # verified its own write.
    if [[ "${exe}" == "guchho" ]] && command -v zipinfo >/dev/null 2>&1; then
        local perm
        perm="$(zipinfo -l "${zipfile}" 2>/dev/null \
            | awk '$NF == "bin/guchho" { print $1; exit }')"
        if [[ "${perm}" != -rwx?x?x? ]]; then
            warn "${platform}: ${zipname} does not store bin/guchho as rwxr-xr-x (got: ${perm:-missing})"
            rm -f "${zipfile}"
            return 1
        fi
    fi

    # ---- Place ----
    mkdir -p "${OUTDIR}"
    local final="${OUTDIR}/${zipname}"

    if [[ -e "${final}" || -e "${final}.sha256" ]] && [[ "${OVERWRITE}" -eq 0 ]]; then
        warn "${final} already exists (use --overwrite to replace it)"
        rm -f "${zipfile}"
        return 1
    fi

    local hash
    hash="$(sha256_hex "${zipfile}")"

    mv -f "${zipfile}" "${final}"
    printf '%s  %s\n' "${hash}" "${zipname}" > "${final}.sha256"

    local size="unknown"
    if command -v du >/dev/null 2>&1; then
        size="$(du -h "${final}" | cut -f1)"
    fi

    log "Created ${final} (${size})"
    log "SHA256: ${hash}"

    return 0
}

# ========================================
# Main
# ========================================

main() {
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        log "Dry run: metadata only, nothing will be written."
        echo
    else
        if ! fetch_main_docs; then
            warn "Could not fetch guchho@${VERSION} for LICENSE/NOTICE; continuing without them."
            MAIN_PKG=""
        fi
        echo
    fi

    local i=0
    local failures=0
    local entry platform dep_version

    for entry in "${PLATFORMS[@]}"; do
        i=$(( i + 1 ))
        platform="${entry}"

        dep_version=""
        for d in "${DISCOVERED_LIST[@]}"; do
            if [[ "${d%%|*}" == "${platform}" ]]; then
                dep_version="${d#*|}"
                break
            fi
        done

        if process_platform "${platform}" "${dep_version}" "${i}" "${#PLATFORMS[@]}"; then
            if [[ "${DRY_RUN}" -eq 1 ]]; then
                record "${platform}" "PLANNED" "guchho-${VERSION}-${platform}.zip"
            else
                record "${platform}" "OK" "guchho-${VERSION}-${platform}.zip"
            fi
        else
            record "${platform}" "FAILED" "see warnings above"
            failures=$(( failures + 1 ))
        fi
        echo
    done

    # ---- Report ----
    echo "========================================"
    echo " REPORT - guchho@${VERSION}"
    echo "========================================"
    echo
    printf '%-20s %-8s %s\n' "PLATFORM" "STATUS" "ARTIFACT"
    printf '%-20s %-8s %s\n' "--------" "------" "--------"

    local idx
    for idx in "${!RESULTS_NAME[@]}"; do
        printf '%-20s %-8s %s\n' \
            "${RESULTS_NAME[$idx]}" "${RESULTS_STATUS[$idx]}" "${RESULTS_DETAIL[$idx]}"
    done

    echo

    if [[ "${DRY_RUN}" -eq 1 ]]; then
        echo "Dry run complete: ${#RESULTS_NAME[@]} platform(s) planned, ${failures} failed. Nothing was written."
    elif [[ "${failures}" -eq 0 ]]; then
        echo "Done: ${#RESULTS_NAME[@]} archive(s) in ${OUTDIR}"
        echo "Checksums: ${OUTDIR}/guchho-${VERSION}-<platform>.zip.sha256"
    else
        echo "Finished with failures: $(( ${#RESULTS_NAME[@]} - failures )) succeeded, ${failures} failed."
    fi
    echo

    if [[ "${failures}" -gt 0 ]]; then
        return 1
    fi
    return 0
}

main
