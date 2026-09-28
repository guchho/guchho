// Reading the engine's diagnostics back out of a captured error stream.
//
// This is the one piece of the API that is a pure function over a string, which
// makes it the one piece that can be tested exhaustively without a binary, and
// it is worth testing on its own: splitDiagnostics() is what decides whether a
// caller sees one syntax error or five, with a tail of timing lines appended to
// the last one as though they were part of its snippet.
//
// Why it is not trivial. The error stream holds three kinds of line that look
// alike and mean different things:
//
//   X [ERROR] ...          a diagnostic
//   X [WARN]  ...          a diagnostic
//   build finished in 4ms the engine narrating itself
//   1 error                the logger counting what it just said
//
// The first two are what the caller can act on. The last two are said on every
// run whether or not anything went wrong, and both arrive after the last real
// diagnostic — so without a rule for them they would be appended to whichever
// error happened to be last and read as part of a syntax error's source
// snippet. That is a bug report that cannot be reproduced, because the text
// being complained about is not in the text.

const assert = require("node:assert/strict");
const { describe, it } = require("node:test");

const { splitDiagnostics } = require("../guchho/lib/exec");

describe("splitDiagnostics: the two kinds of diagnostic", () => {
    it("reads an error as an error and a warning as a warning", () => {
        const { errors, warnings } = splitDiagnostics(
            "X [ERROR] could not resolve './missing.js'\n" +
            "X [WARN] this import is unused\n"
        );

        assert.equal(errors.length, 1);
        assert.equal(warnings.length, 1);
        assert.match(errors[0], /could not resolve/);
        assert.match(warnings[0], /this import is unused/);
    });

    it("accepts both spellings of the warning prefix", () => {
        // The logger has used both, and a caller filtering on one of them would
        // silently lose the other.
        const { errors, warnings } = splitDiagnostics(
            "X [WARN] short form\nX [WARNING] long form\n"
        );

        assert.equal(errors.length, 0);
        assert.equal(warnings.length, 2);
    });

    it("tolerates leading whitespace before the severity", () => {
        // An indented diagnostic is still a diagnostic, and reading it as
        // unclassified text would put it in errors by accident rather than by
        // decision.
        const { errors } = splitDiagnostics("   X [ERROR] indented\n");
        assert.equal(errors.length, 1);
    });

    it("keeps the text as it was written rather than trimming it into shape", () => {
        // The prose after the prefix is for a person, and a caller showing it
        // needs the original.
        const line = "X [ERROR] Unexpected token at 3:11";
        assert.deepEqual(splitDiagnostics(line).errors, [line]);
    });

    it("does not mistake a warning for an error because of the word inside it", () => {
        // "error" appears in plenty of messages that are not errors, so the
        // prefix is what decides and nothing else is consulted.
        const { errors, warnings } = splitDiagnostics(
            "X [WARN] the 'error' handler is missing\n"
        );

        assert.equal(errors.length, 0);
        assert.equal(warnings.length, 1);
    });
});

describe("splitDiagnostics: lines that continue one", () => {
    it("appends an unclassified line to the diagnostic above it", () => {
        // A multi-line diagnostic — a snippet with the line it is about —
        // belongs to the one it was printed under. Splitting it makes a
        // four-line syntax error read as four separate failures.
        const { errors } = splitDiagnostics(
            "X [ERROR] Unexpected token\n" +
            "  3 | const = 1;\n" +
            "    |       ^\n"
        );

        assert.equal(errors.length, 1);
        assert.match(errors[0], /const = 1/);
        assert.match(errors[0], /\^/);
    });

    it("appends to a warning as readily as to an error", () => {
        const { errors, warnings } = splitDiagnostics(
            "X [WARN] circular dependency\n  a -> b -> a\n"
        );

        assert.equal(errors.length, 0);
        assert.equal(warnings.length, 1);
        assert.match(warnings[0], /a -> b -> a/);
    });

    it("keeps each diagnostic separate when they are interleaved", () => {
        // Two errors, each with a snippet. Appending every continuation to the
        // last one would put the first error's snippet under the second.
        const { errors } = splitDiagnostics(
            "X [ERROR] first\n  snippet of first\n" +
            "X [ERROR] second\n  snippet of second\n"
        );

        assert.equal(errors.length, 2);
        assert.match(errors[0], /snippet of first/);
        assert.doesNotMatch(errors[0], /snippet of second/);
        assert.match(errors[1], /snippet of second/);
    });

    it("treats text with no diagnostic above it as an error", () => {
        // Something the engine said and nothing classified is closer to a
        // failure than to silence: dropping it would turn a real problem into
        // a successful-looking run.
        const { errors, warnings } = splitDiagnostics("something went wrong somewhere\n");

        assert.equal(errors.length, 1);
        assert.equal(warnings.length, 0);
    });
});

describe("splitDiagnostics: the engine's own commentary", () => {
    it("drops a timing line, which belongs to no diagnostic", () => {
        // "transform finished in 12ms" is there precisely because a transform
        // is a filter and its standard output belongs to the program. On the
        // error stream it is the logger narrating, and a caller cannot act on
        // it.
        const { errors, warnings } = splitDiagnostics(
            "X [ERROR] real problem\ntransform finished in 12ms\n"
        );

        assert.equal(errors.length, 1);
        assert.equal(warnings.length, 0);
        assert.doesNotMatch(errors[0], /finished in/);
    });

    it("drops the count of errors the logger just reported", () => {
        // Without this rule the last line of a failed run reads as part of the
        // last error, and "1 error" ends up inside a syntax error's message.
        const { errors } = splitDiagnostics(
            "X [ERROR] unexpected token\n1 error\n"
        );

        assert.equal(errors.length, 1);
        assert.doesNotMatch(errors[0], /1 error/);
    });

    it("drops a plural count as readily as a singular one", () => {
        const { errors } = splitDiagnostics("X [ERROR] bad\n3 errors\n");
        assert.equal(errors.length, 1);
        assert.doesNotMatch(errors[0], /3 errors/);
    });

    it("drops a summary naming both counts", () => {
        const { errors, warnings } = splitDiagnostics(
            "X [ERROR] bad\nX [WARN] iffy\n1 error and 1 warning\n"
        );

        assert.equal(errors.length, 1);
        assert.equal(warnings.length, 1);
    });

    it("recognises a timing line for a command other than transform", () => {
        // Matched by shape rather than by exact wording, so that a timer added
        // to another command does not make this quietly stop matching — and then
        // start appending timings to somebody's syntax error.
        const { errors } = splitDiagnostics(
            "X [ERROR] bad\nbuild finished in 4ms\n"
        );

        assert.equal(errors.length, 1);
        assert.doesNotMatch(errors[0], /finished in/);
    });

    it("keeps a diagnostic that happens to mention a timing", () => {
        // The other direction: a rule loose enough to catch "the chunk finished
        // in 3ms" would throw away a real message.
        const { errors } = splitDiagnostics(
            "X [ERROR] chunk finished in 3ms but the graph is wrong\n"
        );

        assert.equal(errors.length, 1);
    });
});

describe("splitDiagnostics: the shape of the input", () => {
    it("returns nothing at all for an empty stream", () => {
        // A successful run with no warnings says nothing, and that is not the
        // same as saying one empty thing.
        assert.deepEqual(splitDiagnostics(""), { errors: [], warnings: [] });
    });

    it("ignores blank lines and a trailing newline", () => {
        // A file that ends in a newline — which a captured stream always does —
        // must not produce a trailing empty error.
        const { errors, warnings } = splitDiagnostics("\n\nX [ERROR] bad\n\n");

        assert.equal(errors.length, 1);
        assert.equal(warnings.length, 0);
    });

    it("handles CRLF, which is what a Windows build produces", () => {
        // The carriage return is left in the text but must not become part of
        // the match, or every line on Windows stops looking like its own
        // diagnostic and the whole run reads as one long error.
        const { errors, warnings } = splitDiagnostics(
            "X [ERROR] first\r\nX [WARN] second\r\n"
        );

        assert.equal(errors.length, 1);
        assert.equal(warnings.length, 1);
    });

    it("keeps the order it was given, because the first error is the one to read", () => {
        const { errors } = splitDiagnostics(
            "X [ERROR] first thing\nX [ERROR] second thing\nX [ERROR] third thing\n"
        );

        assert.deepEqual(
            errors.map((e) => e.split("] ")[1]),
            ["first thing", "second thing", "third thing"]
        );
    });
});
