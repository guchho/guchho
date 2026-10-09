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
// whether a failure says what is wrong with it.
//
// This file was rewritten when the API moved to the service and to esbuild's
// shape. Three things changed and all three are worth stating:
//
//   - "exitCode" is gone. There is no process per transform any more, so there
//     is no exit code; a transform that fails throws, and the error carries the
//     diagnostics.
//
//   - "legalComments" is gone. Nothing in the service reports it, and a field
//     that is always null is a field a caller learns to ignore.
//
//   - "map" is now "" when there is no sourcemap, not null. That is esbuild's
//     shape, and a caller moving between the two does not have to change.
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

    it("has the rest of the API as named bindings too", () => {
        // Each one spelled out. A namespace import that quietly lost a binding
        // would resolve, import and destructure without complaint, and fail at
        // the first call — which is to say, at the first build.
        assert.equal(typeof guchho.context, "function");
        assert.equal(typeof guchho.analyze, "function");
        assert.equal(typeof guchho.analyzeMetafile, "function");
        assert.equal(typeof guchho.watch, "function");
        assert.equal(typeof guchho.serve, "function");
        assert.equal(typeof guchho.stop, "function");
        assert.equal(typeof guchho.pack, "function");
        assert.equal(typeof guchho.version, "string");
        assert.equal(typeof guchho.lexer, "function");
        assert.equal(typeof guchho.parse, "function");
        assert.equal(typeof guchho.print, "function");
        assert.equal(typeof guchho.Watcher, "function");
        assert.equal(typeof guchho.DevServer, "function");
    });

    it("dropped the names the CHANGELOG says are gone", () => {
        // The break is intentional and documented. A binding that is still
        // there is a caller depending on something that was promised gone.
        // analyzeMetafile is not in this list because it is not gone: analyze
        // changed meaning, and the metafile report it used to do is still
        // reachable under the name that says so.
        assert.equal(guchho.formatMessages, undefined);
        // zip is not a leftover of the old surface either: it became pack, and
        // a name that does the same thing twice is a promise nobody wants.
        assert.equal(guchho.zip, undefined);
        assert.equal(guchho.getBinaryPath, undefined);
        assert.equal(guchho.getPlatformKey, undefined);
        assert.equal(guchho.spawnBinary, undefined);
        // The twelve per-language compiler names are the reason this is a
        // breaking release, so a leftover one would be the worst kind of leak.
        for (const language of ["HTML", "CSS", "JS"]) {
            for (const stage of ["lex", "parse", "transform", "print"]) {
                assert.equal(guchho[`${stage}${language}`], undefined);
            }
        }
        assert.equal(guchho.transformAst, undefined);
    });

    it("binds analyze and analyzeMetafile as two different functions", () => {
        // Named re-export rather than a wrapper, so an ESM caller and a CommonJS
        // caller get the same function object. A wrapper here would be a second
        // answer to the same question, and the two would drift.
        assert.equal(typeof guchho.analyze, "function");
        assert.equal(typeof guchho.analyzeMetafile, "function");
        assert.notEqual(guchho.analyze, guchho.analyzeMetafile);
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
    it("returns the program and nothing else in the code", needsBinary, async () => {
        // The property the whole wrapper exists to provide. "guchho transform"
        // is a filter, so the output is the program; a report printed in front of
        // it would be a syntax error in the middle of somebody's file.
        const result = await guchho.transform("const answer = 42;\nconsole.log(answer);\n");

        assert.equal(result.errors.length, 0, `unexpected errors: ${result.errors.join("\n")}`);
        assert.equal(typeof result.code, "string", "code should be text, not bytes");
        assert.ok(result.code.includes("answer = 42"), `got: ${JSON.stringify(result.code)}`);
        assert.ok(result.code.includes("console.log"));
        assert.ok(
            !result.code.includes("finished in"),
            "a timing report reached the code"
        );
    });

    it("keeps the diagnostics the same shape whatever happened", needsBinary, async () => {
        // A caller renders result.errors on the happy path and thrown.errors on
        // the other one, and the two should not need different code. So both
        // carry errors and warnings, and both of those are arrays of the same
        // message shape.
        const good = await guchho.transform("const a = 1;\n");
        const bad = await guchho.transform("const a = (;\n").then(
            () => null,
            (error) => error
        );

        assert.ok(bad, "a broken transform should throw");
        for (const result of [good, bad]) {
            assert.ok(Array.isArray(result.errors), "errors should be an array");
            assert.ok(Array.isArray(result.warnings), "warnings should be an array");
            for (const message of result.errors) {
                assert.equal(typeof message.text, "string");
                assert.equal(typeof message.id, "string");
                assert.equal(typeof message.pluginName, "string");
                assert.ok(Array.isArray(message.notes));
                assert.ok("location" in message);
            }
        }

        // A thrown failure has no program in it, and says so by not having the
        // field rather than by having an empty one: an empty string is a
        // successful transform of nothing.
        assert.equal(typeof good.code, "string");
        assert.equal(bad.code, undefined, "a failure should carry no code");
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

    it("throws on a syntax error, and the error says what is wrong", needsBinary, async () => {
        // A caller that has to inspect a resolved value to find out whether its
        // code compiled is a caller that will forget to check, and a forgotten
        // check here ships a broken build. Throwing is the shape esbuild
        // publishes, and the error carries the diagnostics so the catch block
        // has something to render.
        let thrown = null;
        try {
            await guchho.transform("const a = (;\n");
        } catch (error) {
            thrown = error;
        }

        assert.ok(thrown, "a syntax error should throw");
        assert.ok(thrown.errors.length > 0, "the error should carry the diagnostics");
        assert.equal(typeof thrown.errors[0].text, "string", "each error should say something");
        assert.ok(Array.isArray(thrown.warnings), "the error should carry warnings too");
    });

    it("names the line the error is on", needsBinary, async () => {
        // The engine reports a location within the text it read, and a caller
        // showing that to a person needs it kept rather than flattened into a
        // sentence.
        const result = await guchho.transform("const a = 1;\nconst b = (;\n", {
            sourcefile: "input.js",
        }).then(() => null, (error) => error);

        const error = result.errors.find((m) => m.location !== null);
        assert.ok(error, "at least one message should carry a location");
        assert.equal(error.location.file, "input.js", "sourcefile should be the file named");
        assert.equal(error.location.line, 2, "the second line is where the error is");
    });

    it("separates warnings from errors", needsBinary, async () => {
        // A caller that renders both in the same colour is telling a person
        // that something failed when it only did not.
        const result = await guchho.transform("const a = 1;\n");

        assert.deepEqual(result.errors, []);
        assert.deepEqual(result.warnings, []);
    });

    it("rejects input that is not source, before starting anything", async () => {
        // The one failure that is a programming error rather than a result, and
        // the reason it is not a resolved promise: there is no code, so there is
        // nothing to report about, and a TypeError here says so at the call site.
        await assert.rejects(() => guchho.transform(undefined), TypeError);
        await assert.rejects(() => guchho.transform(null), TypeError);
        await assert.rejects(() => guchho.transform(42), TypeError);
    });

    it("rejects a bad option before starting anything", async () => {
        // Likewise. A misspelled log level is a mistake in the caller's code,
        // and finding it here is more useful than a build failure from the engine.
        await assert.rejects(
            () => guchho.transform("const a = 1;\n", { logLevel: "chatty" }),
            /logLevel/
        );

        await assert.rejects(
            () => guchho.transform("const a = 1;\n", { target: true }),
            /target/
        );
    });

    it("takes a Buffer and a Uint8Array as well as a string", needsBinary, async () => {
        // A caller reading a file off disk already has bytes, and making it
        // decode to a string first is a copy it did not ask for. It also matters
        // for correctness: a file with a byte-order mark is not the text a
        // decode would guess it was.
        const source = "const fromBytes = 1;\n";

        for (const input of [Buffer.from(source, "utf8"), new Uint8Array(Buffer.from(source, "utf8"))]) {
            const result = await guchho.transform(input, { loader: "js" });
            assert.deepEqual(result.errors, []);
            assert.ok(result.code.includes("fromBytes"), `got: ${JSON.stringify(result.code)}`);
        }
    });

    it("uses the loader it was given", needsBinary, async () => {
        // A transform has no extensions to speak of — one input, one name for it —
        // so the loader is a single name rather than the map a build takes.
        const result = await guchho.transform("const x: number = 1;\nconsole.log(x);\n", {
            loader: "ts",
        });

        assert.deepEqual(result.errors, [], `unexpected errors: ${result.errors.join("\n")}`);
        assert.ok(!result.code.includes(": number"), `the type annotation survived: ${result.code}`);
    });

    it("returns an empty map when there is no sourcemap", needsBinary, async () => {
        // esbuild's shape: "" rather than null, and rather than a field that is
        // sometimes there. A caller moving between bundlers reads it the same way.
        const result = await guchho.transform("const a = 1;\n");

        assert.equal(result.map, "");
    });

    it("survives a second call, and does not answer with the first one's output", needsBinary, async () => {
        // This is the whole reason there is a service. Each call used to be a
        // process, and a wrapper that only worked once would be one nobody used
        // twice; now it is one process answering many requests, and the failure
        // this guards against is the answers getting crossed.
        const first = await guchho.transform("const first = 1;\n");
        const second = await guchho.transform("const second = 2;\n");

        assert.ok(first.code.includes("first"), `got: ${JSON.stringify(first.code)}`);
        assert.ok(second.code.includes("second"), `got: ${JSON.stringify(second.code)}`);
        assert.ok(!second.code.includes("first"), "the second call returned the first one's output");
    });

    it("keeps requests that overlap, which is what a long-lived process is for", needsBinary, async () => {
        // Not sequential this time. Several requests in flight at once is the
        // normal case for a caller building a whole project, and it is where a
        // single reused reader or a mis-correlated id would show up.
        const sources = Array.from({ length: 8 }, (_, i) => `const v${i} = ${i};\n`);

        const results = await Promise.all(sources.map((code) => guchho.transform(code)));

        for (let i = 0; i < sources.length; i++) {
            assert.ok(
                results[i].code.includes(`v${i}`),
                `request ${i} got the wrong answer: ${JSON.stringify(results[i].code)}`
            );
            // And nothing else, so an answer cannot be right by containing
            // everything.
            for (let j = 0; j < sources.length; j++) {
                if (j !== i) {
                    assert.ok(
                        !results[i].code.includes(`v${j}`),
                        `request ${i} was given request ${j}'s answer`
                    );
                }
            }
        }
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
