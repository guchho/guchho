// guchho.build(), reached the way a caller reaches it.
//
//     import { build } from "guchho";
//     const result = await build({ entries: ["src/main.js"] });
//
// Where transform() answers with a string, build() answers with files on disk,
// so most of what is checked here is that the files it says it wrote are
// actually there. A build that reported outputs which were not written would
// be worse than one that reported none.
//
// The imports are from the package's own lib rather than from "guchho", for the
// reason given at the top of api-transform.test.mjs; e2e.test.js is where the
// bare name is resolved.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");

const { cleanup, skipWithoutBinary } = require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };

// A throwaway project with one entry point. Not a fixture on disk: a directory
// the tests write to is the only kind of input whose state is known, and a
// shared one would let a test that failed to clean up decide the next one's
// result.
function makeProject(files = {}) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "guchho-build-test-"));
    const sources = { "src/main.js": "const answer = 42;\nconsole.log(answer);\n", ...files };

    for (const [relative, contents] of Object.entries(sources)) {
        const file = path.join(root, relative);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, contents);
    }

    return root;
}

describe("the module's shape", () => {
    it("has build as a named binding", () => {
        assert.equal(typeof guchho.build, "function");
    });
});

describe("build()", () => {
    it("reports the files it wrote, and they are there", needsBinary, async () => {
        const root = makeProject();

        const result = await guchho.build({ root, entries: ["src/main.js"] });

        assert.equal(result.exitCode, 0, `build failed: ${result.errors.join("\n")}`);
        assert.ok(result.outputs && typeof result.outputs === "object", "outputs should be an object");
        assert.ok(Object.keys(result.outputs).length > 0, "a successful build should report outputs");

        // The receipt and the disk are checked separately on purpose. The
        // metafile is written by the engine and read by the wrapper, and the
        // files are written by the engine and read by the filesystem; agreeing
        // with the metafile alone would only prove the engine can read its own
        // mind back.
        for (const file of result.files) {
            assert.ok(fs.existsSync(file), `${file} is reported but not on disk`);
            assert.ok(fs.statSync(file).size > 0, `${file} is empty`);
        }
    });

    it("writes into the outdir it was given, and says where that is", needsBinary, async () => {
        const root = makeProject();

        const result = await guchho.build({ root, entries: ["src/main.js"], outdir: "public" });

        assert.equal(result.exitCode, 0, `build failed: ${result.errors.join("\n")}`);
        assert.equal(result.outdir, path.resolve(root, "public"), "outdir should be absolute");

        for (const file of result.files) {
            assert.ok(
                file.startsWith(result.outdir + path.sep),
                `${file} is outside the outdir that was asked for`
            );
        }
    });

    it("defaults outdir to dist", needsBinary, async () => {
        const root = makeProject();

        const result = await guchho.build({ root, entries: ["src/main.js"] });

        assert.equal(result.outdir, path.resolve(root, "dist"));
    });

    it("leaves no metafile behind in the project", needsBinary, async () => {
        // build() asks for a metafile so it has something structured to return,
        // and that file is a receipt for one run rather than a build output.
        // Leaving it in the project would put a file there that no build
        // produced and that the next build would not clean up.
        const root = makeProject();

        const result = await guchho.build({ root, entries: ["src/main.js"] });
        assert.equal(result.exitCode, 0, `build failed: ${result.errors.join("\n")}`);

        const strays = fs.readdirSync(root).filter((name) => name.includes("metafile"));
        assert.deepEqual(strays, [], `build left ${strays.join(", ")} in the project`);
    });

    it("reports a missing entry point as an error and writes nothing", needsBinary, async () => {
        // The other half of "a failed build is a result": a build that cannot
        // find what it was asked for has to say so rather than succeed with an
        // empty output set, because a caller checking only exitCode would
        // deploy the previous directory and not notice.
        const root = makeProject();

        const result = await guchho.build({ root, entries: ["src/nothing-here.js"] });

        assert.notEqual(result.exitCode, 0, "a missing entry point should not report success");
        assert.ok(result.errors.length > 0, "there should be something to say about it");
        assert.deepEqual(result.files, [], "nothing should have been written");
    });

    it("reports a syntax error in an entry point", needsBinary, async () => {
        const root = makeProject({ "src/broken.js": "const a = (;\n" });

        const result = await guchho.build({ root, entries: ["src/broken.js"] });

        assert.notEqual(result.exitCode, 0);
        assert.ok(result.errors.length > 0);
    });

    it("resolves a relative entry against root, not against the caller's cwd", needsBinary, async () => {
        // The two are the same directory in a test process that never chdirs, so
        // this is the only way to tell them apart: the build is asked for
        // "src/main.js" while the process is somewhere else entirely.
        const root = makeProject();
        const before = process.cwd();

        try {
            process.chdir(os.tmpdir());
            const result = await guchho.build({ root, entries: ["src/main.js"] });

            assert.equal(result.exitCode, 0, `build failed: ${result.errors.join("\n")}`);
            for (const file of result.files) {
                assert.ok(file.startsWith(root), `${file} was written outside the project root`);
            }
        } finally {
            process.chdir(before);
        }
    });

    it("rejects a root that is not a directory", async () => {
        // Not a result: there is no run to report on, and the mistake is in the
        // caller's code, so it is worth stopping for.
        await assert.rejects(
            () => guchho.build({ root: "" }),
            TypeError
        );

        await assert.rejects(
            () => guchho.build({ root: 42 }),
            TypeError
        );
    });

    it("keeps the shape of the answer the same whether it worked or not", needsBinary, async () => {
        // Same reasoning as transform(): a caller writes result.files before it
        // knows whether there are any.
        const root = makeProject();
        const good = await guchho.build({ root, entries: ["src/main.js"] });
        const bad = await guchho.build({ root, entries: ["src/nothing-here.js"] });

        for (const result of [good, bad]) {
            assert.deepEqual(
                Object.keys(result).sort(),
                ["errors", "exitCode", "files", "inputs", "outdir", "outputs", "warnings"]
            );
        }
    });

    it("does not put the engine's own banner in the results", needsBinary, async () => {
        // The banner goes to the child's stdout, which for a build is not
        // parsed but is still noise in anything that logs it. What matters is
        // that it cannot be mistaken for a filename: every reported path is one
        // that exists.
        const root = makeProject();

        const result = await guchho.build({ root, entries: ["src/main.js"] });

        for (const file of result.files) {
            assert.ok(path.isAbsolute(file), `${file} is not an absolute path`);
            assert.ok(!file.includes("Guchho"), `${file} looks like it came from the banner`);
        }
    });
});
