// The browser surface of the guchho package.
//
// lib/browser/index.js is what the "browser" export condition resolves to. It
// installs the WebAssembly transport and re-exports the same orchestrators the
// Node entry uses, so the API a browser bundle gets is the API the package
// documents: build, transform, pack, lint, analyze, analyzeMetafile, context and
// the three compiler stages. watch and serve need a host process, unpack needs a
// host filesystem, and all three are kept as names that refuse; configure and
// loadFiles are the browser's own knobs.
//
// These tests need no built engine. A stand-in @guchho/wasm is staged into a
// copy of the package — the same trick api-compiler-wasm.test.js uses for the
// native entry — and its requestRaw() records every request and answers with a
// marker, so the routing is checked without emsdk. The real engine's own
// methods are exercised in wasm-package.test.js.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { after, before, describe, it } = require("node:test");

const { cleanup, stageNodeModules } = require("./helpers/workspace");

after(cleanup);

// A stand-in @guchho/wasm, ESM because the real package is. createGuchho()
// answers each command with a recognisable marker and pushes every payload onto
// a shared global the test reads. The `??=` leaves an object the test installed
// before the first import alone, so the test keeps the same reference.
function stageWasmStub(staged) {
    const wasmPkg = path.join(staged.modules, "@guchho", "wasm");
    fs.mkdirSync(wasmPkg, { recursive: true });
    fs.writeFileSync(
        path.join(wasmPkg, "package.json"),
        JSON.stringify({
            name: "@guchho/wasm",
            version: "0.0.0-test",
            type: "module",
            main: "index.js",
            exports: { ".": "./index.js" },
        })
    );

    const body = `
const state = (globalThis.__guchhoBrowserTest ??= { requests: [], created: [], loaded: null });

function answer(payload) {
    const command = typeof payload.command === "string" ? payload.command : "";
    if (command.startsWith("lex-")) {
        return {
            errors: [],
            warnings: [],
            tokens: [{ kind: "from-wasm-stub", value: payload.input ?? "", start: 0, end: 0, line: 1, column: 0, length: 0 }],
            count: 1,
        };
    }
    if (command.startsWith("parse-")) {
        return { errors: [], warnings: [], id: 4242, partCount: 1, symbols: [] };
    }
    if (command.startsWith("print-")) {
        return { errors: [], warnings: [], code: "printed:" + payload.ast };
    }
    if (command === "transform") {
        return { errors: [], warnings: [], code: "transformed:" + payload.input };
    }
    if (command === "pack") {
        return { errors: [], warnings: [], path: payload.outFile, size: "0" };
    }
    if (command === "lint") {
        return {
            errors: [],
            warnings: [],
            files: [{ path: "src/a.js", errorCount: 0, warningCount: 0, messages: [] }],
            messages: [],
            errorCount: 0,
            warningCount: 0,
            fixCount: 0,
            filesWithProblems: 0,
        };
    }
    if (command === "build" || command === "context" || command === "rebuild") {
        return { errors: [], warnings: [], outputFiles: [], key: 7 };
    }
    if (command === "analyze-metafile") {
        return { errors: [], warnings: [], text: "report" };
    }
    return { errors: [], warnings: [] };
}

export async function createGuchho(options = {}) {
    state.created.push(options);
    return {
        async requestRaw(payload) {
            state.requests.push(payload);
            return answer(payload);
        },
        loadFiles(files) {
            state.loaded = files;
        },
        dispose() {},
    };
}
`;
    fs.writeFileSync(path.join(wasmPkg, "index.js"), body);
}

// Loads the browser entry out of a staged tree, so its transport's dynamic
// import("@guchho/wasm") resolves the stub beside it.
function loadBrowser(staged) {
    const entry = path.join(staged.guchhoPkg, "lib", "browser", "index.js");
    return require(entry);
}

function state() {
    return globalThis.__guchhoBrowserTest;
}

describe("the browser entry", () => {
    let guchho;

    before(() => {
        globalThis.__guchhoBrowserTest = { requests: [], created: [], loaded: null };
        const staged = stageNodeModules({ withPlatform: false });
        stageWasmStub(staged);
        guchho = loadBrowser(staged);
    });

    it("routes the compiler stages to @guchho/wasm", async () => {
        const lexed = await guchho.lexer("a", { language: "html" });
        assert.equal(lexed.tokens[0].kind, "from-wasm-stub");
        assert.ok(state().requests.some((r) => r.command === "lex-html"));

        const parsed = await guchho.parse("const a = 1;", { language: "js" });
        assert.equal(parsed.ast, 4242);
        assert.ok(state().requests.some((r) => r.command === "parse-js"));

        const printed = await guchho.print(parsed.ast, { language: "js" });
        assert.equal(printed.code, "printed:4242");
        assert.ok(state().requests.some((r) => r.command === "print-js"));
    });

    it("routes build to @guchho/wasm", async () => {
        const result = await guchho.build({
            entrypoints: ["src/index.html"],
            outdir: "dist",
            write: false,
        });

        assert.equal(result.success, true);
        const request = state().requests.find((r) => r.command === "build");
        assert.ok(request, "a build request should have reached the stub");
        assert.deepEqual(request.entries, [[null, "src/index.html"]]);
        assert.ok(
            request.flags.includes("--outdir=dist"),
            `the outdir flag should travel, flags were ${JSON.stringify(request.flags)}`
        );
    });

    it("routes transform to @guchho/wasm", async () => {
        const result = await guchho.transform("let a = 1;", { loader: "js" });
        assert.equal(result.code, "transformed:let a = 1;");
        assert.ok(state().requests.some((r) => r.command === "transform"));
    });

    it("routes pack to @guchho/wasm", async () => {
        const result = await guchho.pack({ inputs: ["dist"], outFile: "dist.zip" });
        assert.equal(result.path, "dist.zip");
        assert.ok(state().requests.some((r) => r.command === "pack"));
    });

    it("routes lint to @guchho/wasm", async () => {
        const result = await guchho.lint({
            files: ["src/a.js"],
            rules: { "no-undef": "error" },
            maxWarnings: 0,
        });

        assert.equal(result.files[0].path, "src/a.js");
        assert.equal(result.maxWarningsExceeded, false);

        const request = state().requests.find((r) => r.command === "lint");
        assert.ok(request, "a lint request should have reached the stub");
        assert.deepEqual(request.files, ["src/a.js"]);
        assert.ok(request.flags.includes("--rule=no-undef:error"));
        assert.ok(request.flags.includes("--max-warnings=0"));
    });

    it("refuses watch, serve and unpack with a sentence naming the native package", () => {
        assert.throws(() => guchho.watch(), /not available in the browser/);
        assert.throws(() => guchho.serve(), /not available in the browser/);
        assert.throws(() => guchho.unpack({ archive: "a.zip" }), /not available in the browser/);
        assert.throws(
            () => guchho.unpack({ archive: "a.zip" }),
            /native package/,
            "the refusal should name the package that can do it"
        );
    });

    it("publishes configure and loadFiles, and loadFiles reaches the engine", async () => {
        assert.equal(typeof guchho.configure, "function");
        assert.equal(typeof guchho.loadFiles, "function");

        await guchho.loadFiles({ "src/extra.js": "export default 1;" });
        assert.deepEqual(state().loaded, { "src/extra.js": "export default 1;" });
    });

    it("refuses configure once the engine exists", () => {
        // The engine was created by the calls above, and configure only sets
        // what the engine is created from. Calling it now is a mistake worth a
        // sentence rather than a silent no-op.
        assert.throws(() => guchho.configure({ files: {} }), /already been created/);
    });
});
