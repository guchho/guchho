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

// =============================================================================
// The service's view of an options object
// =============================================================================
//
// Everything above builds a command line. What follows builds the two fields a
// service request carries instead: a flags array, and entry points as [out, in]
// pairs.
//
// The reason there is a flags array at all is the same reason there is a command
// line: the engine has one parser, and it is the thing that decides what an
// option means and what a misspelled one is called. Sending flags rather than
// inventing a second field-per-option shape means an option is accepted here for
// exactly the reason it is accepted on the command line, and refused with the
// same words. A mapping of option names to option members in this file would be a
// second answer to every question the engine already answers, and the two would
// drift the first time a flag was added.

// Boolean options, sent bare: "--sourcemap" rather than "--sourcemap=true".
// A false sends nothing, because a flag is a request and the way to not make one
// is to not make it — there is no "turn this back off" spelling in this grammar.
//
// Every name here is one the engine's parser accepts. One that it does not is a
// build that fails outright with "Invalid build flag", which is caught by
// test/flag-contract.test.js rather than by a caller discovering it in
// production.
const SERVICE_BOOLEANS = {
minify: "--minify",
    minifyHtml: "--minify-html",
    minifyIdentifiers: "--minify-identifiers",
    minifySyntax: "--minify-syntax",
    minifyWhitespace: "--minify-whitespace",
    pretty: "--pretty",
    splitting: "--splitting",
    treeShaking: "--tree-shaking",
    bundle: "--bundle",
    keepNames: "--keep-names",
    sourcesContent: "--sources-content",
    ignoreAnnotations: "--ignore-annotations",
    preserveSymlinks: "--preserve-symlinks",
    jsxDev: "--jsx-dev",
    jsxSideEffects: "--jsx-side-effects",
    mangleQuoted: "--mangle-quoted",
    allowOverwrite: "--allow-overwrite",
};

// "sourcemap" is not in the table above, and that is the interesting part of it.
//
// It takes either a bare "--sourcemap" or "--sourcemap=<value>", and which one
// is the whole meaning: bare means linked for a build and inline for a
// transform, because a build writes a .map beside the output and a transform has
// no output file to sit beside — its code travels back to the caller in a
// string, so the only map that can travel with it is one embedded in it.
//
// It was in this table before, as a boolean. Which meant that the four documented
// string values — inline, external, linked, both — matched no branch here and
// produced no flag at all: a caller who asked for an inline source map got a
// build with no source map and no complaint, because the option was accepted,
// read, and dropped. It is neither table now: it is a flag that takes either a
// bare form or a value, which is why toFlags() asks sourcemapArgs() about it
// rather than reading a table.
const SERVICE_SOURCEMAP_FLAG = "--sourcemap";

// Options whose value goes after an "=".
//
// A boolean is refused rather than stringified: there is no "--minify=1" in this
// grammar, and a boolean here is a caller who expected a different shape of
// option. Finding that out here names the option, where the engine's refusal
// would name a flag several steps later.
const SERVICE_VALUES = {
    outdir: "--outdir",
    outfile: "--outfile",
    outbase: "--outbase",
    target: "--target",
    format: "--format",
    platform: "--platform",
    jsx: "--jsx",
    tsconfig: "--tsconfig",
    logLevel: "--log-level",
    charset: "--charset",
    banner: "--banner",
    footer: "--footer",
    globalName: "--global-name",
    mangleProps: "--mangle-props",
    reserveProps: "--reserve-props",
    legalComments: "--legal-comments",
    drop: "--drop",
    dropLabels: "--drop-labels",
    sourceRoot: "--source-root",
    publicPath: "--public-path",
    resolveExtensions: "--resolve-extensions",
    mainFields: "--main-fields",
    conditions: "--conditions",
sourcefile: "--sourcefile",
    absPaths: "--abs-paths",
};

// The values "--sourcemap=" takes, checked against the grammar's own list so that
// a typo is a TypeError naming the option rather than an "Invalid value" from
// the binary several steps later.
//
// "none" is here rather than special-cased as "leave it out" because a caller
// reading a config file should be able to write the name of the thing they mean.
// It produces no flag, which is how this grammar asks for no source map: a flag
// is a request, and the way to not make one is to not make it.
const SOURCEMAP_VALUES = new Set(["none", "inline", "external", "linked", "both"]);

// Options that may be given more than once, each producing its own flag.
//
// The separator is a colon and not an equals sign because that is what the
// grammar reads: "--external:react" is a flag and "--external=react" is not, and
// the binary turns the second away as an unknown flag — a build that fails
// outright rather than one that quietly does the wrong thing.
const SERVICE_REPEATED = {
    external: { flag: "--external", pairs: false },
    define: { flag: "--define", pairs: true },
    loader: { flag: "--loader", pairs: true },
    alias: { flag: "--alias", pairs: true },
    logOverride: { flag: "--log-override", pairs: true },
};

// Validated against the grammar's own list, so a typo is a TypeError naming the
// option rather than an "Invalid build flag" the caller has to decode.
const SERVICE_LOG_LEVELS = new Set(["verbose", "debug", "info", "warning", "error", "silent"]);

/**
 * The flags a service request carries, and nothing else.
 *
 * Entry points are not in here: the grammar reads them positionally and the
 * request has a field of its own for them, so they are appended to the argument
 * list by the caller rather than being folded into it.
 *
 * @param {object} options
 * @returns {string[]}
 */
function toFlags(options) {
    const args = [];

for (const [name, flag] of Object.entries(SERVICE_BOOLEANS)) {
        if (options[name] === true) args.push(flag);
    }

    args.push(...sourcemapArgs(options.sourcemap));

    for (const [name, flag] of Object.entries(SERVICE_VALUES)) {
        const value = options[name];
        if (value === undefined || value === null) continue;
        if (typeof value === "boolean") {
            throw new TypeError(`${name} takes a value, not true or false`);
        }
        args.push(`${flag}=${value}`);
    }

    for (const [name, spec] of Object.entries(SERVICE_REPEATED)) {
        const value = options[name];
        if (value === undefined || value === null) continue;
        args.push(...repeatedArgs(spec, value));
    }

    if (options.logLevel !== undefined && !SERVICE_LOG_LEVELS.has(options.logLevel)) {
        throw new TypeError(
            `logLevel must be one of ${[...SERVICE_LOG_LEVELS].join(", ")}; got ${options.logLevel}`
        );
    }

    return args;
}

// The flags "sourcemap" turns into, which is zero or one.
//
// Written out rather than folded into either table above because it is the one
// option with both a bare form and a valued one, and putting it in the boolean
// table is what lost the valued form: a string is not `true`, so the four
// documented values produced no flag and no error.
function sourcemapArgs(value) {
    if (value === undefined || value === null || value === false) return [];
    if (value === true) return [SERVICE_SOURCEMAP_FLAG];

    if (typeof value !== "string") {
        throw new TypeError(
            `sourcemap takes true, false, or one of ${[...SOURCEMAP_VALUES].join(", ")}; ` +
            `got ${typeof value}`
        );
    }

    // "none" is the absence of a request, and it arrives as the word rather than
    // as an absent option so that a config file can name it.
    if (value === "none") return [];

    if (!SOURCEMAP_VALUES.has(value)) {
        throw new TypeError(
            `sourcemap must be one of ${[...SOURCEMAP_VALUES].join(", ")}, or true; ` +
            `got "${value}"`
        );
    }

    return [`${SERVICE_SOURCEMAP_FLAG}=${value}`];
}

/**
 * The entry points of a request, as [out, in] pairs.
 *
 * A pair rather than an object because the protocol's object form is a list of
 * string keys, which says "out -> in" well and "in, with an optional out" badly.
 * An entry with no output name is [null, in].
 *
 * @param {object} options
 * @returns {Array<[string|null, string]>}
 */
function toEntryPoints(options) {
    const given = options.entryPoints !== undefined ? options.entryPoints : options.entries;
    if (given === undefined || given === null) return [];

    const list = Array.isArray(given) ? given : [given];
    const pairs = [];

    for (const entry of list) {
        if (typeof entry === "string") {
            if (entry.length === 0) throw new TypeError("entryPoints must be non-empty strings or [out, in] pairs");
            pairs.push([null, entry]);
            continue;
        }

        if (entry !== null && typeof entry === "object" && !Array.isArray(entry)) {
            // The object spelling, because "the output is called this" reads
            // better as { out, in } than as ["out", "in"] and both are common.
            const input = entry.in !== undefined ? entry.in : entry.input;
            if (typeof input !== "string" || input.length === 0) {
                throw new TypeError('an entry point needs a non-empty "in"');
            }
            const out = entry.out;
            if (out === undefined || out === null || out === "") {
                pairs.push([null, input]);
            } else if (typeof out === "string") {
                pairs.push([out, input]);
            } else {
                throw new TypeError('an entry point\'s "out" must be a string');
            }
            continue;
        }

        if (Array.isArray(entry) && entry.length === 2) {
            const [out, input] = entry;
            if (typeof input !== "string" || input.length === 0) {
                throw new TypeError("an entry point's input must be a non-empty string");
            }
            pairs.push([out === undefined || out === null || out === "" ? null : String(out), input]);
            continue;
        }

        throw new TypeError('entryPoints takes a path, an { out, in } record, or a [out, in] pair');
    }

    return pairs;
}

/**
 * The options that are requests about the answer rather than about the code.
 *
 * They are not flags and are not entry points: they are fields on the request
 * that the service reads itself. Kept as one list so that a caller building a
 * request by hand has the same division in front of them, and so that there is
 * one place that says what a request is made of.
 */
const SERVICE_FIELDS = {
    write: "boolean",
    metafile: "boolean",
    absWorkingDir: "string",
    stdin: "object",
    mangleCache: "array",
    nodePaths: "array",
    sourcemapRaw: "string",
    color: "boolean",
};

// Attached to the exports rather than added to the block above, because the
// service-facing functions were written after that block and this file is read
// top to bottom by the next person who changes it.
module.exports.toFlags = toFlags;
module.exports.toEntryPoints = toEntryPoints;
module.exports.SERVICE_FIELDS = SERVICE_FIELDS;
module.exports.SERVICE_BOOLEANS = SERVICE_BOOLEANS;
module.exports.SERVICE_VALUES = SERVICE_VALUES;
module.exports.SERVICE_REPEATED = SERVICE_REPEATED;
module.exports.SOURCEMAP_VALUES = SOURCEMAP_VALUES;
module.exports.SERVICE_LOG_LEVELS = SERVICE_LOG_LEVELS;
