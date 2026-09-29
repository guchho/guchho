// guchho.build(), reached the way a caller reaches it.
//
//     import { build } from "guchho";
//     const result = await build({ entryPoints: ["src/main.js"] });
//
// This file was rewritten when the API moved to the service and to esbuild's
// shape. What changed and why it was worth changing:
//
//   - "entries" is now "entryPoints", and "root" is now "absWorkingDir". Both
//     are esbuild's names, and a project moving between bundlers should be a
//     change of import rather than a rewrite of every call.
//
//   - a build that fails now throws. It used to resolve with a non-zero
//     "exitCode" and an empty "files", which meant a caller who checked the
//     result instead of catching would deploy whatever the previous build left
//     behind. Throwing is the shape esbuild publishes and the shape every tool
//     written against it already handles.
//
//   - the answer is errors, warnings, and outputFiles. Not "outputs", "inputs",
//     "files" and "exitCode" — the metafile is now only there when it was asked
//     for, and the outputs come back in memory when write is false.
//
// What is checked here is mostly that what the result claims is true: the files
// it reports were written, the ones it collected are the ones on disk, and a
// build that failed did not also write something.
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

        const result = await guchho.build({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
        });

        assert.deepEqual(result.errors, [], `build failed: ${result.errors.join("\n")}`);
        assert.ok(fs.existsSync(path.join(root, "dist")), "the default outdir should be dist");

        const written = fs
            .readdirSync(path.join(root, "dist"))
            .map((name) => path.join(root, "dist", name));

        assert.ok(written.length > 0, "a successful build should write something");
        for (const file of written) {
            assert.ok(fs.statSync(file).size > 0, `${file} is empty`);
        }
    });

    it("writes into the outdir it was given", needsBinary, async () => {
        const root = makeProject();

        await guchho.build({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            outdir: "public",
        });

        assert.ok(fs.existsSync(path.join(root, "public")), "public should be the outdir");
    });

    it("collects the outputs in memory when write is false", needsBinary, async () => {
        // The half of the contract that lets a build be used without being run:
        // the outputs come back as bytes, and nothing reaches the disk.
        const root = makeProject();

        const result = await guchho.build({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            write: false,
            bundle: true,
            format: "esm",
        });

        assert.deepEqual(result.errors, []);
        assert.ok(Array.isArray(result.outputFiles), "outputFiles should be there");
        assert.equal(result.outputFiles.length, 1, "one entry point, one output");

        const file = result.outputFiles[0];
        assert.ok(file.contents instanceof Uint8Array, "contents should be bytes");
        assert.ok(file.contents.length > 0, "contents should not be empty");
        assert.equal(typeof file.hash, "string");
        assert.ok(path.isAbsolute(file.path), `path should be absolute, got ${file.path}`);
        assert.ok(file.text.includes("42"), `text should decode the contents, got ${file.text}`);

        // The point of write: false. A build that wrote anyway would make this
        // option a lie that only shows up as a stray file much later.
        assert.ok(!fs.existsSync(path.join(root, "dist")), "write:false must not touch the disk");
    });

    it("leaves no metafile behind in the project", needsBinary, async () => {
        // The metafile is a receipt for one run rather than a build output, so it
        // is asked for and not written to the project. Under the service it is
        // returned in the answer and never touches the disk at all, which is why
        // this is now a statement about the whole mechanism rather than about a
        // temporary file.
        const root = makeProject();

        await guchho.build({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            metafile: true,
        });

        const strays = fs.readdirSync(root).filter((name) => name.includes("metafile"));
        assert.deepEqual(strays, [], `build left ${strays.join(", ")} in the project`);
    });

    it("returns a metafile when asked, and none when not", needsBinary, async () => {
        const root = makeProject();

        const withMetafile = await guchho.build({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            write: false,
            metafile: true,
        });
        assert.ok(withMetafile.metafile && typeof withMetafile.metafile === "object");
        assert.ok(withMetafile.metafile.outputs !== undefined, "a metafile should have outputs");

        const without = await guchho.build({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            write: false,
        });
        // Absent rather than an empty object: "you did not ask" and "you asked
        // and there was nothing" are different answers.
        assert.equal(without.metafile, undefined, "metafile should be absent when not asked for");
    });

    it("throws on a missing entry point, carrying the diagnostics", needsBinary, async () => {
        // The other half of "a failed build is a throw": the error has to say
        // what went wrong, or a caller can only report that their build failed.
        const root = makeProject();

        let thrown = null;
        try {
            await guchho.build({ absWorkingDir: root, entryPoints: ["src/nothing-here.js"] });
        } catch (error) {
            thrown = error;
        }

        assert.ok(thrown, "a missing entry point should throw");
        assert.ok(Array.isArray(thrown.errors) && thrown.errors.length > 0, "the error should carry errors");
        assert.equal(typeof thrown.errors[0].text, "string", "each error should say something");
        assert.ok(Array.isArray(thrown.warnings), "the error should carry warnings too");
    });

    it("reports a syntax error in an entry point", needsBinary, async () => {
        const root = makeProject({ "src/broken.js": "const a = (;\n" });

        await assert.rejects(
            () => guchho.build({ absWorkingDir: root, entryPoints: ["src/broken.js"] }),
            (error) => {
                assert.ok(error.errors.length > 0, "there should be something to say about it");
                return true;
            }
        );
    });

    it("resolves a relative entry against absWorkingDir, not the caller's cwd", needsBinary, async () => {
        // The two are the same directory in a test process that never chdirs, so
        // this is the only way to tell them apart: the build is asked for
        // "src/main.js" while the process is somewhere else entirely.
        const root = makeProject();
        const before = process.cwd();

        try {
            process.chdir(os.tmpdir());
            const result = await guchho.build({
                absWorkingDir: root,
                entryPoints: ["src/main.js"],
                write: false,
            });

            assert.deepEqual(result.errors, [], `build failed: ${result.errors.join("\n")}`);
            assert.ok(
                result.outputFiles[0].path.startsWith(root),
                `${result.outputFiles[0].path} was written outside the project root`
            );
        } finally {
            process.chdir(before);
        }
    });

    it("refuses to run with no entry points at all", async () => {
        // Not a failed build: a build that would fall back to a glob, find
        // nothing, and succeed having built nothing. A caller reading that as a
        // successful build is exactly the bug worth stopping for.
        await assert.rejects(() => guchho.build({}), TypeError);
        await assert.rejects(() => guchho.build({ entryPoints: [] }), TypeError);
    });

    it("rejects options that are not an object", async () => {
        await assert.rejects(() => guchho.build(null), TypeError);
        await assert.rejects(() => guchho.build("src/main.js"), TypeError);
    });

    it("refuses an option the grammar has no spelling for", async () => {
        // The refusal names the option and arrives before anything is built,
        // rather than reaching the engine and coming back as a build failure
        // that looks like a problem with the code.
        await assert.rejects(
            () => guchho.build({ entryPoints: ["src/main.js"], logLevel: "chatty" }),
            TypeError
        );
    });

    it("keeps the shape of a successful answer predictable", needsBinary, async () => {
        // A caller writes result.errors before it knows whether there are any.
        // The optional halves are the ones a caller has to ask about first, and
        // they are asked about by being absent.
        const root = makeProject();

        const result = await guchho.build({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            write: false,
        });

        assert.deepEqual(Object.keys(result).sort(), ["errors", "outputFiles", "warnings"]);
        for (const message of [...result.errors, ...result.warnings]) {
            assert.equal(typeof message.id, "string");
            assert.equal(typeof message.pluginName, "string");
            assert.equal(typeof message.text, "string");
            assert.ok(Array.isArray(message.notes), "notes should always be an array");
            assert.ok("location" in message, "location should always be present, even as null");
        }
    });

    it("does not put the engine's own banner in the results", needsBinary, async () => {
        // The banner is a banner, not a filename, and a build that reported it as
        // an output would be a caller deleting the wrong thing.
        const root = makeProject();

        const result = await guchho.build({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            write: false,
        });

        for (const file of result.outputFiles) {
            assert.ok(path.isAbsolute(file.path), `${file.path} is not an absolute path`);
            assert.ok(!file.path.includes("Guchho"), `${file.path} looks like it came from the banner`);
        }
    });
});
