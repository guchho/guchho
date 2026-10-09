// guchho.zip(), reached the way a caller reaches it.
//
//     import { zip } from "guchho";
//     const result = await zip({ inputs: ["dist"], outFile: "release.zip" });
//
// The whole of the public contract: an options object in, a promise of a path
// and a size out, and a throw when either half did not happen. What this file
// checks is the three places that can go wrong — the options the caller built,
// the request the service refused, and the archive that was supposed to be on
// disk — because they are three different failures with three different
// callers to blame.
//
// The validation tests deliberately do not need a binary. Every one of them
// has to fail before a service is started, which is the difference between
// "your options are wrong" and "the engine said no": a TypeError for a shape
// the caller built is an answer in milliseconds, while the same mistake
// reported by the engine costs a process and says less. If a validation test
// ever needs GUCHHO_REQUIRE_BINARY to pass, it has stopped testing that.

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
    const root = makeTempDir("zip");
    const sources = { "dist/app.js": "let a = 1;\n", ...files };

    for (const [relative, contents] of Object.entries(sources)) {
        const file = path.join(root, relative);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, contents);
    }
    return root;
}

// The zip local-file-header signature. The one assertion that says "this is an
// archive" rather than "this is a file at the path the result named" — the
// latter is true even when the engine wrote nothing and the caller is reading
// a file from an earlier run.
function isZip(file) {
    const head = Buffer.alloc(4);
    const fd = fs.openSync(file, "r");
    try {
        fs.readSync(fd, head, 0, 4, 0);
    } finally {
        fs.closeSync(fd);
    }
    return head.equals(Buffer.from([0x50, 0x4b, 0x03, 0x04]));
}

describe("the module's shape", () => {
    it("has zip as a named binding", () => {
        assert.equal(typeof guchho.zip, "function");
    });
});

describe("zip() validates what it is given", () => {
    it("needs an options object", async () => {
        await assert.rejects(() => guchho.zip(), TypeError);
        await assert.rejects(() => guchho.zip(null), TypeError);
        await assert.rejects(() => guchho.zip("dist"), TypeError);
        await assert.rejects(() => guchho.zip(["dist"]), TypeError);
    });

    it("needs a non-empty list of paths", async () => {
        await assert.rejects(() => guchho.zip({ outFile: "a.zip" }), TypeError);
        await assert.rejects(() => guchho.zip({ outFile: "a.zip", inputs: [] }), TypeError);
        await assert.rejects(
            () => guchho.zip({ outFile: "a.zip", inputs: "dist" }),
            TypeError
        );
    });

    it("needs every entry to be a path", async () => {
        await assert.rejects(
            () => guchho.zip({ outFile: "a.zip", inputs: ["dist", 3] }),
            TypeError
        );
    });

    it("needs an outFile", async () => {
        await assert.rejects(() => guchho.zip({ inputs: ["dist"] }), TypeError);
        await assert.rejects(() => guchho.zip({ inputs: ["dist"], outFile: "" }), TypeError);
        await assert.rejects(() => guchho.zip({ inputs: ["dist"], outFile: 7 }), TypeError);
    });

    it("refuses a level it could not send", async () => {
        const base = { inputs: ["dist"], outFile: "a.zip" };
        await assert.rejects(() => guchho.zip({ ...base, level: 9.5 }), TypeError);
        await assert.rejects(() => guchho.zip({ ...base, level: "9" }), TypeError);
        await assert.rejects(() => guchho.zip({ ...base, level: -1 }), RangeError);
        await assert.rejects(() => guchho.zip({ ...base, level: 10 }), RangeError);
    });

    it("refuses an overwrite it could not send", async () => {
        await assert.rejects(
            () => guchho.zip({ inputs: ["dist"], outFile: "a.zip", overwrite: "yes" }),
            TypeError
        );
    });

    it("refuses a date that is not a date", async () => {
        const base = { inputs: ["dist"], outFile: "a.zip" };
        await assert.rejects(() => guchho.zip({ ...base, date: 1500000000 }), TypeError);
        await assert.rejects(() => guchho.zip({ ...base, date: "2017" }), TypeError);
        await assert.rejects(() => guchho.zip({ ...base, date: new Date("nope") }), RangeError);
    });

    it("refuses a mode outside the bits a permission mask holds", async () => {
        const base = { inputs: ["dist"], outFile: "a.zip" };
        // 0o755 with a fraction: the right value with the wrong type of number.
        await assert.rejects(() => guchho.zip({ ...base, mode: 493.5 }), TypeError);
        await assert.rejects(() => guchho.zip({ ...base, mode: "755" }), TypeError);
        await assert.rejects(() => guchho.zip({ ...base, mode: -1 }), RangeError);
        await assert.rejects(() => guchho.zip({ ...base, mode: 0o100000 }), RangeError);
    });

    it("fails before starting anything when the options are wrong", async () => {
        // A binary that is not there would turn a validation mistake into a
        // skip rather than a failure, and a validation mistake is exactly what
        // these tests are about. Reaching the TypeError at all proves the
        // service was never asked.
        await assert.rejects(() => guchho.zip({ inputs: [] }), (error) => {
            assert.ok(error instanceof TypeError, `expected a TypeError, got ${error}`);
            assert.ok(!(error instanceof guchho.BuildFailure));
            return true;
        });
    });
});

describe("zip()", () => {
    it("writes the archive and reports where and how big", needsBinary, async () => {
        const root = makeTree();
        const outFile = path.join(root, "release.zip");

        const result = await guchho.zip({
            inputs: [path.join(root, "dist")],
            outFile,
        });

        assert.equal(result.path, outFile);
        assert.ok(Number.isFinite(result.size), `size should be a number, got ${result.size}`);
        assert.ok(result.size > 0, "an archive of a file has bytes");
        assert.deepEqual(result.warnings, []);

        assert.ok(fs.existsSync(outFile), "the path it reported should be on disk");
        assert.equal(result.size, fs.statSync(outFile).size, "the size should be the file's");
        assert.ok(isZip(outFile), "the file should start with a zip header");
    });

    it("puts the tree under its own name", needsBinary, async () => {
        const root = makeTree({ "dist/assets/logo.svg": "<svg/>" });
        const outFile = path.join(root, "out.zip");

        await guchho.zip({ inputs: [path.join(root, "dist")], outFile });

        // Read from the end of the file, where the central directory is: the
        // names live there rather than in the local headers, and a reader that
        // only looked at the front would miss an entry whose name was written
        // by a different code path.
        const tail = fs.readFileSync(outFile).toString("latin1");
        assert.ok(tail.includes("dist/app.js"), "dist/app.js should be an entry");
        assert.ok(tail.includes("dist/assets/logo.svg"), "the nested file should be too");
    });

    it("refuses an existing archive, and replaces it when asked", needsBinary, async () => {
        const root = makeTree();
        const outFile = path.join(root, "release.zip");
        fs.writeFileSync(outFile, "not an archive");

        await assert.rejects(
            () => guchho.zip({ inputs: [path.join(root, "dist")], outFile }),
            (error) => {
                assert.ok(error instanceof guchho.BuildFailure);
                assert.match(error.message, /already exists/);
                return true;
            }
        );
        assert.equal(fs.readFileSync(outFile, "utf8"), "not an archive");

        const result = await guchho.zip({
            inputs: [path.join(root, "dist")],
            outFile,
            overwrite: true,
        });
        assert.equal(result.path, outFile);
        assert.ok(isZip(outFile), "overwrite should have replaced it with an archive");
    });

    it("throws the engine's own words when an input is not there", needsBinary, async () => {
        const root = makeTree();

        await assert.rejects(
            () => guchho.zip({
                inputs: [path.join(root, "absent")],
                outFile: path.join(root, "out.zip"),
            }),
            (error) => {
                assert.ok(error instanceof guchho.BuildFailure);
                assert.match(error.message, /Input does not exist/);
                return true;
            }
        );
        assert.ok(!fs.existsSync(path.join(root, "out.zip")));
    });

    it("carries the warnings back rather than dropping them", needsBinary, async () => {
        const root = makeTree();
        const outFile = path.join(root, "dist/out.zip");

        await guchho.zip({ inputs: [path.join(root, "dist")], outFile });

        // The archive is now standing inside the tree being read, which is the
        // case a caller has to be told about: without the warning the archive
        // would quietly contain itself on the next run.
        const second = await guchho.zip({
            inputs: [path.join(root, "dist")],
            outFile,
            overwrite: true,
        });

        assert.equal(second.warnings.length, 1, `warnings: ${second.warnings.join("; ")}`);
        assert.match(second.warnings[0], /archive being written/);
    });

    it("keeps serving after a failure", needsBinary, async () => {
        const root = makeTree();

        await assert.rejects(
            () => guchho.zip({
                inputs: [path.join(root, "absent")],
                outFile: path.join(root, "a.zip"),
            }),
            guchho.BuildFailure
        );

        // A service that gave up after refusing a request would leave every
        // later call waiting for an answer that is not coming.
        const ok = await guchho.zip({
            inputs: [path.join(root, "dist")],
            outFile: path.join(root, "b.zip"),
        });
        assert.ok(isZip(ok.path));
    });

    it("accepts a level and a mode and a date together", needsBinary, async () => {
        const root = makeTree();
        const outFile = path.join(root, "out.zip");

        const result = await guchho.zip({
            inputs: [path.join(root, "dist")],
            outFile,
            level: 0,
            mode: 0o755,
            date: new Date(1500000000 * 1000),
        });

        assert.equal(result.path, outFile);
        assert.ok(isZip(outFile));
    });

    it("does not overwrite the input when the output sits inside it", needsBinary, async () => {
        const root = makeTree();
        const source = path.join(root, "dist");
        const outFile = path.join(source, "bundle.zip");

        await guchho.zip({ inputs: [source], outFile });

        // The source file is still there afterwards — the archive skipped
        // itself rather than reading the half-written bytes it was creating.
        assert.ok(fs.existsSync(path.join(source, "app.js")));
        assert.ok(isZip(outFile));
    });
});
