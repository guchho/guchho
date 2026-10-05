"use strict";

// analyze(config) — what went into a build, and what came out.
//
// This takes the same configuration build() does and answers a question about
// it rather than producing anything: which files were read, which packages were
// left as imports, which outputs were produced, and how big they are.
//
// The numbers come from the metafile the engine already writes for a build, so
// this is the bundler's own dependency graph rather than a second opinion from a
// scanner that walks the tree and guesses. A separate scanner disagrees with the
// bundler about resolve extensions, tsconfig paths and package exports, and it
// disagrees in the direction of reporting files the build never read — which is
// the direction that makes an analysis worth having.
//
// Nothing is written. The build runs with write:false, so the analysis costs a
// parse and a bundle and leaves the project exactly as it found it. For a build
// that has to happen anyway, build({ metafile: true }).inputs is the same answer
// and costs nothing extra.

const { build } = require("./build");
const {
    isRecord,
    toInputSummaries,
    toOutputSummaries,
    toDependencySummaries,
} = require("./convert");
const { normalize } = require("./options");

/**
 * Analyses a build configuration without producing anything.
 *
 * @param {object} [config] A configuration for build(), flat or nested.
 * @returns {Promise<{inputs: object[], outputs: object[], dependencies: string[],
 *   totalBytes: number}>}
 *   inputs: every file read, with its size and the inputs it pulled in.
 *   outputs: every file produced, with its size.
 *   dependencies: the packages left as imports rather than bundled.
 *   totalBytes: the size of the output tree.
 * @throws {Error} When the configuration does not build. An analysis of a build
 *   that failed is an analysis of nothing, so the failure is reported as the
 *   failure it is rather than as an empty result.
 */
async function analyze(config = {}) {
  const { build: normalized } = normalize(config);

  // write:false so the bundle happens in memory and the project is untouched,
  // and metafile:true because that is where the graph comes from. Both are
  // forced rather than honoured: a caller who passed write:true wants outputs,
  // and a caller who passed metafile:false wants to not pay for this.
  const result = await build(
    Object.assign({}, normalized, { write: false, metafile: true })
  );

  return summarize(result.metafile);
}

/**
 * Turns a metafile into the shape analyze() publishes.
 *
 * Exported because a caller who ran the build themselves and has the metafile
 * should be able to get the same summary without running a second build.
 *
 * @param {object|string} metafile A parsed metafile, or its JSON text.
 * @returns {{inputs: object[], outputs: object[], dependencies: string[], totalBytes: number}}
 */
function summarize(metafile) {
  const parsed = typeof metafile === "string" ? safeParse(metafile) : metafile;

  // An absent or unreadable metafile is reported as an empty analysis rather
  // than a throw. The build that produced it succeeded, and a caller asking what
  // it contained deserves "nothing", not an exception about a document they may
  // never have asked for.
  if (!isRecord(parsed)) {
    return { inputs: [], outputs: [], dependencies: [], totalBytes: 0 };
  }

  const outputs = toOutputSummaries(parsed);

  return {
    // The shapes are read from the metafile by lib/convert.js, which is also
    // where build() reads them from. One place that knows how a metafile is laid
    // out, so a change to the engine's format lands in one file rather than in
    // two that disagree about it.
    inputs: toInputSummaries(parsed),
    outputs,
    dependencies: toDependencySummaries(parsed),
    totalBytes: outputs.reduce((total, output) => total + output.size, 0),
  };
}

/** Parses JSON, answering undefined rather than throwing on rubbish. */
function safeParse(text) {
  try {
    return JSON.parse(text);
  } catch {
    return undefined;
  }
}

module.exports = { analyze, summarize };
