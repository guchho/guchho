// The platform table and the platform packages, which have to agree.
//
// lib/platforms.js is a hard-coded list of twenty-five names, and
// package/npm/@guchho/ holds twenty-five directories. Nothing in the build ties
// the two together: a platform added to the list without a directory behind it
// makes install.js warn instead of installing, and a directory without an entry
// in the list is a package npm will install and nothing will ever use.
//
// The names are not free text either. npm decides whether to install a platform
// package from its "os" and "cpu" fields, so a key that disagrees with them
// means the package is skipped on the one machine it was built for.

const assert = require("node:assert/strict");
const path = require("node:path");
const { describe, it } = require("node:test");

const { PLATFORMS_DIR, platformDirs, readManifest } = require("./helpers/paths");

// Required as a file rather than imported through the package, so that the
// table can be read without a binary being resolvable. lib/main.js works out
// where the binary is the moment it is called, not when it is loaded, but
// going straight to the table keeps this test from depending on that.
const { PLATFORM_PACKAGES } = require("../guchho/lib/platforms");

const mainVersion = readManifest(require("./helpers/paths").GUCHHO_DIR).version;

describe("the platform table", () => {
    it("names a package for every platform directory, and no others", () => {
        const onDisk = platformDirs();
        const inTable = Object.keys(PLATFORM_PACKAGES).sort();

        assert.deepEqual(
            inTable,
            onDisk,
            "lib/platforms.js and package/npm/@guchho/ have drifted apart"
        );
    });

    it("maps every key to the package in the directory of the same name", () => {
        for (const key of platformDirs()) {
            assert.equal(
                PLATFORM_PACKAGES[key],
                `@guchho/${key}`,
                `the table maps "${key}" to "${PLATFORM_PACKAGES[key]}"`
            );
        }
    });

    it("declares the os and cpu npm will filter on", () => {
        // npm compares "os"/"cpu" against process.platform and process.arch, and
        // those are the two halves of the key — so the obvious rule is that they
        // are the key's spelling. That is right for every platform except one.
        //
        // Node reports process.platform as "ohos" on OpenHarmony, while the
        // platform is called openharmony everywhere else: the key, the
        // toolchain triplets, the preset names. A manifest that said
        // "openharmony" would never match a real machine and the package would
        // silently never install, which is why this is spelled out rather than
        // left to be derived. The point of the table is that the *key* is this
        // project's name for a platform and the *os* is Node's.
        const NODE_PLATFORM = { "openharmony": "ohos" };

        for (const key of platformDirs()) {
            const manifest = readManifest(path.join(PLATFORMS_DIR, key));
            assert.ok(manifest, `${key} has no package.json`);

            const [keyOs, cpu] = key.split("-");
            const os = NODE_PLATFORM[keyOs] || keyOs;

            assert.deepEqual(manifest.os, [os], `${key} declares os ${JSON.stringify(manifest.os)}, not [${os}]`);
            assert.deepEqual(manifest.cpu, [cpu], `${key} declares cpu ${JSON.stringify(manifest.cpu)}`);
        }
    });

    it("is the key this machine would look itself up by", () => {
        // The one that cannot be caught by reading the table: if Node reported
        // a platform or architecture the table has no entry for, install would
        // warn on a working machine.
        const here = `${process.platform}-${process.arch}`;
        assert.ok(
            PLATFORM_PACKAGES[here],
            `this machine is "${here}", which the table does not know`
        );
    });
});

describe("each platform package", () => {
    it("is named after the directory holding it", () => {
        for (const key of platformDirs()) {
            const manifest = readManifest(path.join(PLATFORMS_DIR, key));
            assert.equal(manifest.name, `@guchho/${key}`);
        }
    });

    it("carries the same version as the package that depends on it", () => {
        // The wrapper pins an exact version, so a platform package at a
        // different one is either never installed or installed alongside a
        // wrapper built for another release.
        for (const key of platformDirs()) {
            const manifest = readManifest(path.join(PLATFORMS_DIR, key));
            assert.equal(manifest.version, mainVersion, `@guchho/${key} is at ${manifest.version}`);
        }
    });

    it("publishes the binary directory and a README, and both exist", () => {
        for (const key of platformDirs()) {
            const dir = path.join(PLATFORMS_DIR, key);
            const manifest = readManifest(dir);

            assert.deepEqual(manifest.files, ["bin/", "README.md"], `@guchho/${key} files`);
            assert.ok(
                require("node:fs").existsSync(path.join(dir, "README.md")),
                `@guchho/${key} lists a README it does not have`
            );
        }
    });

    it("keeps the binary out of version control, which is what .gitignore does", () => {
        // The binaries are built by CI and copied in, so they are ignored. A
        // committed one would be a several-megabyte binary in the history of
        // every platform, and one that nobody re-tests.
        const ignored = require("node:fs")
            .readFileSync(path.join(require("./helpers/paths").REPO_ROOT, ".gitignore"), "utf8");

        assert.ok(
            ignored.includes("package/npm/@guchho/*/bin/"),
            ".gitignore no longer keeps the built binaries out of the repository"
        );
    });

    it("does not ask for preferUnplugged, which npm has never honoured", () => {
        // preferUnplugged is a Yarn field. npm ignores it silently, so it reads
        // as a safety net that is not there: the actual reason a platform
        // package is safe to unplug is that it holds one binary and no state.
        for (const key of platformDirs()) {
            const manifest = readManifest(path.join(PLATFORMS_DIR, key));
            assert.equal(
                manifest.preferUnplugged,
                undefined,
                `@guchho/${key} sets preferUnplugged, a Yarn field npm ignores`
            );
        }
    });
});
