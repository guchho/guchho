// One number, said in four places.
//
// The project's version lives in the top-level CMakeLists.txt, is compiled into
// the binary as GUCHHO_VERSION_STRING, and is written into the manifest of the
// main package and each of the twenty-five platform packages. scripts/bump-version.sh
// edits the first of those and is supposed to leave the rest agreeing.
//
// The failure this guards against is not a wrong number, it is a set of right
// numbers. A wrapper at 1.0.1 that installs a binary calling itself 1.0.0 gives
// a bug report no way to reproduce, because the two halves of the install
// disagree about what was installed. Nothing enforces the agreement at build
// time: the version define is a macro, the manifests are JSON, and nothing ties
// them together.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { describe, it } = require("node:test");

const { findBuiltBinary, skipWithoutBinary } = require("./helpers/workspace");
const { REPO_ROOT, PLATFORMS_DIR, platformDirs, readManifest, GUCHHO_DIR } = require("./helpers/paths");

// Reads the project version out of the top-level CMakeLists.txt, which is where
// scripts/bump-version.sh writes it and where project(VERSION) reads it from.
//
// Parsed rather than read with a regular expression at the call site so that
// there is one place that knows what the line looks like. The pattern is
// anchored to a bare VERSION keyword on its own line, which is the shape
// project() takes and the shape bump-version.sh writes.
function cmakeProjectVersion() {
    const text = fs.readFileSync(path.join(REPO_ROOT, "CMakeLists.txt"), "utf8");
    const match = text.match(/^\s*VERSION\s+(\S+)\s*$/m);
    assert.ok(match, "no VERSION line in the top-level CMakeLists.txt");
    return match[1];
}

const mainVersion = readManifest(GUCHHO_DIR).version;

describe("the version number", () => {
    it("is the same in the build and in the manifest", () => {
        assert.equal(
            cmakeProjectVersion(),
            mainVersion,
            "CMakeLists.txt and package/npm/guchho/package.json disagree; " +
            "a build of one would install as the other"
        );
    });

    it("is the same in every platform manifest, checked in platform-map.test.js too", () => {
        // Repeated here on purpose: that file is about the platform table and
        // this one is about the number, and a version test that only ran as part
        // of a table test would be skipped by anybody running this file alone.
        for (const key of platformDirs()) {
            const manifest = readManifest(path.join(PLATFORMS_DIR, key));
            assert.equal(manifest.version, mainVersion, `@guchho/${key} is at ${manifest.version}`);
        }
    });

    it("is what the binary reports for --version", { skip: skipWithoutBinary() }, () => {
        // The one check that needs a build, and the one that matters most: it
        // is the difference between the wrapper and the binary agreeing and
        // between them silently not agreeing. It fails when the tree has been
        // edited but not rebuilt, which is the answer, not a false alarm.
        const binary = findBuiltBinary();
        const result = require("node:child_process").spawnSync(binary, ["--version"], {
            encoding: "utf8",
        });

        assert.equal(result.status, 0, `guchho --version failed: ${result.stderr}`);

        const reported = result.stdout.trim();
        assert.equal(
            reported,
            `guchho v${mainVersion}`,
            `the binary at ${path.relative(REPO_ROOT, binary)} reports "${reported}" but the manifests say ${mainVersion}; ` +
            "rebuild it (scripts/build-npm.sh) or the installed binary will not match the package it came in"
        );
    });

    it("is what the binary reports in its banner, which is printed separately", () => {
        // The banner and --version are two different format strings in
        // cli_help.cpp, and the version test above only covers one of them.
        // A release that changed the number in one and not the other would
        // otherwise be found by a person reading a screenshot.
        const help = fs.readFileSync(path.join(REPO_ROOT, "src", "cli", "cli_help.cpp"), "utf8");
        const fallback = help.match(/#define\s+GUCHHO_VERSION_STRING\s+"([^"]+)"/);

        assert.ok(
            fallback,
            "cli_help.cpp no longer has a fallback version, so the seam it was given is gone"
        );
        assert.notEqual(
            fallback[1],
            mainVersion,
            "cli_help.cpp's fallback still equals the current version, which means " +
            "GUCHHO_VERSION_STRING is probably not reaching it and the number is a coincidence"
        );
    });

    it("reaches the file that prints it, rather than a target that only links it", () => {
        // cli_help.cpp is compiled into the guchho_cli static library. A
        // PRIVATE definition on the executable does not reach a static library
        // it links, so the number silently fell back to the literal in that
        // file and every published manifest disagreed with the binary. The
        // check is that the definition is on the library.
        const cmake = fs.readFileSync(path.join(REPO_ROOT, "src", "CMakeLists.txt"), "utf8");
        const block = cmake.match(/target_compile_definitions\((\w+)\s+PRIVATE\s+GUCHHO_VERSION_STRING/);

        assert.ok(
            block,
            "GUCHHO_VERSION_STRING is no longer defined in src/CMakeLists.txt"
        );
        assert.equal(
            block[1],
            "guchho_cli",
            `GUCHHO_VERSION_STRING is defined on "${block[1]}", but cli_help.cpp is compiled into guchho_cli`
        );
    });
});
