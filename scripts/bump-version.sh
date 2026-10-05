#!/usr/bin/env bash

set -euo pipefail

# ========================================
# bump-version.sh
#
# Update the version across every place the repository carries it:
#   - CMakeLists.txt                              project(VERSION)
#   - package/npm/guchho/package.json             version + @guchho/* pins
#   - package/npm/@guchho/*/package.json          version, one per platform
#   - package/chocolatey/guchho/guchho.nuspec     <version>
#   - CONTRIBUTING.md                             version table + naming example
#   - src/cli/cli_run.cpp                         --version doc comment
#   - .vscode/c_cpp_properties.json               GUCHHO_VERSION_STRING define
#
# Deliberately NOT this script's business:
#
#   package/chocolatey/guchho/tools/VERIFICATION.txt
#       Templated as {VERSION} and filled in by scripts/build-chocolatey.sh
#       at pack time. The release URL names an artifact that does not exist
#       until the build has run, so the build owns the number, not the bump.
#       (Before the retemplate it held a literal, and build-chocolatey.sh's
#       placeholder replace was a silent no-op against it.)
#
#   src/cli/cli_help.cpp
#       Its GUCHHO_VERSION_STRING fallback is a sentinel and must stay
#       different from the real version. A fallback that matches the
#       release is the signal that the define stopped reaching guchho_cli,
#       which is exactly what package/npm/test/version.test.js asserts.
#
#   src/api/windows.rc.in, src/api/Info.plist.in
#       CMake substitutes @PROJECT_VERSION@ from the project() call above.
#
#   compile_commands.json, package/chocolatey/guchho/guchho.*.nupkg
#       Generated, and gitignored.
#
# Usage:
#   ./scripts/bump-version.sh <version>
#   ./scripts/bump-version.sh --verify-only
#
# Example:
#   ./scripts/bump-version.sh 1.2.3
#
# Every edit is read back off disk and checked before the script reports
# success, so an edit that silently did nothing fails here rather than in a
# published package. --verify-only runs those same checks against the tree
# without writing, so CI can assert agreement independently of a bump.
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

require_node() {
    if ! command -v node >/dev/null 2>&1; then
        error "node is required to re-pad the CONTRIBUTING.md table. Install Node 18+ and re-run."
    fi
}

# Versions go into sed patterns, where a dot matches any character. "1.0.1"
# would still only ever match "1.0.1" here, but a pattern that means what it
# says is worth the two lines. Everything non-alphanumeric is escaped rather
# than a hand-picked class, so a pre-release tag or a stray backslash in a
# captured value cannot quietly turn into a sed metacharacter.
escape_re() {
    printf '%s' "$1" | sed 's/[^A-Za-z0-9]/\\&/g'
}

# project(VERSION x.y.z) in the top-level CMakeLists.txt -- the one place the
# version is declared rather than repeated.
#
# Anchored to a bare VERSION keyword on its own line, which is the shape
# project() takes and the shape this script writes. The unanchored form also
# matched nothing else today only because cmake_minimum_required() spells its
# 3.25 with two components; that is not a thing to rely on.
cmake_version() {
    grep -oP '^\s*VERSION\s+\K\S+' "$ROOT_DIR/CMakeLists.txt" | head -1 || true
}

json_version() {
    grep -oP '"version"\s*:\s*"\K[^"]+' "$1" | head -1 || true
}


# ========================================
# Parse arguments
# ========================================

VERIFY_ONLY=0

if [[ $# -eq 1 && "$1" == "--verify-only" ]]; then
    VERIFY_ONLY=1
    # Nothing to bump to, so the number being verified against is the one
    # CMakeLists.txt declares -- the same one every other location is
    # expected to agree with.
    VERSION=$(cmake_version)
    if [[ -z "$VERSION" ]]; then
        error "Could not read a VERSION out of ${ROOT_DIR}/CMakeLists.txt"
    fi
    info "Verifying that every location says ${VERSION}"
elif [[ $# -ne 1 ]]; then
    error "Usage: $0 <version>\n       $0 --verify-only\n\n  Example: $0 1.2.3"
else
    VERSION="$1"

    # Validate semver format (basic: X.Y.Z with optional pre-release/build metadata)
    if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9.]+)?(\+[a-zA-Z0-9.]+)?$ ]]; then
        error "Invalid version format: '$VERSION'\n  Expected semver like: 1.2.3, 0.1.0-beta.1, 1.0.0+build.1"
    fi

    info "Bumping version to ${VERSION}"
fi


# ========================================
# Verify
# ========================================
#
# Reads every location back off disk and asserts it agrees. This is the
# failure this script is built around: not a wrong number, but a set of right
# numbers. A wrapper at 1.0.1 that installs a binary calling itself 1.0.0
# gives a bug report no way to reproduce, because the two halves of the
# install disagree about what was installed. Nothing enforces the agreement
# at build time -- the version define is a macro, the manifests are JSON, and
# nothing ties them together.
#
# package/npm/test/version.test.js says the same thing from the other side.
# This says it here, at bump time, so a broken bump never reaches a commit.

CHECK_FAILURES=0

check_eq() {
    # check_eq <label> <expected> <actual>
    if [[ "$2" == "$3" ]]; then
        printf "  ${GREEN}ok${NC}    %-40s %s\n" "$1" "$3"
    else
        printf "  ${RED}FAIL${NC}  %-40s want %s, got %s\n" "$1" "$2" "$3"
        CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi
}

check_contains() {
    # check_contains <label> <needle> <haystack>
    if [[ "$3" == *"$2"* ]]; then
        printf "  ${GREEN}ok${NC}    %-40s contains %s\n" "$1" "$2"
    else
        printf "  ${RED}FAIL${NC}  %-40s want something containing %s, got: %s\n" "$1" "$2" "$3"
        CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi
}

check_not_eq() {
    # check_not_eq <label> <unwanted> <actual>
    if [[ "$2" != "$3" ]]; then
        printf "  ${GREEN}ok${NC}    %-40s differs from %s (%s)\n" "$1" "$2" "$3"
    else
        printf "  ${RED}FAIL${NC}  %-40s equals %s\n" "$1" "$2"
        printf "  ${RED}      ${NC}                                      GUCHHO_VERSION_STRING is probably no longer reaching guchho_cli\n"
        CHECK_FAILURES=$((CHECK_FAILURES + 1))
    fi
}

verify_versions() {
    local key pins_total pins_stale platforms

    echo ""
    echo "========================================"
    echo "[verify] Every location must say ${VERSION}"
    echo "========================================"

    check_eq "CMakeLists.txt project(VERSION)" \
        "$VERSION" "$(cmake_version)"

    check_eq "package/npm/guchho/package.json" \
        "$VERSION" "$(json_version "$ROOT_DIR/package/npm/guchho/package.json")"

    # Counts are reported rather than assumed. Twenty-five platform manifests
    # where there should be twenty-six is a deletion, not a pass.
    platforms=0
    for pkg_dir in "$ROOT_DIR"/package/npm/@guchho/*/; do
        key=$(basename "$pkg_dir")
        check_eq "@guchho/${key} manifest" \
            "$VERSION" "$(json_version "${pkg_dir}package.json")"
        platforms=$((platforms + 1))
    done

    pins_total=$(grep -cP '^\s*"@guchho/[^"]+":\s*"' "$ROOT_DIR/package/npm/guchho/package.json" || true)
    pins_stale=$(grep -oP '^\s*"@guchho/[^"]+":\s*"\K[^"]+' "$ROOT_DIR/package/npm/guchho/package.json" | grep -cvFx "$VERSION" || true)
    check_eq "@guchho/* pins in guchho/package.json" \
        "0 stale of ${pins_total}" "${pins_stale} stale of ${pins_total}"

    check_eq "guchho.nuspec <version>" \
        "$VERSION" "$(grep -oP '<version>\K[^<]+' "$ROOT_DIR/package/chocolatey/guchho/guchho.nuspec" | head -1 || true)"

    check_eq "CONTRIBUTING.md version table" \
        "$VERSION" "$(grep -oP '^\| Current version +\|\s*\K[0-9]+\.[0-9]+\.[0-9]+[^\s|]*' "$ROOT_DIR/CONTRIBUTING.md" | head -1 || true)"

    check_eq "CONTRIBUTING.md naming example" \
        "$VERSION" "$(grep -oP 'guchho-\K[0-9]+\.[0-9]+\.[0-9]+[^-[:space:]]*' "$ROOT_DIR/CONTRIBUTING.md" | head -1 || true)"

    check_eq "src/cli/cli_run.cpp --version comment" \
        "$VERSION" "$(grep -oP 'guchho v\K[0-9]+\.[0-9]+\.[0-9]+[^"]*' "$ROOT_DIR/src/cli/cli_run.cpp" | head -1 || true)"

    check_eq ".vscode/c_cpp_properties.json define" \
        "$VERSION" "$(grep -oP 'GUCHHO_VERSION_STRING=\\?"?\K[^"]+' "$ROOT_DIR/.vscode/c_cpp_properties.json" | head -1 || true)"

    # Build-templated, so it must hold the placeholder and NOT a literal --
    # a literal here is the staleness build-chocolatey.sh used to leave behind.
    check_contains "VERIFICATION.txt is templated" "{VERSION}" \
        "$(grep -m1 'releases/download/' "$ROOT_DIR/package/chocolatey/guchho/tools/VERIFICATION.txt" || true)"

    check_not_eq "src/cli/cli_help.cpp fallback" "$VERSION" \
        "$(grep -oP '#define\s+GUCHHO_VERSION_STRING\s+"\K[^"]+' "$ROOT_DIR/src/cli/cli_help.cpp" | head -1 || true)"

    echo ""
    if [[ "$CHECK_FAILURES" -gt 0 ]]; then
        error "${CHECK_FAILURES} location(s) disagree about the version -- see the FAIL lines above"
    fi
    info "All locations agree on ${VERSION} (${platforms} platform manifests, ${pins_total} dependency pins)"
}


# ========================================
# Update CMakeLists.txt
# ========================================

if [[ "$VERIFY_ONLY" -eq 0 ]]; then

CMAKE_FILE="$ROOT_DIR/CMakeLists.txt"
OLD_CMAKE_VERSION=$(cmake_version)

if [[ -z "$OLD_CMAKE_VERSION" ]]; then
    error "CMakeLists.txt: no VERSION field to update"
fi

if [[ "$OLD_CMAKE_VERSION" != "$VERSION" ]]; then
    # Anchored to the whole line so the indentation project() uses survives
    # and nothing else in the file can be caught by the substitution.
    sed -i "s/^\([[:space:]]*VERSION[[:space:]]*\)$(escape_re "$OLD_CMAKE_VERSION")[[:space:]]*\$/\1${VERSION}/" "$CMAKE_FILE"
    info "  CMakeLists.txt: ${OLD_CMAKE_VERSION} -> ${VERSION}"
fi


# ========================================
# Update main guchho package.json
# ========================================

MAIN_PKG="$ROOT_DIR/package/npm/guchho/package.json"

if [[ -f "$MAIN_PKG" ]]; then
    OLD_MAIN_VERSION=$(json_version "$MAIN_PKG")

    if [[ -z "$OLD_MAIN_VERSION" ]]; then
        error "${MAIN_PKG}: no \"version\" field"
    fi

    # Update package version using single-quoted sed
    sed -i '0,/"version": "'"${OLD_MAIN_VERSION}"'"/s//"version": "'"${VERSION}"'"/' "$MAIN_PKG"

    # Update all @guchho/* optional dependency versions
    sed -i -E "/\"@guchho\// s/: \"[^\"]*\"/: \"${VERSION}\"/" "$MAIN_PKG"

    info "  guchho/package.json: ${OLD_MAIN_VERSION} -> ${VERSION}"
else
    warn "  guchho/package.json: not found, skipped"
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
            OLD_PKG_VERSION=$(json_version "$pkg_json")

            if [[ -z "$OLD_PKG_VERSION" ]]; then
                warn "  @guchho/${pkg_name}: no \"version\" field, skipped"
                continue
            fi

            sed -i '0,/"version": "'"${OLD_PKG_VERSION}"'"/s//"version": "'"${VERSION}"'"/' "$pkg_json"
            info "  @guchho/${pkg_name}: ${OLD_PKG_VERSION} -> ${VERSION}"
        fi
    done
fi


# ========================================
# Update the chocolatey nuspec
# ========================================
#
# It carries its own version element, and it was the one place the bump did not
# reach: a release that moved every package.json and left this at the previous
# version published a chocolatey package whose own metadata disagreed with the
# binary inside it.
#
# scripts/build-chocolatey.sh rewrites this at pack time too, from the version
# in package/npm/guchho/package.json. Both exist on purpose -- the checked-in
# copy is what a reader sees in git, the build-time one is what guarantees the
# packed artifact cannot disagree with what was actually built.

NUSPEC="$ROOT_DIR/package/chocolatey/guchho/guchho.nuspec"

if [[ -f "$NUSPEC" ]]; then
    OLD_NUSPEC_VERSION=$(grep -oP '<version>\K[^<]+' "$NUSPEC" | head -1 || true)

    if [[ -n "$OLD_NUSPEC_VERSION" && "$OLD_NUSPEC_VERSION" != "$VERSION" ]]; then
        sed -i "0,/<version>${OLD_NUSPEC_VERSION}/s//<version>${VERSION}/" "$NUSPEC"
        info "  guchho.nuspec: ${OLD_NUSPEC_VERSION} -> ${VERSION}"
    fi
fi


# ========================================
# Update CONTRIBUTING.md
# ========================================
#
# Two places. The table row is the project's advertised version and the one a
# contributor checks before trusting a tag; the naming example is the shape of
# an artifact name. Both are edited together because a reader who lands on
# either has to find the number they just published.
#
# The table row is re-padded rather than substituted. Every other cell in that
# block is padded to a fixed column width, so swapping 1.0.1 for 1.10.0 without
# adjusting the trailing spaces moves the closing pipe and the block stops
# lining up -- the kind of drift that survives review because markdown still
# renders it.

CONTRIB="$ROOT_DIR/CONTRIBUTING.md"

if [[ -f "$CONTRIB" ]]; then
    require_node

    # Run from the repo root with a relative path: node on Windows will not
    # open the /c/Users/... form that $ROOT_DIR carries under Git Bash.
    # Run from the repo root with a relative path: node on Windows will not
    # open the /c/Users/... form that $ROOT_DIR carries under Git Bash.
    #
    # The line terminator is split off and put back per line. CONTRIBUTING.md
    # is CRLF in the working tree, and an anchored $ would not match past the
    # \r -- and joining the lines back with "\n" would rewrite all 571 of them,
    # turning one changed row into a whole-file diff.
    ( cd "$ROOT_DIR" && node -e '
        const fs = require("fs");
        const file = process.argv[1];
        const next = process.argv[2];

        let table = 0;
        let example = 0;

        const lines = fs.readFileSync(file, "utf8").split("\n").map((line) => {
            const cr = line.endsWith("\r");
            const body = cr ? line.slice(0, -1) : line;

            const done = (out) => (cr ? out + "\r" : out);

            const row = /^(\| Current version +\| *)([0-9]+\.[0-9]+\.[0-9]+[^\s|]*)( *\|)$/.exec(body);
            if (row) {
                // The gap is derived from the total width of the row rather
                // than from the gap the pattern matched. That pattern splits
                // the run of spaces between the value and the closing pipe
                // across two groups, and trusting the split loses a space.
                const head = row[1];
                const pad = Math.max(1, body.length - head.length - next.length - 1);
                table++;
                return done(head + next + " ".repeat(pad) + "|");
            }

            if (body.includes("guchho-")) {
                const before = body;
                const after = body.replace(/guchho-[0-9]+\.[0-9]+\.[0-9]+[^-`\s]*/g, "guchho-" + next);
                if (after !== before) example++;
                return done(after);
            }

            return done(body);
        });

        if (table === 0) {
            console.error("no \"| Current version |\" row found in CONTRIBUTING.md");
            process.exit(3);
        }

        fs.writeFileSync(file, lines.join("\n"));
        console.log(`  CONTRIBUTING.md: ${table} table row(s), ${example} naming example(s)`);
    ' "CONTRIBUTING.md" "$VERSION" ) || error "CONTRIBUTING.md update failed"
else
    warn "  CONTRIBUTING.md: not found, skipped"
fi


# ========================================
# Update the --version doc comment
# ========================================
#
# src/cli/cli_run.cpp documents the exact stdout of --version a few lines above
# the call that produces it. Left alone it is a comment claiming a release
# that never happened, and it is the comment most likely to be copied into a
# bug report.

CLI_RUN="$ROOT_DIR/src/cli/cli_run.cpp"

if [[ -f "$CLI_RUN" ]]; then
    OLD_CLI_RUN=$(grep -oP 'guchho v\K[0-9]+\.[0-9]+\.[0-9]+[^"]*' "$CLI_RUN" | head -1 || true)

    if [[ -n "$OLD_CLI_RUN" ]]; then
        sed -i "s/guchho v$(escape_re "$OLD_CLI_RUN")\"/guchho v${VERSION}\"/g" "$CLI_RUN"
        info "  src/cli/cli_run.cpp: ${OLD_CLI_RUN} -> ${VERSION}"
    else
        warn "  src/cli/cli_run.cpp: no \"guchho v<version>\" comment found, skipped"
    fi
fi


# ========================================
# Update the VS Code C/C++ define
# ========================================
#
# .vscode/c_cpp_properties.json mirrors the GUCHHO_VERSION_STRING that
# src/CMakeLists.txt:445 hands the compiler. cpptools never re-reads the build
# directory for defines, so a stale literal here means the squiggles in
# VS Code disagree with the binary the tests run.

CPP_PROPS="$ROOT_DIR/.vscode/c_cpp_properties.json"

if [[ -f "$CPP_PROPS" ]]; then
    OLD_CPP_DEFINE=$(grep -oP 'GUCHHO_VERSION_STRING=\\?"?\K[^"]+' "$CPP_PROPS" | head -1 || true)

    if [[ -n "$OLD_CPP_DEFINE" ]]; then
        sed -i "/GUCHHO_VERSION_STRING=/ s/$(escape_re "$OLD_CPP_DEFINE")/$(escape_re "$VERSION")/g" "$CPP_PROPS"
        info "  .vscode/c_cpp_properties.json: ${OLD_CPP_DEFINE} -> ${VERSION}"
    else
        warn "  .vscode/c_cpp_properties.json: no GUCHHO_VERSION_STRING define found, skipped"
    fi
else
    warn "  .vscode/c_cpp_properties.json: not found, skipped"
fi

fi # VERIFY_ONLY


# ========================================
# Verify, then report
# ========================================

verify_versions

echo ""
if [[ "$VERIFY_ONLY" -eq 1 ]]; then
    info "Verified: the tree is internally consistent."
else
    info "Done! Version bumped to ${VERSION} across all packages."
fi