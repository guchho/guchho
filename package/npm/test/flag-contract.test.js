// Whether the flags the wrapper sends are flags the command line accepts.
//
// This is the seam between the two halves of the package, and nothing else in
// the suite stands on it. flags.test.js checks what the wrapper produces;
// api-build.test.js checks that a build happens. Neither asks whether the
// argument list between them is one the binary will take, and that question has
// a sharp answer waiting: the command line rejects an unknown flag outright, so
// a wrapper that spells a flag wrongly does not do the wrong thing, it fails
// every single call with "Invalid build flag" and a note about a flag the caller
// never typed.
//
// It is not a hypothetical spelling. The wrapper used to join every option to
// its flag with an equals sign, which is right for the twenty-odd options that
// take one and wrong for the three that take a colon — "--external:react", not
// "--external=react" — so every caller who passed an external, a define or a
// loader got an error about a flag they had not written. A define passed as a
// record, which is the shape a config file holds, made it worse: it arrived as
// the string "[object Object]".
//
// So the flags are read out of the engine's own source and compared against the
// tables in lib/flags.js. Neither side is written out here. That is the whole
// design: a list in this file would be a third copy of the grammar, and the
// first edit to any of the other two would leave it behind, passing.
//
// Reading the source also means this needs no built binary and no network, so it
// runs on a fresh checkout — which is where a wrapper that is quietly wrong is
// most likely to sit unexamined.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const {
    BOOLEAN_FLAGS,
    VALUE_FLAGS,
    REPEATED_FLAGS,
    toArgs,
} = require("../guchho/lib/flags");
const {
    cleanup,
    findBuiltBinary,
    makeTempDir,
    skipWithoutBinary,
} = require("./helpers/workspace");
const { REPO_ROOT } = require("./helpers/paths");

after(cleanup);

// The engine's two files that hold the grammar.
//
// cli_options.cpp is where the parsing is: every accepted spelling is a string
// literal in a comparison or a starts_with against an argument. cli_flags.cpp
// is where the build-only table is, which repeats a dozen of the same
// spellings and is the second place a change has to be made. Reading both
// catches an option the parser accepts but the build-only table forgot, which is
// the drift its own comment says the two are meant to catch between them.
const GRAMMAR_SOURCES = ["src/cli/cli_options.cpp", "src/cli/cli_flags.cpp"];

// Every flag spelling the engine knows, as written in its own source.
//
// Comments are stripped first, because the source is heavily commented and its
// comments quote flags freely — "--external=react" appears in exactly the
// comment explaining why the wrapper must not send it. Leaving comments in would
// mean this file passing because of a sentence rather than because of a parser,
// which is worse than not having the test: it would look like coverage.
//
// Only line comments are stripped, and only "//" ones. That is what this source
// uses; the absence of a block comment is checked rather than assumed, because
// one left in would put its contents into the set and quietly widen it to
// whatever it happened to mention.
function grammarFlags() {
    const flags = new Set();

    for (const relative of GRAMMAR_SOURCES) {
        const text = fs.readFileSync(path.join(REPO_ROOT, relative), "utf8");
        assert.doesNotMatch(
            text,
            /\/\*/,
            `${relative} has a block comment, so the stripping below would miss part of it`
        );

        for (const line of text.split(/\r?\n/)) {
            const code = line.replace(/\/\/.*$/, "");
            for (const [, flag] of code.matchAll(/"(--[a-z][a-z0-9-]*[=:]?)"/g)) {
                flags.add(flag);
            }
        }
    }

    // A scan that found almost nothing is a scan that is not working, and the
    // tests below would then pass by finding no flag to disagree with.
    assert.ok(flags.size > 40, `only found ${flags.size} flags; the scan has stopped working`);
    return flags;
}

// An option value that is enough to send the flag, for each of the three kinds
// of option. Written per kind rather than per flag, so a flag added to a table
// is covered without being listed here.
const A_VALUE = {
    bare: true,
    valued: "x",
    external: "react",
    define: "DEBUG=true",
    loader: ".svg=dataurl",
};
describe("the flags the wrapper sends", () => {
    it("sends only spellings the command line parses, separator and value included", () => {
        // The whole emitted argument is compared, not the flag name.
        // "--external" on its own appears nowhere in the grammar, and
        // "--external=react" is not a spelling the parser knows, so a check on
        // the name alone would pass while the wrapper sent the separator the
        // binary rejects.
        const grammar = grammarFlags();
        const unrecognised = [];

        for (const [name, flag] of Object.entries(BOOLEAN_FLAGS)) {
            if (!flag) continue;
            for (const arg of toArgs({ [name]: A_VALUE.bare })) {
                if (!grammar.has(arg)) unrecognised.push(arg);
            }
        }

        for (const [name, flag] of Object.entries(VALUE_FLAGS)) {
            // logLevel is the one option whose value is checked before it is
            // sent, so a placeholder is not enough to send it — and the reason
            // it is checked is flags.test.js's business, not this file's.
            const value = name === "logLevel" ? "warning" : A_VALUE.valued;

            for (const arg of toArgs({ [name]: value })) {
                if (!grammar.has(arg.replace(/=.*/, "="))) unrecognised.push(arg);
            }
        }

        for (const name of Object.keys(REPEATED_FLAGS)) {
            for (const arg of toArgs({ [name]: A_VALUE[name] })) {
                // The value is cut off at the first separator, leaving the flag
                // and its own — "--external:react" becomes "--external:".
                if (!grammar.has(arg.replace(/[=:].*/, arg.includes(":") ? ":" : "="))) {
                    unrecognised.push(arg);
                }
            }
        }

        assert.deepEqual(unrecognised, [], "these are not spellings the command line parses");
    });

    it("sends a metafile the way the command line spells it", () => {
        // Not in a table: build() asks for one through the second argument
        // rather than through an option, so there is no table to read it from
        // and it would go unchecked if it were not asked for here.
        const grammar = grammarFlags();

        for (const arg of toArgs({}, { metafile: "meta.json" })) {
            assert.ok(grammar.has(arg.replace(/=.*/, "=")), `${arg} is not a known flag`);
        }
    });

    it("takes a colon for the three options that take one", () => {
        // Worth its own case because these are asymmetric with the twenty-odd
        // options joined by "=", and the asymmetry is the entire reason the bug
        // existed. "--loader=" does happen to be in the grammar as well, which
        // is why the wrapper uses the colon for all three rather than picking
        // whichever spelling happens to work for each.
        const grammar = grammarFlags();

        for (const name of ["external", "define", "loader"]) {
            assert.ok(REPEATED_FLAGS[name].pairs !== undefined, `${name} should have a shape`);
        }

        assert.ok(grammar.has("--external:"));
        assert.ok(grammar.has("--define:"));
        assert.ok(grammar.has("--loader:"));
        assert.ok(!grammar.has("--external="));
        assert.ok(!grammar.has("--define="));
    });

    it("never sends a repeated flag with an equals sign, which is not one", () => {
        // The specific regression, held separately so that a failure names the
        // bug rather than arriving as a list of unrecognised flags.
        assert.deepEqual(toArgs({ external: "react" }), ["--external:react"]);
        assert.deepEqual(toArgs({ define: { DEBUG: "true" } }), ["--define:DEBUG=true"]);
        assert.deepEqual(toArgs({ loader: { ".svg": "dataurl" } }), ["--loader:.svg=dataurl"]);
    });

    it("never sends the text of an unexpanded object, which is what a record used to become", () => {
        // "--define=[object Object]" is a flag the parser knows and a value it
        // then rejects, which is a worse failure than the colon one was: the
        // error names a value the caller never wrote and says nothing about the
        // record they did.
        for (const options of [
            { define: { DEBUG: "true" } },
            { loader: { ".svg": "dataurl" } },
        ]) {
            for (const arg of toArgs(options)) {
                assert.doesNotMatch(arg, /\[object/, `${arg} is a stringified object`);
            }
        }
    });
});

// The other half of the contract, and the half that cannot be faked: the binary
// agreeing, rather than the source agreeing with itself. Everything above is a
// comparison between files, and two files can be wrong together — a flag renamed
// in both at once, or a grammar read from a comment the strip missed. These run
// the real command line with the argument list the wrapper builds, which is the
// only version of this question that is actually the question.
//
// They need a built binary and skip without one. A missing binary is a missing
// machine, not a broken package; GUCHHO_REQUIRE_BINARY=1 is what makes CI insist
// rather than skip. See skipWithoutBinary() in helpers/workspace.js.
describe("the command line accepting what the wrapper sends", () => {
    it("takes every flag the API can produce, in one run", { skip: skipWithoutBinary() }, () => {
        const root = makeTempDir("contract");
        fs.writeFileSync(path.join(root, "main.js"), "console.log(1);\n");

        // Every option at once, which is the case a caller hits by taking a
        // config file and handing the whole thing to build(). One run rather
        // than twenty: the binary stops at the first flag it does not
        // recognise, so a single run with all of them localises a failure to
        // the message instead of to the twentieth attempt.
        const args = [
            "build",
            "main.js",
            ...toArgs(
                {
                    minify: true,
                    minifyHtml: true,
                    pretty: true,
                    sourcemap: true,
                    splitting: true,
                    treeShaking: true,
                    bundle: true,
                    outdir: "out",
                    target: "es2020",
                    format: "esm",
                    platform: "browser",
                    jsx: "automatic",
                    logLevel: "error",
                    external: ["react"],
                    define: { DEBUG: "true" },
                    loader: { ".svg": "dataurl" },
                    // The two whose plain spelling a build used to refuse, which
                    // is the failure this run exists to catch: the wrapper sends
                    // "--banner=text", and only the build grammar's half of the
                    // parse decides whether that is a flag or a complaint.
                    banner: "/*! banner */",
                    footer: "/*! footer */",
                },
                { metafile: path.join(root, "meta.json") }
            ),
        ];

        const result = runBinary(args, root);

        // Exit 0 is not the assertion. A build of this input can fail for its
        // own reasons, and the run above is known to: the external package is
        // not installed, so the engine exits non-zero for a reason that has
        // nothing to do with the flags. The assertion is that it did not fail by
        // rejecting one, which is what the wrapper used to cause.
        assert.doesNotMatch(
            result.stderr,
            /Invalid (build|transform) flag/,
            `the command line rejected a flag the API produced:\n${result.stderr}`
        );
    });

    it("rejects a flag the API would never send, so the check above means something", { skip: skipWithoutBinary() }, () => {
        // The control. If the run above passed because the binary accepts
        // anything, this would pass too — and a test that cannot fail is not
        // evidence. So the same thing is run with the equals spelling, and the
        // binary is expected to turn it away with exactly the error the wrapper
        // used to provoke.
        //
        // The message says "transform" rather than "build", which is worth
        // reading rather than working around: a command line is read as a build
        // when something on it is evidence of one, and the evidence is a path
        // or a flag only the build grammar takes. "--external=" is neither —
        // the build-only table spells it with a colon — so a line carrying it
        // and no path is read as a transform, and the flag is rejected there.
        // Either way it is rejected, which is the point.
        const result = runBinary(["build", "--external=react"], process.cwd());

        assert.match(result.stderr, /Invalid (build|transform) flag: '--external=react'/);
    });
});

// Runs the binary and returns what it wrote, with the exit status alongside.
// A non-zero exit is not thrown: one of the cases above is a build that failed
// on purpose, and what is being asked is what it said rather than whether it
// succeeded.
function runBinary(args, cwd) {
    const child_process = require("node:child_process");
    const result = child_process.spawnSync(findBuiltBinary(), args, {
        cwd,
        encoding: "utf8",
    });

    if (result.error) {
        throw new Error(`could not run the guchho binary: ${result.error.message}`);
    }

    return {
        status: result.status,
        stdout: result.stdout ?? "",
        stderr: result.stderr ?? "",
    };
}
