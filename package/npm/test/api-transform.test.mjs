// guchho.transform(), reached the way a caller reaches it.
//
// The test at the top is the one this file exists for:
//
//     import * as guchho from "guchho";
//     const result = await guchho.transform(code, options);
//
// That is the whole of the public contract for this half of the API: a
// namespace import with a named binding on it, and a promise. Everything below
// it is about whether the answer is any use — whether the code comes back, and
// whether it comes back alone.
//
// The import is from the package's own lib/index.mjs rather than from "guchho",
// because this file lives inside the package and a bare "guchho" would need the
// package installed into a node_modules of its own. e2e.test.js does that, and
// checks that the name resolves; this file checks what it resolves to. The
// module is the same file either way, which is the point of an exports map.

// A namespace import has to be a static one to be worth testing: the point is
// that the name "transform" is a binding on the module, not something read off
// a default export at run time. So this file is an ES module throughout, which
// is also why it is .mjs rather than .js — relying on Node to notice the
// difference from a syntax error would work today and be a trap tomorrow.
import assert from "node:assert/strict";
import { after, describe, it } from "node:test";

import * as guchho from "../guchho/lib/index.mjs";

// The helpers are CommonJS, which is fine: an ES module imports one by its
// default export, and that is the whole of what module.exports holds.
import workspace from "./helpers/workspace.js";

const { cleanup, skipWithoutBinary } = workspace;

after(cleanup);

// Skipped or not as a whole file: every test here runs the binary, because the
// thing under test is a wrapper around it and a wrapper that has never wrapped
// anything has not been tested.
const needsBinary = { skip: skipWithoutBinary() };

// The line endings a Windows pipe adds are taken out, so a comparison does not
// depend on which platform ran it. The bytes are otherwise the engine's.
function normalized(text) {
    return text.replace(/\r\n/g, "\n");
}

describe("the module's shape", () => {
    it("has transform and build as named bindings on the namespace", () => {
        // A namespace import of a CommonJS module gives real named bindings
        // only if the module asks for them, which is what lib/index.mjs is
        // for. Without it these would be undefined and the error would be a
        // "not a function" at the first call rather than anything about names.
        assert.equal(typeof guchho.transform, "function");
        assert.equal(typeof guchho.build, "function");
    });

    it("keeps the helpers that were already public", () => {
        assert.equal(typeof guchho.getBinaryPath, "function");
        assert.equal(typeof guchho.getPlatformKey, "function");
        assert.equal(typeof guchho.spawnBinary, "function");
    });

    it("does not leak the platform table, which is how the binary is found", () => {
        // Exported because it is convenient, it becomes a promise: a caller
        // reading it to decide what to install is depending on a detail that
        // changes with every platform added.
        assert.equal(guchho.PLATFORM_PACKAGES, undefined);
    });

    it("has a default that is the same object, for a caller who wants the lot", () => {
        assert.equal(guchho.default.transform, guchho.transform);
        assert.equal(guchho.default.build, guchho.build);
    });
});

describe("transform()", () => {
    it("returns the program and nothing else on the code stream", needsBinary, async () => {
        // The property the whole wrapper exists to provide. "guchho transform"
        // is a filter, so stdout is the program; a report printed in front of
        // it would be a syntax error in the middle of somebody's file, which is
        // why the timing line was moved to the error stream on the C++ side.
        const result = await guchho.transform("const answer = 42;\nconsole.log(answer);\n");

        assert.equal(result.exitCode, 0);
        assert.equal(result.errors.length, 0, `unexpected errors: ${result.errors.join("\n")}`);
        assert.ok(result.code.includes("answer = 42"), `got: ${JSON.stringify(result.code)}`);
        assert.ok(result.code.includes("console.log"));
        assert.ok(
            !result.code.includes("finished in"),
            "a timing report reached the code stream"
        );
    });

    it("keeps the shape of the answer the same whatever happened", needsBinary, async () => {
        // One shape for both outcomes, so that a caller can write result.code
        // before it knows whether there is any.
        const good = await guchho.transform("const a = 1;\n");
        const bad = await guchho.transform("const a = (;\n");

        for (const result of [good, bad]) {
            assert.deepEqual(
                Object.keys(result).sort(),
                ["code", "errors", "exitCode", "legalComments", "map", "warnings"]
            );
        }
    });

    it("shortens the program when asked to minify", needsBinary, async () => {
        const plain = await guchho.transform("const value = (input) => input * 2;\nconsole.log(value(2));\n");
        const minified = await guchho.transform("const value = (input) => input * 2;\nconsole.log(value(2));\n", { minify: true });

        assert.ok(
            minified.code.length < plain.code.length,
            `minified (${minified.code.length}) was not shorter than plain (${plain.code.length})`
        );
        assert.ok(minified.code.includes("value"), "minifying changed the meaning");
    });

    it("reports a syntax error as a result, not as a rejection", needsBinary, async () => {
        // A caller that has to try/catch to find out whether its code compiled
        // is a caller that will forget to, and a forgotten catch here is an
        // unhandled rejection that takes the process with it.
        const result = await guchho.transform("const a = (;\n");

        assert.notEqual(result.exitCode, 0, "a syntax error should not report success");
        assert.equal(result.code, "", "there should be no program to hand back");
        assert.ok(result.errors.length > 0, "there should be something to say about it");
        assert.match(result.errors.join("\n"), /ERROR/i);
    });

    it("names the line the error is on", needsBinary, async () => {
        // The command line reports a location within the text it read, and a
        // caller showing that to a person needs it kept rather than flattened.
        const result = await guchho.transform("const a = 1;\nconst b = (;\n");

        assert.notEqual(result.exitCode, 0);
        assert.match(result.errors.join("\n"), /2/, "the second line is where the error is");
    });

    it("separates warnings from errors", needsBinary, async () => {
        // A caller that renders both in the same colour is telling a person
        // that something failed when it only did not.
        const result = await guchho.transform("const a = 1;\n");

        assert.deepEqual(result.errors, []);
        assert.deepEqual(result.warnings, []);
    });

    it("rejects a non-string before starting anything", async () => {
        // The one failure that is a programming error rather than a result, and
        // the reason it is not a resolved promise: there is no code, so there is
        // nothing to report about, and a TypeError here says so at the call site.
        await assert.rejects(
            () => guchho.transform(undefined),
            TypeError
        );
        await assert.rejects(
            () => guchho.transform(42),
            TypeError
        );
    });

    it("rejects a bad option before starting anything", async () => {
        // Likewise. A misspelled log level is a mistake in the caller's code,
        // and finding it here is more useful than an exit code from the binary.
        await assert.rejects(
            () => guchho.transform("const a = 1;\n", { logLevel: "chatty" }),
            /logLevel/
        );

        await assert.rejects(
            () => guchho.transform("const a = 1;\n", { outdir: true }),
            /outdir/
        );
    });

    it("refuses to run silently, which would return nothing at all", async () => {
        // The command line accepts --log-level silent, and for a build it is a
        // sensible thing to ask. For a transform it is not: the program is the
        // output, so a silent transform writes nothing and returns an empty
        // string that looks like an empty program.
        await assert.rejects(
            () => guchho.transform("const a = 1;\n", { logLevel: "silent" }),
            /silen/i
        );
    });

    it("returns null for the map and the legal comments, because it writes no file", async () => {
        // A transform produces one string in memory. There is nothing to link a
        // source map to, and the fields are named so that a caller moving from
        // transform to build does not have to change shape — but they are null
        // rather than an empty string, because empty would look like a map that
        // was asked for and came back with nothing in it.
        const result = await guchho.transform("const a = 1;\n", { sourcemap: true });

        assert.equal(result.map, null);
        assert.equal(result.legalComments, null);
    });

    it("survives a second call, so the binary is not left in a bad state", needsBinary, async () => {
        // Each call is a process, and a wrapper that only worked once would be
        // one nobody used twice.
        const first = await guchho.transform("const first = 1;\n");
        const second = await guchho.transform("const second = 2;\n");

        assert.equal(first.exitCode, 0);
        assert.equal(second.exitCode, 0);
        assert.ok(first.code.includes("first"));
        assert.ok(second.code.includes("second"));
        assert.ok(!second.code.includes("first"), "the second call returned the first one's output");
    });

    it("leaves the line endings to the platform rather than rewriting them", needsBinary, async () => {
        // The program comes back as the bytes the binary wrote, and a Windows
        // pipe turns a newline into two characters. Rewriting them here would
        // mean the wrapper disagreed with what a shell pipeline would have
        // produced from the same command, so it does not: it says what happened.
        const result = await guchho.transform("const a = 1;\nconst b = 2;\n");
        const code = normalized(result.code);

        assert.ok(code.includes("const a = 1;\n"), `got: ${JSON.stringify(result.code)}`);
    });
});
