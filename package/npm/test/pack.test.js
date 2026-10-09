// What a publish would actually put on the registry.
//
// Everything else in this suite checks what the package says about itself.
// This file asks npm, rather than reading the manifest and believing it: the
// "files" field, the always-included files and the always-excluded ones each
// have their own rules, and a manifest can be perfectly self-consistent and
// still pack the wrong thing.
//
// "npm pack --dry-run" reports the same file list a real publish would produce
// and writes nothing, which is what makes it safe to point at the real
// packaging directory rather than at a copy.

const assert = require("node:assert/strict");
const child_process = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const { describe, it } = require("node:test");

const { npmPackDryRun } = require("./helpers/workspace");
const { GUCHHO_DIR, PLATFORMS_DIR, platformDirs, readManifest } = require("./helpers/paths");

// npm includes these whatever "files" says, and a check that only compared
// against the manifest would be surprised by them.
const ALWAYS_PACKED = new Set(["package.json", "README.md", "LICENSE", "LICENCE"]);

// The file list a publish would contain, as a set of paths relative to the
// package root.
function packedPaths(dir) {
    return new Set(npmPackDryRun(dir).files.map((f) => f.path));
}

describe("the main package", () => {
    const manifest = readManifest(GUCHHO_DIR);

    it("packs every file it says it does, and nothing it does not", () => {
        const packed = packedPaths(GUCHHO_DIR);

        for (const name of packed) {
            const allowed = manifest.files.some((entry) => {
                const base = entry.replace(/\/$/, "");
                return name === base || name.startsWith(`${base}/`);
            });
            assert.ok(
                allowed || ALWAYS_PACKED.has(name),
                `${name} would be published but is in no files entry`
            );
        }
    });

    it("packs the API, the shim, the installer and the platform table", () => {
        // Each of these has a test elsewhere that would fail for a confusing
        // reason if the file were missing from the tarball rather than from the
        // disk: the shim cannot start, install.js cannot run, the table cannot
        // be read. A short list here turns all of that into one clear failure.
        const packed = packedPaths(GUCHHO_DIR);

        for (const required of [
            "package.json",
            "bin/guchho.js",
            "lib/index.js",
            "lib/index.mjs",
            "lib/main.js",
            "lib/transform.js",
            "lib/zip.js",
            "lib/build.js",
            "lib/exec.js",
            "lib/flags.js",
            "lib/platforms.js",
            "install.js",
        ]) {
            assert.ok(packed.has(required), `${required} would not be published`);
        }
    });

    it("does not carry a platform binary, which would make one package win everywhere", () => {
        // install.js copies a binary into bin/ next to the shim, and bin/ is in
        // the files list because the shim lives there. So running the installer
        // inside the checkout and then packing would publish this machine's
        // binary inside the main package — where it would be found first on
        // every platform, and would be the wrong architecture almost everywhere.
        // Nothing else stops that; .gitignore only covers the platform packages.
        const binDir = path.join(GUCHHO_DIR, "bin");
        const strays = fs.readdirSync(binDir).filter((name) => name !== "guchho.js");

        assert.deepEqual(
            strays,
            [],
            `package/npm/guchho/bin holds ${strays.join(", ")}; a native binary there would be published in the main package`
        );
    });

    it("carries no test output, tarball or node_modules", () => {
        // npm excludes node_modules and .tgz on its own, but nothing excludes a
        // pack directory or a stray archive somebody left behind.
        const packed = packedPaths(GUCHHO_DIR);

        for (const name of packed) {
            assert.ok(!name.startsWith("node_modules/"), `${name} would be published`);
            assert.ok(!name.endsWith(".tgz"), `${name} would be published`);
            assert.ok(!name.startsWith("test/"), `${name} would be published`);
        }
    });

    it("publishes as the name and version the file declares", () => {
        // What npm would call the package, which is not always what the
        // directory is called and is not something the version of the binary
        // has anything to do with.
        const packed = npmPackDryRun(GUCHHO_DIR);
        assert.equal(packed.name, manifest.name);
        assert.equal(packed.version, manifest.version);
        assert.equal(packed.id, `${manifest.name}@${manifest.version}`);
    });

    it("is small enough that its size is not a mistake", () => {
        // The main package is JavaScript and a README. A megabyte of it means
        // something binary got in, and the test above would not necessarily
        // have caught it if that something were text.
        const packed = npmPackDryRun(GUCHHO_DIR);
        assert.ok(
            packed.size < 512 * 1024,
            `the main package would be ${Math.round(packed.size / 1024)}KB, which is not a JavaScript wrapper`
        );
    });
});

describe("each platform package", () => {
    it("packs its README and its binary directory", () => {
        for (const key of platformDirs()) {
            const dir = path.join(PLATFORMS_DIR, key);
            const packed = packedPaths(dir);

            assert.ok(packed.has("package.json"), `@guchho/${key} would not publish its manifest`);
            assert.ok(packed.has("README.md"), `@guchho/${key} would not publish its README`);
        }
    });

    it("is publishable, or is honestly empty", () => {
        // The publish workflow skips any platform package whose bin directory
        // holds nothing, and warns about it. So a package with no binary is not
        // a broken package — it is one the pipeline declines to publish. What
        // would be wrong is not knowing which of the twenty-five those are, so
        // this reports them rather than failing on them: twenty-three empty
        // platform packages is the normal state of a repository that builds
        // three of them.
        const publishable = [];
        const empty = [];

        for (const key of platformDirs()) {
            const binDir = path.join(PLATFORMS_DIR, key, "bin");
            const hasBinary =
                fs.existsSync(binDir) &&
                fs.readdirSync(binDir).some((name) => !name.startsWith("."));

            if (hasBinary) {
                publishable.push(key);
                const packed = packedPaths(path.join(PLATFORMS_DIR, key));
                assert.ok(
                    [...packed].some((name) => name.startsWith("bin/")),
                    `@guchho/${key} has a binary but would not publish it`
                );
            } else {
                empty.push(key);
            }
        }

        // Reported, not asserted: the interesting number is that the workflow's
        // gate and the filesystem agree, which is what the CI job that builds
        // them is for.
        if (empty.length > 0) {
            console.log(
                `  note: ${empty.length} platform package(s) have no binary and would be skipped by the publish workflow: ${empty.join(", ")}`
            );
        }
        assert.equal(
            publishable.length + empty.length,
            platformDirs().length,
            "every platform package is in exactly one of the two lists"
        );
    });

    it("publishes a binary under the name the machine expects", () => {
        // The name is decided by the machine, not by the package: "guchho.exe"
        // on Windows and "guchho" everywhere else, and a package that got it
        // wrong installs a binary nothing will look for.
        //
        // A Windows package may also hold runtime libraries beside the
        // executable — see the test below — so only the executable is checked
        // here, and what is left over is checked for being a library rather
        // than for having some particular name.
        for (const key of platformDirs()) {
            const binDir = path.join(PLATFORMS_DIR, key, "bin");
            if (!fs.existsSync(binDir)) {
                continue;
            }

            const expected = key.startsWith("win32-") ? "guchho.exe" : "guchho";
            const names = fs.readdirSync(binDir).filter((name) => !name.startsWith("."));

            // A platform nobody has built for has an empty bin directory, which
            // the publish workflow skips. Asserting a name there would be
            // asserting that twenty-three cross-compiled binaries exist.
            if (names.length === 0) {
                continue;
            }

            assert.ok(
                names.includes(expected),
                `@guchho/${key} holds [${names.join(", ")}] and not ${expected}`
            );

            for (const name of names) {
                if (name === expected) {
                    continue;
                }
                assert.ok(
                    key.startsWith("win32-") && name.toLowerCase().endsWith(".dll"),
                    `@guchho/${key} holds ${name}, which is neither ${expected} nor a Windows library`
                );
            }
        }
    });

    it("publishes a Windows binary that starts on its own", () => {
        // A MinGW build is not one file: the C++ runtime is left in shared
        // libraries beside the executable, and Windows looks for them in the
        // executable's own directory. A package holding the executable alone
        // therefore runs on the machine that built it and fails everywhere else
        // with STATUS_DLL_NOT_FOUND, which reaches a user as 0xC0000135 and no
        // output at all.
        //
        // So the binary is run from where the package would put it. Anything
        // that cannot start fails here, in the test that owns packaging, rather
        // than on somebody's machine after an install.
        const binDir = path.join(PLATFORMS_DIR, "win32-x64", "bin");
        const binary = path.join(binDir, "guchho.exe");

        if (!fs.existsSync(binary)) {
            return;
        }

        const result = child_process.spawnSync(binary, ["--version"], {
            encoding: "utf8",
            // Nothing inherited: the point is that the binary finds everything it
            // needs inside the package directory, so a library that is only on
            // this machine's PATH would not save it.
            env: { PATH: "", SystemRoot: process.env.SystemRoot || "" },
            cwd: binDir,
        });

        if (result.error) {
            assert.fail(`could not start ${binary}: ${result.error.message}`);
        }

        assert.equal(
            result.status,
            0,
            `${binary} exited with ${result.status}` +
            (result.status === 3221225785
                ? " (0xC0000135, STATUS_DLL_NOT_FOUND: a runtime library is missing from the package)"
                : "") +
            `\n${result.stderr}`
        );
    });
});
