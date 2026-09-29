"use strict";

// build(options) — a project in, files out.
//
// One request, one answer, no state kept afterwards. When a caller is going to
// build more than once, context() is the thing to reach for instead, because
// this throws away everything the engine learned.

const { getService } = require("./service");
const { toFlags, toEntryPoints } = require("./flags");
const { toBuildResult, toMessages, toText } = require("./convert");
const { answerFields } = require("./context");

/**
 * Runs a build.
 *
 * @param {object} [options] The options "guchho build" takes, in the spellings
 *   guchho uses. Anything not given is left to guchho.config.js next to the
 *   project, or to the built-in default, so this module holds no second copy of
 *   any option.
 * @param {string|string[]|Array<[string,string]>} [options.entryPoints]
 * @param {string[]} [options.external] Packages to leave as imports.
 * @param {Record<string,string>} [options.define] Identifiers to replace.
 * @param {Record<string,string>} [options.loader] Extensions to a loader name.
 * @param {boolean} [options.write=true] False to collect outputs in memory
 *   instead of writing them.
 * @param {boolean} [options.metafile] True to get a metafile back.
 * @param {string} [options.absWorkingDir] Where relative paths resolve from.
 * @returns {Promise<{errors: object[], warnings: object[], outputFiles?: object[], metafile?: object, mangleCache?: object}>}
 * @throws {Error} When the build failed. The error carries "errors" and
 *   "warnings" as the result would have.
 */
async function build(options = {}) {
  if (options === null || typeof options !== "object") {
    throw new TypeError("build takes an options object");
  }

  const service = await getService();

  // A build with no entry points is refused here rather than sent on. The engine
  // would fall back to a glob for a default entry, find nothing, and succeed
  // having built nothing — which a caller reads as a build that worked, and
  // which is the single most expensive way this API can be wrong.
  const entries = toEntryPoints(options);
  if (entries.length === 0 && options.stdin === undefined) {
    throw new TypeError(
      "build needs at least one entry point, or a stdin object. " +
        "A build with neither falls back to a glob, finds nothing, and reports success."
    );
  }

  const request = {
    command: "build",
    flags: toFlags(options),
    entries,
    ...answerFields(options),
  };

  const response = await service.call(request);
  return toBuildResult(response);
}

/**
 * Formats diagnostics the way a terminal would show them.
 *
 * @param {object[]} messages
 * @param {{kind?: "error"|"warning", color?: boolean, terminalWidth?: number}} [options]
 * @returns {Promise<string>}
 */
async function formatMessages(messages, options = {}) {
  const service = await getService();

  if (!Array.isArray(messages)) {
    throw new TypeError("formatMessages takes an array of messages");
  }

  // Round-tripped through the published shape on the way out, so that a message
  // a caller got from build() and a message one of their own construction are
  // formatted by the same code and come out the same.
  const response = await service.call({
    command: "format-msgs",
    messages,
    kind: options.kind === "warning" ? "warning" : "error",
    color: options.color === true,
    terminalWidth: options.terminalWidth,
  });

  const logs = Array.isArray(response.logs) ? response.logs : [];
  return logs.map(toText).join("");
}

/**
 * Pretty-prints a metafile.
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

module.exports = { build, formatMessages, analyzeMetafile, toMessages };
