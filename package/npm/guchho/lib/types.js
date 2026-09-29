"use strict";

// The shapes this package publishes, in one place.
//
// This file holds no behaviour and no defaults. It exists so that the JSDoc in
// the modules that build these values, the declarations in index.d.ts, and the
// tests that assert on them all name the same things — three spellings of a
// result is how a rename becomes a bug report.
//
// A JSDoc typedef rather than a runtime object because the package has no build
// step and no dependencies, and because a typedef costs nothing at require time
// while a schema validator would be a dependency and a second thing to keep in
// step with the service.
//
// The one thing that *is* runtime is at the bottom: the diagnostics contract.
// The service reports a failure two ways — a diagnostic in "errors", or a
// refusal in "error" — and only the second has ever needed a fallback shape,
// because it is the one that arrives when the engine would not even begin.

/**
 * Where a diagnostic points, in a file or in a namespace.
 *
 * @typedef {object} Location
 * @property {string} file
 * @property {string} namespace
 * @property {number} line 1-based
 * @property {number} column 0-based, in bytes
 * @property {number} length in bytes
 * @property {string} lineText
 * @property {string} suggestion
 */

/**
 * A secondary message attached to a primary one.
 *
 * @typedef {object} Note
 * @property {string} text
 * @property {Location|null} location
 */

/**
 * One diagnostic.
 *
 * "detail" is whatever a plugin put there, passed through untouched. It is
 * undefined on every message this package produces itself, because nothing here
 * sets it.
 *
 * @typedef {object} Message
 * @property {string} id
 * @property {string} pluginName
 * @property {string} text
 * @property {Location|null} location
 * @property {Note[]} notes
 * @property {any} [detail]
 */

/**
 * A file a build produced.
 *
 * @typedef {object} OutputFile
 * @property {string} path
 * @property {Uint8Array} contents
 * @property {string} hash
 * @property {string} text "contents" as text. Decoded on each read.
 */

/**
 * What the build read and wrote, and what it cost.
 *
 * @typedef {object} Metafile
 * @property {Record<string, {bytes: number, imports: {path: string, kind: string, external?: boolean}[], format?: string}>} inputs
 * @property {Record<string, {bytes: number, inputs: {path: string, bytesInOutput: number}[], imports: {path: string, kind: string, external?: boolean}[], exports: string[], entryPoint?: string}>} outputs
 */

/**
 * One lexical token, as the engine saw it.
 *
 * The four stages are deliberately the same across the three languages, so that
 * a caller holding tokens from lexCSS and tokens from lexJS can treat them the
 * same way without branching on which language produced them. "kind" is the
 * engine's own name for the token type, so a name this package has never heard
 * of arrives as itself rather than as a number.
 *
 * @typedef {object} Token
 * @property {string} kind
 * @property {string} value
 * @property {number} start 0-based byte offset
 * @property {number} end 0-based byte offset, exclusive
 * @property {number} line 1-based
 * @property {number} column 0-based, in bytes
 * @property {number} length in bytes
 */

/**
 * A parsed tree, held by the engine and referred to by id.
 *
 * The tree itself does not cross the wire. It is a graph of engine-owned nodes —
 * pointers, interned symbols, cycles between scopes — and reproducing it in
 * JavaScript would be a second implementation of the parser's output, which is
 * exactly the duplication this API exists to avoid. So "ast" is a handle: an id
 * the engine remembers, plus a structural summary readable without the engine.
 *
 * The summary is what makes the handle useful rather than opaque. It is a real
 * description of the tree's shape — HTML node types and tag names, CSS rule and
 * selector counts, JavaScript part and symbol counts — produced by walking the
 * engine's own AST. It is not a lossless serialization, and it is not a
 * substitute for one; it is the answer to "what did the parser make of this",
 * which is the question a caller holding an AST handle actually has.
 *
 * A handle is valid until the service process ends, or until the caller
 * releases it. It is not transferable between processes and not comparable
 * between calls to context() or to different service instances.
 *
 * @typedef {object} AST
 * @property {number} id
 * @property {string} language "html", "css" or "js"
 * @property {object} summary
 */

// -----------------------------------------------------------------------------
// Diagnostics contract
// -----------------------------------------------------------------------------

// The diagnostic the service sends when it could not begin the work at all.
//
// Every answer carries "errors", so a host has somewhere to read diagnostics
// from as a matter of course. This one is the exception that made the shape
// necessary: a request the engine refuses before it has a source to blame —
// a command it does not know, a field of the wrong type — has no location and no
// file, and giving it one would mean inventing a path that does not exist.
//
// Built here rather than in convert.js so that the service layer and the build
// layer agree on what a refusal looks like without either importing the other.
const PROTOCOL_ERROR_ID = "protocol-error";

/**
 * A refusal, in the shape a host reads when the service would not begin.
 *
 * @param {string} text
 * @returns {Message}
 */
function protocolError(text) {
    return {
        id: PROTOCOL_ERROR_ID,
        pluginName: "",
        text,
        location: null,
        notes: [],
        detail: undefined,
    };
}

module.exports = { PROTOCOL_ERROR_ID, protocolError };
