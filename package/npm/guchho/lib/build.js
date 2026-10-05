"use strict";

// build(options) — a project in, files out.
//
// One request, one answer, no state kept afterwards. When a caller is going to
// build more than once, context() is the thing to reach for instead, because
// this throws away everything the engine learned.
//
// The options may be written either way — flat, as the API has always published
// them, or nested under "build" as a config file nests them — and lib/options.js
// is what makes the two mean the same thing. Nothing here reads an option by
// name; everything goes through that one module first, which is what stops the
// two spellings from drifting apart.

const { getService } = require("./service");
const { toFlags, toEntryPoints } = require("./flags");
const { toBuildResult, toText } = require("./convert");
const { answerFields } = require("./context");
const { normalize } = require("./options");

/**
 * Runs a build.
 *
 * An array of configurations builds each of them and answers with a result per
 * configuration, in the order given. The array is explicit — a caller who wrote
 * three configurations wants three builds — so this does not go looking for more
 * anywhere. A config file whose own root is an array is a different thing and
 * keeps behaving as it always has.
 *
 * @param {object|object[]} [options] The options "guchho build" takes, in the
 *   spellings guchho uses, flat or nested under "build". Anything not given is
 *   left to guchho.config.js next to the project, or to the built-in default, so
 *   this module holds no second copy of any option.
 * @param {string|string[]|Array<[string,string]>} [options.entrypoints] Entry
 *   points. Also spelled "entryPoints" or "entry".
 * @param {string[]} [options.external] Packages to leave as imports.
 * @param {Record<string,string>} [options.define] Identifiers to replace.
 * @param {Record<string,string>} [options.loader] Extensions to a loader name.
 * @param {string} [options.outdir] Root of the output tree.
 * @param {string} [options.outfile] A single output file. Never derived from outdir.
 * @param {string} [options.format] "esm", "cjs", "iife", "umd", "amd", "system".
 * @param {string} [options.platform] "browser", "node", "neutral".
 * @param {string|string[]} [options.target] A language level, or the engines.
 * @param {boolean|string} [options.sourcemap] true for the default mode, or
 *   "none", "inline", "external", "linked", "both".
 * @param {boolean} [options.write=true] False to collect outputs in memory
 *   instead of writing them.
 * @param {boolean} [options.metafile] True to get a metafile back.
 * @param {string} [options.absWorkingDir] Where relative paths resolve from.
 * @returns {Promise<object|object[]>} One BuildResult, or one per configuration.
 * @throws {Error} When the build failed. The error carries "errors" and
 *   "warnings" as the result would have.
 */
async function build(options = {}) {
  // An array is not a configuration, so it is taken before the normalization
  // layer sees it. Each element is normalized on its own, because each is a
  // whole configuration in its own right and one of them being wrong should
  // name that one.
  if (Array.isArray(options)) {
    if (options.length === 0) {
      throw new TypeError("build takes a configuration, or a non-empty array of them");
    }
    return Promise.all(options.map(build));
  }

  const { build: normalized } = normalize(options);

  const service = await getService();

  // A build with no entry points is refused here rather than sent on. The engine
  // would fall back to a glob for a default entry, find nothing, and succeed
  // having built nothing — which a caller reads as a build that worked, and
  // which is the single most expensive way this API can be wrong.
  const entries = toEntryPoints(normalized);
  if (entries.length === 0 && normalized.stdin === undefined) {
    throw new TypeError(
      "build needs at least one entry point, or a stdin object. " +
        "A build with neither falls back to a glob, finds nothing, and reports success."
    );
  }

  const request = {
    command: "build",
    flags: toFlags(normalized),
    entries,
    ...answerFields(normalized),
  };

  const started = Date.now();
  const response = await service.call(request);
  response.duration = Date.now() - started;

  return toBuildResult(response);
}

/**
 * Renders a metafile as a text report.
 *
 * The old "analyze", kept under its own name because analyze() now means
 * analysing a build configuration rather than reading a metafile. See
 * lib/analyze.js.
 *
 * @param {object|string} metafile
 * @returns {Promise<string>}
 */
async function analyzeMetafile(metafile) {
  const service = await getService();

  // A string is passed through and an object is serialized, because a caller
  // holding a metafile almost always has the object from a build and a caller
  // holding the text has read it off disk. Both are one line to accept and
  // neither is one the caller should have to convert by hand.
  const text = typeof metafile === "string" ? metafile : JSON.stringify(metafile);
  if (text === undefined) {
    throw new TypeError("analyzeMetafile takes a metafile object or its JSON text");
  }

  const response = await service.call({ command: "analyze-metafile", metafile: text });
  return toText(response.text);
}

module.exports = { build, analyzeMetafile };