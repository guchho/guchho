// The compiler API reaching the WebAssembly engine.
//
// lib/engine.js decides which engine answers lexer/parse/print: the native
// service when a binary is installed, the WebAssembly engine otherwise (or when
// GUCHHO_BACKEND=wasm forces it). This file checks the routing without needing
// either engine to be real: a stand-in @guchho/wasm is staged into the tree and
// its request() answers with markers the test can recognise. A test that needed
// a built engine would skip on every machine that had not run emsdk, which is
// the machine most likely to have broken the routing.
//
// The real engine's own compiler methods are exercised in wasm-package.test.js,
// which does need the built artifacts and skips without them. The two together
// cover the seam: this one that guchho sends the request, that one that the
// engine answers it.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const { cleanup, stageNodeModules } = require("./helpers/workspace");

after(cleanup);

const ENGINE_VARS = ["GUCHHO_BACKEND", "GUCHHO_BINARY"];

function withEngineEnv(overrides, fn) {
    const saved = {};
    for (const key of ENGINE_VARS) {
        saved[key] = process.env[key];
        if (overrides[key] === undefined) {
            delete process.env[key];
        } else {
            process.env[key] = overrides[key];
        }
    }
    try {
        return fn();
    } finally {
        for (const key of ENGINE_VARS) {
            if (saved[key] === undefined) {
                delete process.env[key];
            } else {
                process.env[key] = saved[key];
            }
        }
    }
}

// A stand-in @guchho/wasm whose createGuchho() answers each compile command
// with a recognisable marker. Every response records nothing on the test side —
// the marker in the response is how the test knows the request arrived here.
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
export async function createGuchho() {
    return {
        request(payload) {
            if (payload.command.startsWith("lex-")) {
                return {
                    errors: [],
                    warnings: [],
                    tokens: [{ kind: "from-wasm-stub", value: payload.input, start: 0, end: 0, line: 1, column: 0, length: 0 }],
                    count: 1,
                };
            }
            if (payload.command.startsWith("parse-")) {
                return { errors: [], warnings: [], id: 4242, partCount: 1, symbols: [] };
            }
            if (payload.command.startsWith("print-")) {
                return { errors: [], warnings: [], code: "printed:" + payload.ast };
            }
            return { errors: [], warnings: [] };
        },
    };
}
`;
    fs.writeFileSync(path.join(wasmPkg, "index.js"), body);
}

// Loads a copy of the whole guchho package out of a staged tree, so the
// dynamic import("@guchho/wasm") inside lib/engine.js resolves the staged stub.
function loadGuchho(staged) {
    const entry = path.join(staged.guchhoPkg, "lib", "index.js");
    delete require.cache[require.resolve(entry)];
    return require(entry);
}

describe("the compiler API over the WebAssembly engine", () => {
    it("routes lexer through @guchho/wasm when it is forced", async () => {
        const staged = stageNodeModules({ withPlatform: false });
        stageWasmStub(staged);
        const guchho = loadGuchho(staged);

        await withEngineEnv({ GUCHHO_BACKEND: "wasm" }, async () => {
            const result = await guchho.lexer("const a = 1", { language: "js" });
            assert.equal(result.count, 1);
            assert.equal(result.tokens[0].kind, "from-wasm-stub");
            assert.equal(result.tokens[0].value, "const a = 1");
        });
    });

    it("carries a parse handle from parse to print through the stub", async () => {
        const staged = stageNodeModules({ withPlatform: false });
        stageWasmStub(staged);
        const guchho = loadGuchho(staged);

        await withEngineEnv({ GUCHHO_BACKEND: "wasm" }, async () => {
            const parsed = await guchho.parse("const a = 1;", { language: "js" });
            assert.equal(parsed.ast, 4242);

            const printed = await guchho.print(parsed.ast, { language: "js" });
            assert.equal(printed.code, "printed:4242");
        });
    });

    it("prefers the native binary when one is installed, without loading wasm", async () => {
        // A real platform package is staged, so getBinaryPath() resolves. With
        // no GUCHHO_BACKEND the compiler transport must choose native and never
        // reach the stub — the marker would appear if it did.
        const staged = stageNodeModules();
        stageWasmStub(staged);
        const guchho = loadGuchho(staged);

        await withEngineEnv({}, async () => {
            // The staged native binary is a stand-in, not an executable, so the
            // native request fails to start the process. That failure is the
            // proof native was chosen: the stub would have answered.
            await assert.rejects(() => guchho.lexer("a", { language: "js" }));
        });
    });
});
