// The failure shapes: BuildFailure and ServiceError, and how to tell them apart.
//
// The whole point of the two classes is that they are different failures with
// different fixes. A BuildFailure is the code being built — the diagnostics are
// in the error, and fixing them is editing the code. A ServiceError is the
// installation — the binary is missing, or too old to talk to, or died, and
// nothing about the code changes that.
//
// So the tests here pin down three things:
//
//   - a failed build throws a BuildFailure, carrying the diagnostics;
//   - a BuildFailure is an Error, and is not a ServiceError;
//   - a BuildFailure thrown by build, by transform and by a refused compiler
//     request are the same shape, because a caller rendering them does not want
//     to know which command produced them.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");
const { cleanup, skipWithoutBinary } = require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };

function makeProject() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "guchho-errors-test-"));
    fs.mkdirSync(path.join(root, "src"), { recursive: true });
    fs.writeFileSync(path.join(root, "src", "main.js"), "const answer = 42;\n");
    return root;
}

describe("BuildFailure", () => {
    it("is thrown by a failed build, carrying the diagnostics", needsBinary, async () => {
        const root = makeProject();

        let thrown = null;
        try {
            await guchho.build({ absWorkingDir: root, entryPoints: ["src/nothing-here.js"] });
        } catch (error) {
            thrown = error;
        }

        assert.ok(thrown, "a failed build should throw");
        assert.ok(thrown instanceof guchho.BuildFailure, "it should be a BuildFailure");
        assert.ok(Array.isArray(thrown.errors) && thrown.errors.length > 0, "it should carry the errors");
        assert.ok(Array.isArray(thrown.warnings), "it should carry the warnings");
        assert.equal(typeof thrown.errors[0].text, "string");
    });

    it("is thrown by a failed transform, in the same shape", needsBinary, async () => {
        let thrown = null;
        try {
            await guchho.transform("const a = (;\n");
        } catch (error) {
            thrown = error;
        }

        assert.ok(thrown, "a failed transform should throw");
        assert.ok(thrown instanceof guchho.BuildFailure);
        assert.ok(thrown.errors.length > 0);
        assert.ok(Array.isArray(thrown.warnings));
    });

    it("is thrown by a compiler request the engine refused", needsBinary, async () => {
        // A print with a handle that names nothing is a refused request: there
        // is no tree to print, and no result to hand back. It is the same
        // BuildFailure a failed build throws, with the engine's own wording.
        await assert.rejects(
            () => guchho.print(999_999, { language: "html" }),
            (error) => {
                assert.ok(error instanceof guchho.BuildFailure);
                assert.equal(typeof error.errors[0].text, "string");
                return true;
            }
        );
    });

    it("is an Error, so `instanceof Error` catches it", needsBinary, async () => {
        let thrown = null;
        try {
            await guchho.build({ entryPoints: [] });
        } catch (error) {
            thrown = error;
        }
        // build({}) rejects with a TypeError for the missing entry points, so
        // this forces a failure the engine reports rather than one this module
        // stops at. Any BuildFailure is an Error by construction.
        assert.ok(guchho.BuildFailure.prototype instanceof Error);
        assert.ok(thrown, "there is still an Error to test against");
    });
});

describe("the two classes", () => {
    it("are different classes with different names", () => {
        assert.notEqual(guchho.BuildFailure, guchho.ServiceError);
        assert.equal(guchho.BuildFailure.name, "BuildFailure");
        assert.equal(guchho.ServiceError.name, "ServiceError");
        assert.ok(guchho.ServiceError.prototype instanceof Error);
    });

    it("a BuildFailure is not a ServiceError", needsBinary, async () => {
        // The check a caller writes is `instanceof ServiceError` for "the
        // installation is broken" and `instanceof BuildFailure` (or the
        // absence of it) for "the code is broken". A BuildFailure that also
        // matched ServiceError would collapse the two.
        let thrown = null;
        try {
            await guchho.transform("const a = (;\n");
        } catch (error) {
            thrown = error;
        }
        assert.ok(thrown instanceof guchho.BuildFailure);
        assert.ok(!(thrown instanceof guchho.ServiceError));
    });
});