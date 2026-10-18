"use strict";

// Which engine answers a compiler-stage request: the native service, or the
// WebAssembly engine in this process.
//
// The compiler API — lexer, parse and print — is one request to the same
// service a build uses. On a machine with the native binary that request goes
// over the pipe, exactly as it always has. On a machine that installed only
// @guchho/wasm there is no pipe to send it over, and this module is what makes
// the same three functions work anyway by running the engine's service dispatch
// in process.
//
// The order is native first, then WebAssembly, and it is deliberately not the
// WebAssembly-first order the command-line wrapper resolves by. A compiler-stage
// request carries no plugins and reaches the same lexer either way, so there is
// nothing to gain from the slower engine when a faster one is installed; the
// fallback exists for the machine that has no binary, not to redirect a machine
// that has one. GUCHHO_BACKEND still forces either choice, so a caller who
// wants the WebAssembly engine despite a native binary still gets it.
//
// The WebAssembly engine is a singleton, like the native service: parse answers
// with a handle into a tree the engine keeps, and a second engine would hold a
// second set of trees. One instance, reused for every request, is what keeps a
// handle valid across a parse and the print that follows it.

const { getService } = require("./service");
const { getBinaryPath } = require("./main");

// The decision, made once. A native binary does not appear or disappear while a
// process runs, and re-deciding on every request would be a getBinaryPath() walk
// per lex.
let useWasmCache = null;

// Whether to answer with the WebAssembly engine.
function useWasm() {
  if (useWasmCache !== null) return useWasmCache;

  const backend = process.env.GUCHHO_BACKEND;

  if (backend === "wasm") return (useWasmCache = true);
  if (backend === "native") return (useWasmCache = false);
  if (backend !== undefined) {
    throw new Error(
      `GUCHHO_BACKEND is set to "${backend}", which is not one of "wasm" or "native".`
    );
  }

  try {
    getBinaryPath();
    return (useWasmCache = false);
  } catch {
    return (useWasmCache = true);
  }
}

// The one WebAssembly engine, loaded on first use. Imported rather than
// required because the package is an ES module.
let wasmEngine = null;

function loadWasmEngine() {
  if (wasmEngine === null) {
    wasmEngine = import("@guchho/wasm")
      .then((module) => module.createGuchho())
      .catch((error) => {
        // The import failed, so the package is not installed. Say so the way the
        // resolver would, rather than letting a bare module-not-found out.
        wasmEngine = null;
        throw new Error(
          "no Guchho engine found: install the native binary, or use the " +
            `pure-WASM engine: npm install @guchho/wasm (${error.message})`
        );
      });
  }
  return wasmEngine;
}

/**
 * Sends one compiler-stage request to whichever engine is available.
 *
 * Resolves with the engine's response object, in the same shape the native
 * service sends. Rejects when the request was refused; that is the caller's
 * `unwrap` to turn into the published failure shape, so it is left alone here.
 *
 * @param {object} payload The service request. Its "command" is a compile
 *   command, an unknown one is refused by the engine.
 * @returns {Promise<object>}
 */
async function compilerRequest(payload) {
  if (!useWasm()) {
    const service = await getService();
    return service.request(payload);
  }

  const engine = await loadWasmEngine();
  return engine.request(payload);
}

module.exports = { compilerRequest, useWasm };
