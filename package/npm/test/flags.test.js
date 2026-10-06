// Turning an options object into an argument list, which is the last thing that
// happens before the binary sees a request.
//
// None of this needs a binary. toArgs() is a pure function over its argument,
// and the argument list it produces is the whole contract between the API and
// the command line — so what is being pinned here is the spelling of every
// flag, not that the engine does anything useful with it. The files that need a
// binary are api-build.test.js and api-transform.test.mjs; what they do not
// check is that the arguments reaching the binary are ones it accepts.
//
// The spelling matters more than it looks. The command line rejects an unknown
// flag outright, with "Invalid build flag", and it has two separators rather
// than one: "--outdir=dist" but "--external:react". A wrapper that guessed
// wrong is not a wrapper that does slightly the wrong thing — it is one that
// fails every call, with an error naming a flag the caller never wrote. That is
// why there is a test per separator here rather than one test of the shape.

const assert = require("node:assert/strict");
const { describe, it } = require("node:test");

const { toArgs, defaultMetafilePath, LOG_LEVELS } = require("../guchho/lib/flags");

describe("toArgs: entry points", () => {
    it("puts entries first, because the command line reads them positionally", () => {
        // "guchho build [entry...] [options]" — an option before an entry is
        // read as an option and the entry as a stray argument, so the order is
        // not a formatting choice.
        assert.deepEqual(
            toArgs({ entries: ["src/index.html", "src/app.js"], minify: true }),
            ["src/index.html", "src/app.js", "--minify"]
        );
    });

    it("accepts entry as well as entries, because both spellings are in circulation", () => {
        assert.deepEqual(toArgs({ entry: "src/main.js" }), ["src/main.js"]);
    });

    it("sends no entry at all when neither is given, rather than an empty string", () => {
        // An empty positional is a path, and a path that does not exist is an
        // error about a file the caller never mentioned.
        assert.deepEqual(toArgs({}), []);
    });

    it("sends no entry when entries is null, which is a caller with none", () => {
        assert.deepEqual(toArgs({ entries: null }), []);
    });

    it("rejects an empty entry, rather than passing one on to be a missing file", () => {
        assert.throws(() => toArgs({ entries: [""] }), TypeError);
        assert.throws(() => toArgs({ entry: "" }), TypeError);
    });

    it("rejects a non-string entry", () => {
        assert.throws(() => toArgs({ entries: [42] }), TypeError);
    });

    it("sends no entry when the caller asked for none, which is what a transform wants", () => {
        // transform() is a filter over one piece of source: it has no entry
        // point, and an entry given to it is a path the filter was not asked
        // for. The second argument is how the caller says so.
        assert.deepEqual(toArgs({ entries: ["src/main.js"] }, { entries: false }), []);
    });
});

describe("toArgs: options that take no value", () => {
    it("sends a bare flag for true and nothing at all for false", () => {
        // There is no "turn this back off" spelling: a flag is a request, and
        // the way not to make it is not to make it. Sending "--pretty=false"
        // would also pin a default that belongs to the engine.
        assert.deepEqual(
            toArgs({ minify: true, pretty: false, sourcemap: true, watch: true }),
            ["--minify", "--sourcemap", "--watch"]
        );
    });

    it("sends nothing for a value that is neither true nor false", () => {
        // Not undefined and not true is not a request. Sending it would put
        // something on the command line the caller did not ask to be there.
        assert.deepEqual(toArgs({ minify: undefined, pretty: 1, sourcemap: null }), []);
    });

    it("carries every bare flag the build command documents", () => {
        // The list is spelled out rather than derived from a table in the
        // module, so that adding a flag to lib/flags.js without deciding
        // whether it is bare or valued shows up here.
        assert.deepEqual(
            toArgs({
                minify: true,
                minifyHtml: true,
                pretty: true,
                sourcemap: true,
                splitting: true,
                treeShaking: true,
                bundle: true,
                watch: true,
            }).sort(),
            [
                "--bundle",
                "--minify",
                "--minify-html",
                "--pretty",
                "--sourcemap",
                "--splitting",
                "--tree-shaking",
                "--watch",
            ]
        );
    });
});

describe("toArgs: options that take a value", () => {
    it("joins a value to its flag with an equals sign", () => {
        assert.deepEqual(
            toArgs({
                outdir: "dist",
                outfile: "out/bundle.js",
                target: "es2020",
                format: "esm",
                platform: "browser",
                jsx: "automatic",
                tsconfig: "tsconfig.json",
            }),
            [
                "--outdir=dist",
                "--outfile=out/bundle.js",
                "--target=es2020",
                "--format=esm",
                "--platform=browser",
                "--jsx=automatic",
                "--tsconfig=tsconfig.json",
            ]
        );
    });

    it("sends no flag for an option that was not given", () => {
        // The built-in defaults are the engine's business. A flag spelling out
        // a default is a second copy of it, and the two disagree the moment
        // either one changes.
        assert.deepEqual(toArgs({ outdir: undefined, outfile: null }), []);
    });

    it("sends --exports with an equals sign, like every other valued flag", () => {
        // "default" is the only value the grammar has, and the binary refuses
        // anything else with a note naming it — but the spelling of the flag
        // itself is this layer's contract: "--exports=default", not
        // "--exports:default" and not a bare "--exports".
        assert.deepEqual(toArgs({ exports: "default" }), ["--exports=default"]);
    });

    it("rejects a boolean for exports, which is a caller expecting a bare flag", () => {
        assert.throws(() => toArgs({ exports: true }), /exports/);
    });

    it("sends a number, because a value is stringified rather than refused", () => {
        assert.deepEqual(toArgs({ outdir: 8080 }), ["--outdir=8080"]);
    });

    it("rejects a boolean, which is a caller who expected a different shape of option", () => {
        // There is no flag that means "--minify=1", and a boolean reaching the
        // binary as "--outdir=true" is a parse failure several frames from the
        // line that caused it.
        assert.throws(() => toArgs({ outdir: true }), TypeError);
        assert.throws(() => toArgs({ format: false }), TypeError);
    });

    it("rejects nothing that a value is required for, and says which option it was", () => {
        // The message names the option, because "takes a value" on its own
        // leaves the caller to work out which of the eight it was.
        assert.throws(() => toArgs({ platform: true }), /platform/);
    });
});

describe("toArgs: options that may be given more than once", () => {
    // These three are the ones where a wrapper has to know a second thing: that
    // the separator is a colon and not an equals sign. "--external=react" is
    // not a misspelling the command line forgives — it is a flag it rejects
    // with "Invalid build flag", and one note suggesting the colon.

    it("writes --external with a colon, which is what the command line parses", () => {
        assert.deepEqual(toArgs({ external: ["react", "vue"] }), [
            "--external:react",
            "--external:vue",
        ]);
    });

    it("writes a single external as one flag", () => {
        assert.deepEqual(toArgs({ external: "lodash" }), ["--external:lodash"]);
    });

    it("expands a record of defines into one flag per entry", () => {
        // A record is the shape a config file holds, and it is the shape a
        // caller reaches for, so it has to mean something rather than arriving
        // as the text "[object Object]".
        assert.deepEqual(toArgs({ define: { DEBUG: "true", PROD: "1" } }), [
            "--define:DEBUG=true",
            "--define:PROD=1",
        ]);
    });

    it("expands a record of loaders the same way", () => {
        assert.deepEqual(toArgs({ loader: { ".svg": "dataurl" } }), [
            "--loader:.svg=dataurl",
        ]);
    });

    it("takes a define as a name=value string, which the binary also parses", () => {
        assert.deepEqual(toArgs({ define: "DEBUG=true" }), ["--define:DEBUG=true"]);
    });

    it("takes an array of either, one flag for each", () => {
        assert.deepEqual(toArgs({ define: [{ A: "1" }, { B: "2" }, "C=3"] }), [
            "--define:A=1",
            "--define:B=2",
            "--define:C=3",
        ]);
    });

    it("keeps the order the caller gave, because the last one may win", () => {
        assert.deepEqual(toArgs({ external: ["a", "b", "c"] }), [
            "--external:a",
            "--external:b",
            "--external:c",
        ]);
    });

    it("sends nothing for an empty list, which is a caller with no externals", () => {
        assert.deepEqual(toArgs({ external: [], define: {} }), []);
    });

    it("rejects a value with no name, which would be a flag with nothing after it", () => {
        assert.throws(() => toArgs({ external: 42 }), TypeError);
        assert.throws(() => toArgs({ define: [""] }), TypeError);
    });

    it("rejects a record for an option that takes a plain name", () => {
        // "external" has nothing to pair a name with, so {react: true} is a
        // caller's mistake rather than a shape to guess at.
        assert.throws(() => toArgs({ external: { react: true } }), TypeError);
    });

    it("rejects an empty name on the left of the =", () => {
        // "--define:=true" names nothing, and the binary's error for it says
        // less than the one that can be said here.
        assert.throws(() => toArgs({ define: { "": "true" } }), TypeError);
    });

    it("keeps a value that is itself a pair whole", () => {
        // The value goes in after the first "=", so a replacement that
        // contains one is not mistaken for the start of another pair.
        assert.deepEqual(toArgs({ define: { API: "a=b" } }), ["--define:API=a=b"]);
    });
});

describe("toArgs: the metafile", () => {
    it("sends the path the caller named", () => {
        assert.deepEqual(toArgs({}, { metafile: "/tmp/meta.json" }), [
            "--metafile=/tmp/meta.json",
        ]);
    });

    it("sends nothing when there is no path", () => {
        // build() asks for one every run, but toArgs is also the transform's
        // path to the same function and there is no file to write there.
        assert.deepEqual(toArgs({}, { metafile: null }), []);
    });

    it("is not taken from an option of that name, because a metafile takes a path", () => {
        // "metafile" is in the boolean table with no flag, and a caller who
        // sets it expecting a file to be written gets silence instead — which
        // is why this says so rather than quietly sending nothing.
        assert.deepEqual(toArgs({ metafile: "out/meta.json" }), []);
    });
});

describe("toArgs: the log level", () => {
    it("sends a level the command line accepts", () => {
        assert.deepEqual(toArgs({ logLevel: "error" }), ["--log-level=error"]);
    });

    it("accepts every level in the table, so the table is not narrower than the command line", () => {
        // "guchho build --help" lists six. If the command line grows a seventh
        // and this does not, a caller cannot reach it, so the test holds the
        // two to the same list rather than to this file's opinion.
        assert.deepEqual([...LOG_LEVELS].sort(), [
            "debug",
            "error",
            "info",
            "silent",
            "verbose",
            "warning",
        ]);
    });

    it("rejects a level the command line would reject, before the binary sees it", () => {
        // Refused here rather than left to the binary so that a typo is a
        // rejected promise instead of an exit code the caller has to know the
        // meaning of.
        assert.throws(() => toArgs({ logLevel: "loud" }), TypeError);
    });

    it("says which levels it accepts when it refuses one", () => {
        assert.throws(() => toArgs({ logLevel: "loud" }), /verbose, debug, info, warning, error, silent/);
    });
});

describe("defaultMetafilePath", () => {
    it("is in the temporary directory, because a metafile is a receipt and not an output", () => {
        // Left in the project it would make a build look like it had produced
        // a file it did not, and it would be a file no clean knows to remove.
        const file = defaultMetafilePath("test");
        assert.equal(require("node:path").dirname(file), require("node:os").tmpdir());
    });

    it("names the run and the process, so two builds cannot claim one receipt", () => {
        // The tag and the pid are both in the name because the case is two
        // builds in one process, which is what the API allows: a caller that
        // wants two projects built can await them in sequence and the second
        // would read the first's outputs.
        const file = defaultMetafilePath("metafile");
        assert.match(require("node:path").basename(file), /^guchho-metafile-\d+\.json$/);
    });

    it("is not the same path twice, whatever the tag", () => {
        assert.notEqual(defaultMetafilePath("a"), defaultMetafilePath("b"));
    });
});
