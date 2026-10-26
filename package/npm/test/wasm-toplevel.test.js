// The top level of @guchho/wasm.
//
// The package publishes a createGuchho() factory and, beside it, a set of
// module-level functions for the caller who wants to lex a string or run one
// build without holding an engine: configure, loadFiles, lexer, parse, print,
// build, pack, unpack, request and version. They run against one shared engine,
// created on first use, and configure() is what the in-memory tree that engine
// is created from is set with.
//
// The export shapes need no engine and are checked always. The behaviour behind
// the shared engine needs the built artifacts — scripts/build-wasm.sh puts
// lib/guchho.wasm and lib/guchho.js in the package — so those tests skip on a
// checkout that has not built one, rather than failing.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { describe, it } = require("node:test");

const { WASM_DIR, readManifest } = require("./helpers/paths");

const manifest = readManifest(WASM_DIR);

// The functions that run against the shared engine. createGuchho and runCli are
// the factory and the CLI entry; they are checked alongside. toVirtual and
// mountHost are Node-only path utilities, synchronous in the ES module.
const TOP_LEVEL = [
    "build",
    "configure",
    "lexer",
    "loadFiles",
    "mountHost",
    "pack",
    "packBytes",
    "parse",
    "print",
    "request",
    "toVirtual",
    "unpack",
    "unpackBytes",
    "version",
];

function hasBuiltEngine() {
    return (
        fs.existsSync(path.join(WASM_DIR, "lib", "guchho.wasm")) &&
        fs.existsSync(path.join(WASM_DIR, "lib", "guchho.js"))
    );
}

const skipWithoutEngine = hasBuiltEngine()
    ? false
    : "no built wasm engine found; run scripts/build-wasm.sh";

// A files map the shared engine can build from. It is installed by configure()
// before the engine is created, and the build test below reads it back.
const PROJECT = {
    "brochure/src/index.html":
        '<!doctype html><html><body><script type="module" src="./main.js"></script></body></html>',
    "brochure/src/main.js": 'console.log("hello from the shared engine");\n',
};

describe("@guchho/wasm top-level exports", () => {
    it("exposes the shared-engine functions from the ES module", async () => {
        const module = await import("../@guchho/wasm/index.mjs");

        for (const name of TOP_LEVEL) {
            assert.equal(typeof module[name], "function", `${name} should be exported`);
        }
        assert.equal(typeof module.createGuchho, "function");
        assert.equal(typeof module.runCli, "function");
        // The default is the factory itself, so the ESM and CommonJS entries
        // answer a default import the same way (index.cjs sets .default too).
        assert.equal(module.default, module.createGuchho);
    });

    it("exposes the same names through require()", () => {
        const module = require("../@guchho/wasm/index.cjs");

        // The CommonJS shape is the factory itself, with the names hung off it
        // so that both `require("@guchho/wasm")()` and destructuring work.
        assert.equal(typeof module, "function");
        for (const name of TOP_LEVEL) {
            assert.equal(typeof module[name], "function", `${name} should be reachable`);
        }
        assert.equal(typeof module.createGuchho, "function");
        assert.equal(typeof module.runCli, "function");
        assert.equal(module.default, module.createGuchho);
    });
});

describe("@guchho/wasm configure", () => {
    it("accepts options before the engine exists", async () => {
        const { configure } = await import("../@guchho/wasm/index.mjs");

        assert.equal(configure(), undefined, "configure() with nothing is a no-op");
        assert.equal(configure({ files: { "a.js": "export default 1;" } }), undefined);
        // A second call before the engine exists replaces what was set. The
        // sinks keep the shared engine's own banner out of the test output.
        assert.equal(
            configure({ files: PROJECT, print: () => {}, printErr: () => {} }),
            undefined
        );
    });
});

describe("@guchho/wasm shared engine", { skip: skipWithoutEngine }, () => {
    it("reports the version the manifest claims and refuses configure afterwards", async () => {
        const { version, configure } = await import("../@guchho/wasm/index.mjs");

        assert.equal(await version(), manifest.version);
        assert.throws(() => configure({}), /already been created/);
    });

    it("lexes through the shared engine", async () => {
        const { lexer } = await import("../@guchho/wasm/index.mjs");

        const result = await lexer("<a>\n<b>", { language: "html" });
        assert.deepEqual(result.errors, []);
        assert.equal(result.count, 4);
    });

    it("parses and prints through the shared engine", async () => {
        const { parse, print } = await import("../@guchho/wasm/index.mjs");

        const parsed = await parse("const a = 1;", { language: "js" });
        assert.equal(typeof parsed.ast, "number");

        const printed = await print(parsed.ast, { language: "js" });
        assert.match(printed.code, /const a = 1/);
    });

    it("builds from the tree configure() installed", async () => {
        const { build } = await import("../@guchho/wasm/index.mjs");

        const result = await build("brochure/src/index.html", { outdir: "brochure/dist" });
        assert.equal(result.exitCode, 0, "the shared engine should build the configured tree");
    });
});
