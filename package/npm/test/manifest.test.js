// The main package manifest, read as the contract a person installing the
// package depends on.
//
// Everything here is a promise made to npm before a single file is unpacked:
// that "bin" names a file that will be there, that "main" and "exports" point
// at modules that exist, that "postinstall" names a script that is shipped, and
// that "files" names what is actually being published. A mistake in any of them
// is invisible until somebody installs the package, which is the worst place
// to find one out, and invisible to the C++ test suite entirely — none of it
// looks at a package.json.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { describe, it } = require("node:test");

const { GUCHHO_DIR, PLATFORMS_DIR, readManifest } = require("./helpers/paths");

const manifest = readManifest(GUCHHO_DIR);

describe("guchho/package.json", () => {
    it("is a manifest at all", () => {
        assert.ok(manifest, "package/npm/guchho/package.json could not be read");
        assert.equal(manifest.name, "guchho");
    });

    it("points bin at a file that is shipped", () => {
        // A "bin" entry naming a missing file produces a shim that fails on
        // first run, with an error that says nothing about the package.
        assert.deepEqual(Object.keys(manifest.bin), ["guchho"]);

        const target = path.join(GUCHHO_DIR, manifest.bin.guchho);
        assert.ok(fs.existsSync(target), `bin entry names a missing file: ${manifest.bin.guchho}`);
    });

    it("gives the bin shim a node shebang, so it can be run directly", () => {
        const target = path.join(GUCHHO_DIR, manifest.bin.guchho);
        const first = fs.readFileSync(target, "utf8").split("\n")[0];
        assert.equal(first, "#!/usr/bin/env node");
    });

    it("points main and both exports conditions at files that exist", () => {
        // The two conditions are the whole reason index.mjs is here: "require"
        // for CommonJS and "import" for an ES module, so that
        // "import * as guchho from 'guchho'" has named bindings to find.
        assert.ok(fs.existsSync(path.join(GUCHHO_DIR, manifest.main)), "main is missing");

        const root = manifest.exports["."];
        assert.ok(root, 'exports["."] is missing');
        for (const condition of ["import", "require"]) {
            const target = root[condition];
            assert.ok(target, `exports["."] has no "${condition}" condition`);
            assert.ok(
                fs.existsSync(path.join(GUCHHO_DIR, target)),
                `exports["."].${condition} names a missing file: ${target}`
            );
        }
    });

    it("keeps package.json reachable through exports", () => {
        // Tools read a dependency's manifest to find out what it needs, and an
        // exports map that hides it makes them fail in ways that look like a
        // broken tool rather than a missing entry.
        assert.equal(manifest.exports["./package.json"], "./package.json");
    });

    it("points postinstall at a shipped script", () => {
        const script = manifest.scripts.postinstall;
        assert.equal(script, "node install.js");
        assert.ok(
            fs.existsSync(path.join(GUCHHO_DIR, "install.js")),
            "postinstall names install.js but it is not shipped"
        );
    });

    it("lists only files that exist, and only directories that hold files", () => {
        // "files" is an allow-list, so a typo does not publish too little — it
        // publishes nothing for that entry, and the package fails at runtime.
        for (const entry of manifest.files) {
            const target = path.join(GUCHHO_DIR, entry);
            assert.ok(fs.existsSync(target), `files lists a missing entry: ${entry}`);

            if (fs.statSync(target).isDirectory()) {
                const contents = fs.readdirSync(target);
                assert.ok(
                    contents.length > 0,
                    `files lists the directory ${entry}, which is empty and would pack as nothing`
                );
            }
        }
    });

    it("ships every file the package needs to run", () => {
        // The inverse check: something on disk that the package needs but does
        // not list would be published by luck on npm and by nothing after a
        // future "files" edit.
        const shipped = new Set();
        for (const entry of manifest.files) {
            const target = path.join(GUCHHO_DIR, entry);
            if (fs.statSync(target).isDirectory()) {
                for (const name of fs.readdirSync(target)) {
                    shipped.add(path.posix.join(entry.replace(/\/$/, ""), name));
                }
            } else {
                shipped.add(entry);
            }
        }

        for (const needed of ["bin/guchho.js", "lib/index.js", "lib/index.mjs", "lib/main.js", "lib/transform.js", "lib/pack.js", "lib/build.js", "lib/exec.js", "lib/flags.js", "lib/platforms.js", "install.js", "README.md", "LICENSE"]) {
            assert.ok(shipped.has(needed), `${needed} is needed to run but is not in files`);
        }
    });

    it("asks for a Node that has the features the code uses", () => {
        // lib/index.mjs and the node:test suite both need 18 or later, and
        // top-level await in an ES module is why the floor is not lower.
        const floor = Number(manifest.engines.node.replace(/[^\d.]/g, ""));
        assert.ok(floor >= 18, `engines.node is ${manifest.engines.node}, which predates ESM support`);
    });

    it("does not claim a native module it does not ship", () => {
        // There is no .node file anywhere in the package: the API wraps the
        // binary rather than linking against it. A "gypfile" or a binding.gyp
        // would say otherwise, and would send npm looking for a build step that
        // is not there.
        const binding = path.join(GUCHHO_DIR, "binding.gyp");
        assert.ok(!fs.existsSync(binding), "a binding.gyp would make npm try to build this package");
    });

    it("lists one optional dependency per platform package, at one version", () => {
        // The version is pinned per platform rather than left to a range
        // because a range would let npm pair a 1.0.1 wrapper with a 1.1.0
        // binary, and the two have no way to check each other.
        const onDisk = fs.readdirSync(PLATFORMS_DIR, { withFileTypes: true })
            .filter((e) => e.isDirectory())
            .map((e) => `@guchho/${e.name}`)
            .sort();

        const declared = Object.keys(manifest.optionalDependencies).sort();
        assert.deepEqual(declared, onDisk, "optionalDependencies and the platform packages disagree");

        for (const [name, version] of Object.entries(manifest.optionalDependencies)) {
            assert.equal(version, manifest.version, `${name} is pinned to ${version}, not ${manifest.version}`);
        }
    });

    it("repeats no keyword", () => {
        // Not a functional problem, and not worth a release on its own, but it
        // is the kind of thing that survives three years of copying a manifest
        // and then shows up in a search listing twice.
        const keywords = manifest.keywords;
        assert.equal(
            new Set(keywords).size,
            keywords.length,
            `keywords repeats: ${keywords.join(", ")}`
        );
    });
});
