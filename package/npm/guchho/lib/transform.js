"use strict";

// transform(code, options) — one file in, one file out.
//
// The same shape as build() and the same refusal: a transform that failed throws,
// and the error carries the diagnostics. The one thing that is different is the
// input, which arrives as a string in the call rather than as an entry point
// named on disk, so there is nothing to resolve and no file to read.

const { getService } = require("./service");
const { toFlags } = require("./flags");
const { toTransformResult } = require("./convert");
const { normalize } = require("./options");

/**
 * Transforms a single piece of source.
 *
 * @param {string|Buffer|Uint8Array} code The source. A buffer is accepted
 *   because that is what a caller reading a file off disk already has, and
 *   making it decode to a string first would be a copy they did not ask for.
 * @param {object} [options]
 * @param {string} [options.loader] The loader name: "js", "ts", "jsx", "css",
 *   "json", "text", and so on. This is the transform's own loader, which is a
 *   single name, where a build's is a map of extension to name.
 * @param {string} [options.sourcefile] The name to blame in diagnostics. Worth
 *   setting: without it every message says "<stdin>", which is true and useless.
 * @param {string} [options.format] "iife", "cjs", "esm", "umd", "amd", "system".
 * @param {string} [options.target] "es2020", "chrome80", "node16".
 * @param {boolean} [options.minify]
 * @param {boolean} [options.sourcemap] Adds a "map" to the result.
 * @returns {Promise<{code: string, map: string, errors: object[], warnings: object[]}>}
 * @throws {Error} When the transform failed.
 */
async function transform(code, options = {}) {
  if (code === undefined || code === null) {
    throw new TypeError("transform needs some code to transform");
  }

  // A transform takes the same options a build does, so it reads them through the
  // same layer. There is no "build" section here for a caller to nest into — a
  // transform has no entry points and no outdir — but a caller who writes one
  // anyway gets it rather than a silent no-op.
  const { build: normalized } = normalize(options);

  const service = await getService();

  let input;
  if (typeof code === "string") {
    input = code;
  } else if (Buffer.isBuffer(code) || code instanceof Uint8Array) {
    // Bytes, not a string. The service accepts either, and sending the bytes a
    // caller already has means a file with a byte-order mark or Latin-1 content
    // is transformed as it is rather than as whatever a decode guessed it was.
    input = code;
  } else {
    throw new TypeError("transform takes a string, a Buffer, or a Uint8Array");
  }

  const request = {
    command: "transform",
    // "loader" is deliberately not among the flags. A build's loader is a map of
    // extension to name and reads as "--loader:.ts=ts", but a transform has no
    // extensions to speak of — it has exactly one input and one name for it, so
    // the service takes it as a field and turns it into the grammar's single-name
    // "--loader=ts". Letting it through toFlags as well would send both, and the
    // grammar would turn away the one that belongs to a build.
    flags: toFlags(withoutLoader(normalized)),
    sourcefile: normalized.sourcefile,
    loader: normalized.loader,
    input,
  };

  const response = await service.call(request);
  return toTransformResult(response);
}

// The options minus the one that is a field here rather than a flag.
function withoutLoader(options) {
  const copy = Object.assign({}, options);
  delete copy.loader;
  return copy;
}

module.exports = { transform };
