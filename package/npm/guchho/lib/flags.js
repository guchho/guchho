"use strict";

// One place where an option name becomes a command-line flag.
//
// Every value here is optional, and an option that is not given produces no
// flag at all rather than a flag spelling out the default. That matters: the
// built-in defaults are the engine's business, and a flag would make the two
// disagree the moment either changed. "outdir" not being set must leave the
// engine's "dist" in charge, not pin it to a second copy of the same number
// living in this file.
//
// The flag names are the ones "guchho build --help" prints. Anything it does
// not list is rejected by the command line with "Invalid build flag", so a
// name added here without a flag on the other side fails loudly and at once,
// which is better than a silently ignored option.

const path = require("path");

// Boolean options, sent bare: "guchho build --sourcemap" rather than
// "--sourcemap=true". A value of false sends nothing, because there is no
// "turn this back off" spelling: a flag is a request, and the way to not make
// it is to not make it.
const BOOLEAN_FLAGS = {
    minify: "--minify",
    minifyHtml: "--minify-html",
    pretty: "--pretty",
    sourcemap: "--sourcemap",
    splitting: "--splitting",
    treeShaking: "--tree-shaking",
    bundle: "--bundle",
    watch: "--watch",
    metafile: null, // takes a path; handled by the caller
};

// Options whose value goes after an "=". The value is stringified, so a number
// is accepted and a boolean is not: there is no flag that means "--minify=1",
// and a boolean here is a caller who expected a different shape of option.
const VALUE_FLAGS = {
    outdir: "--outdir",
    outfile: "--outfile",
    target: "--target",
    format: "--format",
    platform: "--platform",
    jsx: "--jsx",
    tsconfig: "--tsconfig",
    logLevel: "--log-level",
};

// Options that may be given more than once, each producing its own flag.
//
// The separator is a colon and not an equals sign, because a colon is what the
// command line parses for these three: "--external:react" is a flag and
// "--external=react" is not — the binary turns the second away as an unknown
// flag, which is a build that fails outright rather than one that does the wrong
// thing. src/cli/cli_flags.cpp keeps the same table, and
// test/flag-contract.test.js is what holds the two to it.
//
// "pairs" says whether the option's value is a name on its own or a name and
// the thing it becomes, joined by "=". It decides two things: how an object is
// expanded, and what a caller is told they passed when they passed a name
// without a value.
const REPEATED_FLAGS = {
    entry: null, // positional; handled by the caller
    external: { flag: "--external", pairs: false },
    define: { flag: "--define", pairs: true },
    loader: { flag: "--loader", pairs: true },
};

// Turns one repeated option's value into the flags it stands for, in the order
// the value gave them.
//
// Three shapes are accepted, because all three read naturally and two of them
// are what a config file holds: a string, an object, or an array of either —
// where an array is how a caller says "each of these separately" and a single
// one of them says the same thing.
//
// An object expands to one flag per entry, the key and the value joined by "="
// because the value can be one itself. A string is used as it stands, so
// "DEBUG=true" and "react" both arrive without anything added: the string is
// already the rest of the flag, and the binary has a better error for one that
// is not a pair than anything that could be said here.
function repeatedArgs(spec, value) {
    const list = Array.isArray(value) ? value : [value];
    const args = [];

    for (const item of list) {
        if (typeof item === "string" && item.length > 0) {
            args.push(`${spec.flag}:${item}`);
            continue;
        }

        if (item !== null && typeof item === "object" && !Array.isArray(item)) {
            if (!spec.pairs) {
                throw new TypeError(`${spec.flag} takes a name, not a name and a value`);
            }
            for (const [name, replacement] of Object.entries(item)) {
                if (name.length === 0) {
                    throw new TypeError(`${spec.flag} needs a name on the left of the =`);
                }
                args.push(`${spec.flag}:${name}=${replacement}`);
            }
            continue;
        }

        throw new TypeError(
            `${spec.flag} takes ${spec.pairs ? "a name, a name=value string, or a record" : "a name"}` +
            `, or an array of those`
        );
    }

    return args;
}

// The log levels the command line accepts. Checked here rather than left to the
// binary so that a typo is a rejected promise instead of an exit code the
// caller has to know the meaning of.
const LOG_LEVELS = new Set([
    "verbose",
    "debug",
    "info",
    "warning",
    "error",
    "silent",
]);

// Turns an options object into the argument list a command takes, entries
// first because the command line reads them positionally.
//
// Returns an array of strings, ready to hand to spawn. Throws a TypeError
// rather than letting a bad value reach the binary as "undefined", because the
// binary's error for that is a parse failure several frames from the line that
// caused it.
function toArgs(options, { entries = true, metafile = null } = {}) {
    const args = [];

    // Entry points are positional: the usage is "guchho build [entry...]" and
    // there is no --entry flag for them to arrive as. They go first because
    // that is where the command line reads them, and it is also what makes
    // them work — a build handed nothing positional falls back to a glob for a
    // default entry, finds nothing, and exits successfully having built
    // nothing, which a caller reads as a build that worked.
    if (entries) {
        // "entry" as well as "entries", because the singular is what a single
        // value reads as naturally and both spellings are in circulation. They
        // are the same option; entries is the one the usage line uses.
        const given = options.entries !== undefined ? options.entries : options.entry;

        if (given !== undefined && given !== null) {
            const list = Array.isArray(given) ? given : [given];
            for (const e of list) {
                if (typeof e !== "string" || e.length === 0) {
                    throw new TypeError("entries must be non-empty strings or an array of them");
                }
                args.push(e);
            }
        }
    }

    for (const [name, flag] of Object.entries(BOOLEAN_FLAGS)) {
        if (flag && options[name] === true) {
            args.push(flag);
        }
    }

    for (const [name, flag] of Object.entries(VALUE_FLAGS)) {
        if (options[name] === undefined || options[name] === null) {
            continue;
        }
        if (typeof options[name] === "boolean") {
            throw new TypeError(`${name} takes a value, not true or false`);
        }
        args.push(`${flag}=${options[name]}`);
    }

    for (const [name, spec] of Object.entries(REPEATED_FLAGS)) {
        if (options[name] === undefined || options[name] === null || !spec) {
            continue;
        }
        args.push(...repeatedArgs(spec, options[name]));
    }

    if (metafile !== null) {
        args.push(`--metafile=${metafile}`);
    }

    if (options.logLevel !== undefined && !LOG_LEVELS.has(options.logLevel)) {
        throw new TypeError(
            `logLevel must be one of ${[...LOG_LEVELS].join(", ")}; got ${options.logLevel}`
        );
    }

    return args;
}

// The path a metafile is written to when the caller did not name one. Kept out
// of the project directory on purpose: it is a receipt for one run, not a
// build output, and a build that has to clean up after itself should not be the
// reason a file shows up beside the sources.
function defaultMetafilePath(tag) {
    return path.join(require("os").tmpdir(), `guchho-${tag}-${process.pid}.json`);
}

module.exports = {
    toArgs,
    defaultMetafilePath,
    LOG_LEVELS,
    // The tables are exported so that the test holding them to the engine's
    // grammar can read them rather than write out its own copy, which would be
    // a second list to fall behind. They are not part of the package's public
    // API: lib/index.js re-exports none of them, and nothing a caller is
    // offered reaches them.
    BOOLEAN_FLAGS,
    VALUE_FLAGS,
    REPEATED_FLAGS,
};
