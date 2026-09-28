"use strict";

// Running the binary and reading both streams back.
//
// The command line already does the work: it resolves the config file, applies
// the defaults and reports its own diagnostics. Re-implementing any of that in
// JavaScript would be a second answer to the same question, and the two would
// drift. So this module only knows how to start a process, give it a standard
// input, and collect what it wrote.

const child_process = require("child_process");

const { getBinaryPath } = require("./main");

// The two shapes a diagnostic takes on the error stream. The logger prefixes
// each one with the severity in brackets, which is the only machine-readable
// part of the line; the rest is prose meant for a person and is left intact
// rather than parsed apart.
const ERROR_PREFIX = /^\s*X\s*\[ERROR\]/;
const WARNING_PREFIX = /^\s*X\s*\[WARN(?:ING)?\]/;

// The engine's own running commentary, which is not a diagnostic about
// anything. Two shapes appear at the end of a run:
//
//   transform finished in 12ms done      the timer report
//   1 error                              the count of what it just said
//
// The first is there precisely because a transform is a filter and its standard
// output belongs to the program. The second is the logger summarising itself.
// Neither is something a caller can act on, and both arrive after the last real
// diagnostic, so without a rule for them they would be appended to whichever
// error happened to be last and read as part of a syntax error's snippet.
//
// They are recognised by shape rather than by exact wording, so that a timer
// added to another command does not make this quietly stop matching.
const REPORT_LINE = /^\s*\S+ finished in \d+ms\b/;
const SUMMARY_LINE = /^\s*(?:\d+\s+(?:error|warning)s?\b|\d+\s+warnings?\b.*\berrors?\b)/i;

// Splits a captured error stream into errors and warnings, one entry per
// diagnostic, keeping the text as it was written.
//
// A line that is neither is dropped unless it is the continuation of the
// diagnostic above it, because a multi-line diagnostic (a snippet with the line
// it is about) belongs to the one it was printed under rather than becoming an
// entry of its own. Text that follows no diagnostic at all is kept as an error,
// since something the engine said and nothing classified is closer to a failure
// than to silence — except for the engine's own report and summary lines, which
// are said on every run whether or not anything went wrong.
function splitDiagnostics(text) {
    const errors = [];
    const warnings = [];

    for (const line of text.split(/\r?\n/)) {
        if (line.trim().length === 0) {
            continue;
        }

        if (REPORT_LINE.test(line) || SUMMARY_LINE.test(line)) {
            continue;
        }

        if (ERROR_PREFIX.test(line)) {
            errors.push(line);
        } else if (WARNING_PREFIX.test(line)) {
            warnings.push(line);
        } else if (errors.length > 0) {
            errors[errors.length - 1] += `\n${line}`;
        } else if (warnings.length > 0) {
            warnings[warnings.length - 1] += `\n${line}`;
        } else {
            errors.push(line);
        }
    }

    return { errors, warnings };
}

// Runs the binary and resolves once it has exited. Never rejects on a non-zero
// exit: a failed build is a result with errors in it, not an exception, and a
// caller that has to try/catch to find out whether its code compiled is a
// caller that will forget to.
//
// Rejects only when the binary could not be started at all, which is a broken
// install rather than a bad input, and is worth stopping for.
function run(args, { input = null, cwd = undefined } = {}) {
    const binaryPath = getBinaryPath();

    return new Promise((resolve, reject) => {
        const child = child_process.spawn(binaryPath, args, {
            cwd,
            stdio: ["pipe", "pipe", "pipe"],
        });

        let stdout = "";
        let stderr = "";

        child.stdout.setEncoding("utf8");
        child.stderr.setEncoding("utf8");
        child.stdout.on("data", (chunk) => {
            stdout += chunk;
        });
        child.stderr.on("data", (chunk) => {
            stderr += chunk;
        });

        child.on("error", reject);

        child.on("close", (exitCode) => {
            const { errors, warnings } = splitDiagnostics(stderr);
            resolve({ exitCode, stdout, stderr, errors, warnings });
        });

        if (input !== null) {
            child.stdin.on("error", () => {
                // The child may have exited before the input was written — a
                // syntax error makes it stop reading immediately. The exit code
                // is the answer; a broken pipe here is not a second one.
            });
            child.stdin.end(input);
        } else {
            child.stdin.end();
        }
    });
}

module.exports = { run, splitDiagnostics };
