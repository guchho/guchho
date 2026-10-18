"use strict";

// The WebAssembly transport: the engine running in this process, registered as
// the one the orchestrators use.
//
// The native package talks to a child process over a pipe. In a browser there
// is no child process, so the same service dispatch runs in-process through
// @guchho/wasm's `guchho_service`. This module is the whole difference: the
// orchestrators in lib/ are unchanged, and every request they build arrives
// here instead of at a pipe.
//
// Plugins are refused rather than emulated. A plugin hook is a callback the
// engine pauses for, and there is no callback transport across the C ABI. A
// request that names one is rejected with a message saying so, at the call
// rather than as a hang.

const { setTransport } = require("./transport");
const { unwrap } = require("./errors");

// What the engine is created from. Held here because a browser build needs its
// files in memory before the first call, and the engine can only be created
// once with them.
const config = { files: null, print: undefined, printErr: undefined };

let enginePromise = null;

function createEngine() {
    return import("@guchho/wasm").then((module) =>
        module.createGuchho({
            files: config.files === null ? undefined : config.files,
            print: config.print,
            printErr: config.printErr,
        })
    );
}

// The one engine, loaded on first use. Imported rather than required because
// the package is an ES module.
function engine() {
    if (enginePromise === null) {
        enginePromise = createEngine().catch((error) => {
            enginePromise = null;
            throw new Error(
                "no Guchho engine found: the browser build needs the pure-WASM " +
                    `engine. Install it with: npm install @guchho/wasm (${error.message})`
            );
        });
    }
    return enginePromise;
}

// The service shape lib/build.js and friends expect: request for the raw
// response, call for the unwrapped one, and a pluginHost slot that
// callWithPlugins fills in — which is exactly what makes a plugin request fail.
const wasmService = {
    pluginHost: null,

    async request(payload) {
        return (await engine()).requestRaw(payload);
    },

    async call(payload) {
        const response = await (await engine()).requestRaw(payload);
        return unwrap(response);
    },
};

function getService() {
    return Promise.resolve(wasmService);
}

async function compilerRequest(payload) {
    return (await engine()).requestRaw(payload);
}

// The browser has no project directory to walk for a config file, so the
// config's plugins are left to the engine to ignore. The caller's own plugins
// are returned, so a build that named one is refused by the engine with its
// own wording rather than silently dropping it.
async function withConfigPlugins(_startDir, callerPlugins) {
    if (callerPlugins === null || callerPlugins === undefined) return null;
    const list = Array.isArray(callerPlugins) ? callerPlugins : [callerPlugins];
    return list.length === 0 ? null : list;
}

// The in-process engine frees itself with the page; there is no process to end
// and nothing to await. Kept so the public stop() has the same shape on both
// transports.
async function stopService() {}

/**
 * Sets the files the engine is created with and the diagnostic sinks. Call it
 * before the first build, lex, parse or print: the engine is created once, and
 * a browser build reads its entry points out of the in-memory tree it was
 * created with.
 *
 * @param {object} [options]
 * @param {Record<string, string|Uint8Array>} [options.files] Virtual paths.
 * @param {(line: string) => void} [options.print]
 * @param {(line: string) => void} [options.printErr]
 */
function configure(options = {}) {
    if (enginePromise !== null) {
        throw new Error(
            "the Guchho engine has already been created; configure() must be " +
                "called before the first build, lex, parse or print"
        );
    }
    if (options.files !== undefined) config.files = options.files;
    if (options.print !== undefined) config.print = options.print;
    if (options.printErr !== undefined) config.printErr = options.printErr;
}

/**
 * Writes files into the engine's in-memory tree after creation. For the
 * browser the engine is created with a files map instead; this is the path for
 * adding more before a later build.
 *
 * @param {Record<string, string|Uint8Array>} files
 */
async function loadFiles(files) {
    const loaded = await engine();
    loaded.loadFiles(files);
}

setTransport({ getService, compilerRequest, withConfigPlugins, stopService });

module.exports = {
    configure,
    loadFiles,
    getService,
    compilerRequest,
    withConfigPlugins,
    stopService,
};
