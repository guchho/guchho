// Which engine runs a command: WebAssembly in-process, or a native binary.
//
// lib/main.js's resolveEngine() walks a fixed order: GUCHHO_BINARY, then
// GUCHHO_BACKEND, then wasm-first when @guchho/wasm is installed, then the
// native platform package and the local build tree. Each rung is a decision a
// person can force from the environment, so each rung is a test here.
//
// The tests stage a copy of the package into a temporary node_modules the way
// npm would lay it out, including a stand-in @guchho/wasm when the case needs
// one. require.resolve("@guchho/wasm") inside the staged lib/main.js then
// answers from the staged tree rather than from whatever this machine happens
// to have installed, which is the whole point of staging.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const { cleanup, stageNodeModules, fakeBinary } = require("./helpers/workspace");
const { BINARY_NAME } = require("./helpers/paths");

after(cleanup);

// The environment variables resolveEngine reads, saved and restored around
// every test that sets one. A leak here would decide the next test's answer
// before it ran, and the failure would read as a resolution bug.
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

// Loads a copy of lib/main.js out of a staged tree, so require.resolve inside
// it walks the staged node_modules. A separate module instance per call keeps
// one test's staging from deciding another's answer.
function loadMain(staged) {
    const entry = path.join(staged.guchhoPkg, "lib", "main.js");
    delete require.cache[require.resolve(entry)];
    return require(entry);
}

// Puts a stand-in @guchho/wasm into the staged node_modules. Resolution only
// needs the manifest to name a file that exists; nothing here loads the engine.
function stageWasm(staged) {
    const wasmPkg = path.join(staged.modules, "@guchho", "wasm");
    fs.mkdirSync(wasmPkg, { recursive: true });
    fs.writeFileSync(
        path.join(wasmPkg, "package.json"),
        JSON.stringify({ name: "@guchho/wasm", version: "0.0.0-test", main: "index.js" })
    );
    fs.writeFileSync(path.join(wasmPkg, "index.js"), "module.exports = {};\n");
}

describe("resolveEngine", () => {
    it("is wasm-first when @guchho/wasm is installed, native binary or not", () => {
        const staged = stageNodeModules({ withPlatform: false });
        stageWasm(staged);
        const { resolveEngine } = loadMain(staged);

        withEngineEnv({}, () => {
            assert.deepEqual(resolveEngine(), { type: "wasm" });
        });
    });

    it("prefers wasm even when a native platform package is also installed", () => {
        const staged = stageNodeModules();
        stageWasm(staged);
        const { resolveEngine } = loadMain(staged);

        withEngineEnv({}, () => {
            assert.deepEqual(resolveEngine(), { type: "wasm" });
        });
    });

    it("falls back to the native binary when wasm is not installed", () => {
        const staged = stageNodeModules();
        const { resolveEngine } = loadMain(staged);

        withEngineEnv({}, () => {
            const engine = resolveEngine();
            assert.equal(engine.type, "native");
            assert.equal(path.basename(engine.binaryPath), BINARY_NAME);
            assert.ok(fs.existsSync(engine.binaryPath));
        });
    });

    it("GUCHHO_BACKEND=native skips wasm even when it is installed", () => {
        const staged = stageNodeModules();
        stageWasm(staged);
        const { resolveEngine } = loadMain(staged);

        withEngineEnv({ GUCHHO_BACKEND: "native" }, () => {
            const engine = resolveEngine();
            assert.equal(engine.type, "native");
            assert.ok(engine.binaryPath);
        });
    });

    it("GUCHHO_BACKEND=wasm resolves wasm, and errors naming the package when it is missing", () => {
        const withWasm = stageNodeModules();
        stageWasm(withWasm);
        const { resolveEngine: resolveWith } = loadMain(withWasm);

        withEngineEnv({ GUCHHO_BACKEND: "wasm" }, () => {
            assert.deepEqual(resolveWith(), { type: "wasm" });
        });

        const withoutWasm = stageNodeModules({ withPlatform: false });
        const { resolveEngine: resolveWithout } = loadMain(withoutWasm);

        withEngineEnv({ GUCHHO_BACKEND: "wasm" }, () => {
            assert.throws(
                () => resolveWithout(),
                /GUCHHO_BACKEND=wasm, but @guchho\/wasm is not installed/,
                "the error should say the package is missing"
            );
            assert.throws(
                () => resolveWithout(),
                /npm install @guchho\/wasm/,
                "the error should say what to install"
            );
        });
    });

    it("GUCHHO_BACKEND with any other value is refused rather than guessed at", () => {
        const staged = stageNodeModules();
        const { resolveEngine } = loadMain(staged);

        withEngineEnv({ GUCHHO_BACKEND: "yes" }, () => {
            assert.throws(
                () => resolveEngine(),
                /GUCHHO_BACKEND is set to "yes", which is not one of "wasm" or "native"/
            );
        });
    });

    it("GUCHHO_BINARY wins over both wasm-first and GUCHHO_BACKEND", () => {
        const staged = stageNodeModules();
        stageWasm(staged);
        const { resolveEngine } = loadMain(staged);

        const override = path.join(staged.root, "custom-guchho");
        fs.writeFileSync(override, fakeBinary());

        // Over the default (which would be wasm-first).
        withEngineEnv({ GUCHHO_BINARY: override }, () => {
            const engine = resolveEngine();
            assert.equal(engine.type, "native");
            assert.equal(engine.binaryPath, override);
        });

        // Over an explicit backend too: the path is the more specific ask.
        withEngineEnv({ GUCHHO_BINARY: override, GUCHHO_BACKEND: "wasm" }, () => {
            const engine = resolveEngine();
            assert.equal(engine.type, "native");
            assert.equal(engine.binaryPath, override);
        });
    });

    it("a GUCHHO_BINARY that is not a file is stopped on, not fallen through from", () => {
        const staged = stageNodeModules();
        stageWasm(staged);
        const { resolveEngine } = loadMain(staged);

        withEngineEnv({ GUCHHO_BINARY: path.join(staged.root, "no-such-binary") }, () => {
            assert.throws(
                () => resolveEngine(),
                /GUCHHO_BINARY is set to .* which is not a file/
            );
        });
    });

    it("names both install options when nothing at all is available", () => {
        // No wasm, no platform package, no binary beside the shim, and the
        // staged tree's climb for a build directory lands in the temporary
        // root, where there is none.
        const staged = stageNodeModules({ withPlatform: false });
        const { resolveEngine } = loadMain(staged);

        withEngineEnv({}, () => {
            assert.throws(
                () => resolveEngine(),
                /No Guchho engine found/,
                "the error should say no engine was found"
            );
            assert.throws(
                () => resolveEngine(),
                new RegExp(`npm install -D @guchho/${process.platform}-${process.arch}`),
                "the error should name the native package to install"
            );
            assert.throws(
                () => resolveEngine(),
                /npm install @guchho\/wasm/,
                "the error should also offer the wasm package"
            );
        });
    });
});
