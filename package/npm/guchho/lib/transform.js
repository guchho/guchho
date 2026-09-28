"use strict";

// transform(code, options) — one piece of source in, one piece of source out.
//
// This is the smallest useful thing Guchho does, and the one most often reached
// for by something that is not a build at all: hand a string to the same parser
// and printer the bundler uses and get the rewritten string back. No entry
// points, no output files, no graph, no watching.
//
// The work is done by "guchho transform", which is a filter — the source goes
// in on standard input and the program comes out on standard output. Wrapping it
// rather than opening a second route into the engine is what keeps the promise
// that a snippet accepted here is also accepted inside a bundle.

const { toArgs } = require("./flags");
const { run } = require("./exec");

// Options that name what the code is rather than how it is printed. "loader"
// decides how a file extension is read, "sourcefile" is the name a diagnostic
// blames when there is no file, and the rest describe the target. They exist on
// the command line because a transform reads the same grammar as a build.
const TRANSFORM_ONLY = {
    sourcefile: "--sourcefile",
};

/**
 * Transforms a string of source code.
 *
 * @param {string} code The source to transform.
 * @param {object} [options] The same options "guchho build" takes, minus the
 *   ones that describe a build rather than a single file. An option that is not
 *   given is left to the engine's default, so this module holds no second copy
 *   of any of them.
 * @returns {Promise<object>} The transformed source, plus whatever diagnostics
 *   the run produced. A syntax error is a resolved promise with a non-zero
 *   exitCode and an empty "code", not a rejection.
 */
async function transform(code, options = {}) {
    if (typeof code !== "string") {
        throw new TypeError("transform() takes a string of source code");
    }

    // A silent transform prints nothing at all — not even the program, which is
    // the only thing this function exists to return. It is a reasonable thing to
    // ask of a build and an impossible thing to ask of a filter, so it is
    // refused here rather than quietly handing back "".
    if (String(options.logLevel).toLowerCase() === "silent") {
        throw new TypeError(
            'transform() cannot run silently: the transformed code is the output. ' +
            'Use logLevel "error" to keep a successful run quiet, or build() to write to a file.'
        );
    }

    const args = toArgs(options, { entries: false });

    // "transform" is the command, and it has to be in the argument list: run
    // without it the binary has nothing to do, prints its help and exits
    // successfully, which reaches the caller as an empty program that compiled.
    // That is the worst possible answer, because nothing about it looks wrong.
    args.unshift("transform");

    for (const [name, flag] of Object.entries(TRANSFORM_ONLY)) {
        if (options[name] !== undefined && options[name] !== null) {
            args.push(`${flag}=${options[name]}`);
        }
    }

    // A transform is a filter, so the bytes on the standard output are the
    // program and nothing else. Anything else printed there would land in the
    // middle of the caller's file, which is why the command line was changed to
    // put its timing line on the error stream; the test that pins that is
    // package/npm/test/api-transform.test.js.
    const result = await run(args, { input: code });

    return {
        // The program, exactly as the engine produced it. "map" and
        // "legalComments" are always null: a transform writes no file, so
        // there is nothing to link a source map to. They are named here so the
        // shape of the answer does not change when a caller switches to build().
        code: result.exitCode === 0 ? result.stdout : "",
        map: null,
        legalComments: null,
        errors: result.errors,
        warnings: result.warnings,
        exitCode: result.exitCode,
    };
}

module.exports = { transform };
