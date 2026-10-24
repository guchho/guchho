// guchho.unpack(), reached the way a caller reaches it.
//
//     import { unpack } from "guchho";
//     const result = await unpack({ archive: "release.zip" });
//
// The mirror of api-pack.test.js: an options object in, a promise of what was
// written out, and a throw when the archive could not be read. What is checked
// is the three places that can go wrong — the options the caller built, the
// request the service refused, and the files that were supposed to be on disk —
// because they are three different failures with three different callers to
// blame.
//
// The validation tests deliberately do not need a binary. Every one of them has
// to fail before a service is started, which is the difference between "your
// options are wrong" and "the engine said no".

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");

const { cleanup, makeTempDir, skipWithoutBinary } = require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };

// A temporary directory with one file in it, registered for cleanup.
function makeTree(files = {}) {
    const root = makeTempDir("unpack");
    const sources = { "dist/app.js": "let a = 1;\n", ...files };

    for (const [relative, contents] of Object.entries(sources)) {
        const file = path.join(root, relative);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, contents);
    }
    return root;
}

// Packs a tree with the public API, so the archive under test was written by
// the other half of the same engine rather than by a fixture nobody can repair.
async function packTree(root, format = "zip") {
    const outFile = path.join(root, `release.${format}`);
    await guchho.pack({ inputs: [path.join(root, "dist")], outFile, format });
    return outFile;
}

describe("the module's shape", () => {
    it("has unpack as a named binding", () => {
        assert.equal(typeof guchho.unpack, "function");
    });
});

describe("unpack() validates what it is given", () => {
    it("needs an options object", async () => {
        await assert.rejects(() => guchho.unpack(), TypeError);
        await assert.rejects(() => guchho.unpack(null), TypeError);
        await assert.rejects(() => guchho.unpack("release.zip"), TypeError);
        await assert.rejects(() => guchho.unpack(["release.zip"]), TypeError);
    });

    it("needs an archive", async () => {
        await assert.rejects(() => guchho.unpack({}), TypeError);
        await assert.rejects(() => guchho.unpack({ archive: "" }), TypeError);
        await assert.rejects(() => guchho.unpack({ archive: 7 }), TypeError);
    });

    it("needs outDir to be a non-empty string when it is given", async () => {
        await assert.rejects(() => guchho.unpack({ archive: "a.zip", outDir: 7 }), TypeError);
        await assert.rejects(() => guchho.unpack({ archive: "a.zip", outDir: "" }), TypeError);
    });

    it("refuses a strip count it could not send", async () => {
        const base = { archive: "a.zip" };
        await assert.rejects(() => guchho.unpack({ ...base, stripComponents: 1.5 }), TypeError);
        await assert.rejects(() => guchho.unpack({ ...base, stripComponents: "1" }), TypeError);
        await assert.rejects(() => guchho.unpack({ ...base, stripComponents: -1 }), RangeError);
    });

    it("refuses an overwrite it could not send", async () => {
        await assert.rejects(
            () => guchho.unpack({ archive: "a.zip", overwrite: "yes" }),
            TypeError
        );
    });

    it("refuses preserve flags it could not send", async () => {
        await assert.rejects(
            () => guchho.unpack({ archive: "a.zip", preservePermissions: "yes" }),
            TypeError
        );
        await assert.rejects(
            () => guchho.unpack({ archive: "a.zip", preserveTimestamps: 1 }),
            TypeError
        );
    });

    it("refuses a format it could not read", async () => {
        const base = { archive: "a.zip" };
        await assert.rejects(() => guchho.unpack({ ...base, format: 9 }), TypeError);
        await assert.rejects(() => guchho.unpack({ ...base, format: "" }), RangeError);
        await assert.rejects(() => guchho.unpack({ ...base, format: "7z" }), RangeError);

        // Exact spelling, as the engine compares it: "ZIP" is a different
        // answer rather than a near miss.
        await assert.rejects(() => guchho.unpack({ ...base, format: "ZIP" }), RangeError);
    });

    it("returns a Promise, even when it is about to reject", async () => {
        const pending = guchho.unpack({});
        assert.ok(pending instanceof Promise, "unpack() should return a Promise");
        await assert.rejects(() => pending, TypeError);
    });

    it("fails before starting anything when the options are wrong", async () => {
        await assert.rejects(() => guchho.unpack({}), (error) => {
            assert.ok(error instanceof TypeError, `expected a TypeError, got ${error}`);
            assert.ok(!(error instanceof guchho.BuildFailure));
            return true;
        });
    });
});

describe("unpack()", () => {
    it("extracts an archive and reports what it wrote", needsBinary, async () => {
        const root = makeTree();
        const archive = await packTree(root);
        const outDir = path.join(root, "out");

        const result = await guchho.unpack({ archive, outDir });

        assert.equal(result.outDir, outDir);
        // Archive paths use forward slashes on every host, because that is how
        // the entry names are written inside the archive.
        assert.ok(
            result.files.includes("dist/app.js"),
            `dist/app.js should be listed, got ${JSON.stringify(result.files)}`
        );
        assert.deepEqual(result.warnings, []);

        const extracted = path.join(outDir, "dist", "app.js");
        assert.ok(fs.existsSync(extracted), "the file it listed should be on disk");
        assert.equal(fs.readFileSync(extracted, "utf8"), "let a = 1;\n");
    });

    it("uses the archive name when outDir is left out", needsBinary, async () => {
        const root = makeTree();
        const archive = await packTree(root);

        const result = await guchho.unpack({ archive });

        const expected = path.join(root, "release");
        assert.equal(result.outDir, expected);
        assert.ok(fs.existsSync(path.join(expected, "dist", "app.js")));
    });

    it("writes each format it says it supports", needsBinary, async () => {
        for (const format of ["zip", "tar", "tar.gz"]) {
            const root = makeTree();
            const archive = await packTree(root, format);
            const outDir = path.join(root, "out");

            const result = await guchho.unpack({ archive, outDir, format });

            assert.equal(result.outDir, outDir);
            assert.deepEqual(result.warnings, []);
            assert.ok(
                fs.existsSync(path.join(outDir, "dist", "app.js")),
                `${format} should extract dist/app.js`
            );
        }
    });

    it("skips an existing file unless overwrite is asked for", needsBinary, async () => {
        const root = makeTree();
        const archive = await packTree(root);
        const outDir = path.join(root, "out");
        const extracted = path.join(outDir, "dist", "app.js");

        await guchho.unpack({ archive, outDir });
        fs.writeFileSync(extracted, "changed\n");

        const skipped = await guchho.unpack({ archive, outDir });
        assert.equal(fs.readFileSync(extracted, "utf8"), "changed\n");
        assert.ok(
            skipped.warnings.length > 0,
            "a skipped existing file should be reported as a warning"
        );

        const replaced = await guchho.unpack({ archive, outDir, overwrite: true });
        assert.deepEqual(replaced.warnings, []);
        assert.equal(fs.readFileSync(extracted, "utf8"), "let a = 1;\n");
    });

    it("strips leading components when asked", needsBinary, async () => {
        const root = makeTree();
        const archive = await packTree(root);
        const outDir = path.join(root, "out");

        const result = await guchho.unpack({ archive, outDir, stripComponents: 1 });

        assert.ok(result.files.includes("app.js"), JSON.stringify(result.files));
        assert.ok(fs.existsSync(path.join(outDir, "app.js")));
        assert.ok(!fs.existsSync(path.join(outDir, "dist")));
    });

    it("throws a BuildFailure for an archive that is not there", needsBinary, async () => {
        const root = makeTempDir("unpack-missing");

        await assert.rejects(
            () => guchho.unpack({ archive: path.join(root, "absent.zip") }),
            (error) => {
                assert.ok(error instanceof guchho.BuildFailure, `expected a BuildFailure, got ${error}`);
                assert.ok(error.errors.length > 0, "a failure should carry its diagnostics");
                return true;
            }
        );
    });
});
