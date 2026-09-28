"use strict";

// build(options) — entry points in, files on disk.
//
// The work is done by "guchho build", with one addition: the run is always
// asked for a metafile, so the answer is the set of files it actually produced
// rather than a log line a caller would have to scrape. The metafile is JSON
// describing every input and every output, which is the closest thing the
// command line has to a structured result.

const fsp = require("fs/promises");
const path = require("path");

const { toArgs, defaultMetafilePath } = require("./flags");
const { run } = require("./exec");

// Where a build writes when nothing says otherwise. Named once because it is
// the engine's default, not this module's, and "dist" appearing in two places
// in this file would be a second answer to the same question.
const DEFAULT_OUTDIR = "dist";

// Reads a metafile the run has already finished with. A metafile that is not
// there, or that is not JSON, is not a failed build — the build succeeded and
// its receipt is simply unreadable, which is a weaker and different thing to
// say. The output list is empty in that case, so "outputs" is empty rather
// than wrong.
async function readMetafile(file) {
    let text;
    try {
        text = await fsp.readFile(file, "utf8");
    } catch {
        return null;
    }

    try {
        const parsed = JSON.parse(text);
        return parsed && typeof parsed === "object" ? parsed : null;
    } catch {
        return null;
    }
}

/**
 * Runs a production build.
 *
 * @param {object} [options] The same options "guchho build" takes, plus
 *   "root": the directory to run in, which is where the config file is
 *   discovered from and where a relative entry path is resolved against. An
 *   option that is not given is left to the config file, or to the built-in
 *   default, so this module holds no second copy of any of them.
 * @returns {Promise<object>} The files the build produced, plus whatever
 *   diagnostics the run produced. A failed build is a resolved promise with a
 *   non-zero exitCode, not a rejection.
 */
async function build(options = {}) {
    const root = options.root ?? process.cwd();

    if (typeof root !== "string" || root.length === 0) {
        throw new TypeError("root must be a non-empty string");
    }

    // The metafile is a receipt for this one run, so it is written somewhere
    // temporary and removed afterwards. Leaving it in the project would make a
    // build look like it had produced a file it did not.
    const metafile = defaultMetafilePath("metafile");

    const args = toArgs(options, { entries: true, metafile });

    // "build" is the command, and it has to be in the argument list: run without
    // it the binary has nothing to do, prints its help and exits successfully,
    // and a build that produced no files and reported no errors would be
    // indistinguishable from one that worked.
    args.unshift("build");

    let result;
    try {
        result = await run(args, { cwd: root });

        // Read before cleaning up. A build writes the metafile as its last act,
        // so reading it afterwards would always find nothing there.
        return await summarize(root, options.outdir, metafile, result);
    } finally {
        // Cleaned up whether the run succeeded or failed: a metafile left in
        // the project would make a build look like it had produced a file it
        // did not.
        fsp.unlink(metafile).catch(() => {});
    }
}

// Turns a finished run into the answer, reading the receipt the run left behind.
async function summarize(root, outdir, metafile, result) {
    const parsed = result.exitCode === 0 ? await readMetafile(metafile) : null;

    return {
        // Every output the build wrote, keyed by its path relative to the
        // directory the build ran in. Each value carries the size, the entry
        // point it came from and the inputs that went into it. Empty when the
        // build failed, or when it succeeded without a readable metafile.
        outputs: parsed && parsed.outputs ? parsed.outputs : {},
        inputs: parsed && parsed.inputs ? parsed.inputs : {},
        // The files on disk, as absolute paths. Resolved here rather than left
        // to the caller, because the metafile names them the way the build saw
        // them and a caller holding only that would have to know the rule.
        files: parsed && parsed.outputs
            ? Object.keys(parsed.outputs).map((p) => path.resolve(root, p))
            : [],
        errors: result.errors,
        warnings: result.warnings,
        exitCode: result.exitCode,
        outdir: path.resolve(root, outdir ?? DEFAULT_OUTDIR),
    };
}

// A metafile that cannot be written, because the temporary directory is not
// there, leaves the build itself intact: the run still happens and still
// produces its files. What is lost is the receipt, which is what an empty
// "outputs" says.
module.exports = { build };
