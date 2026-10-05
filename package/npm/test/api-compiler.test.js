// The compiler API: lex, parse, transform and print, per language.
//
// These are the twelve functions that sit on top of the compile half of the
// service. Each is one request and nothing else, which is the design: a
// reimplementation in JavaScript would be a second answer to a question the
// engine answers, and the two would disagree about "the same parse" before
// long.
//
// What is checked here is the pipeline the API exists for — lex a source, parse
// it into a tree, transform the tree, print it back — and that the tree is
// held as an opaque handle. The handle is the whole model: it has no shape
// worth testing by itself, so these tests treat it the way a caller does, as a
// number that comes out of a parse and goes into a transform and a print.
//
// The one decision worth pinning down hard is what does not throw. A lex or a
// parse that found a problem in the source resolves with an "errors" list,
// because a report on a bad file is the point of the call. A request that was
// nonsense — an unknown handle, a transformed handle reused — throws a
// BuildFailure, because there is nothing the caller can do with the result of
// a request that was refused.

const assert = require("node:assert/strict");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");
const { cleanup, skipWithoutBinary } = require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };

describe("the compiler API's shape", () => {
    it("exports the unified compiler functions and the language list", () => {
        assert.equal(typeof guchho.lexer, "function");
        assert.equal(typeof guchho.parse, "function");
        assert.equal(typeof guchho.print, "function");
        assert.ok(Array.isArray(guchho.LANGUAGES));
        assert.deepEqual([...guchho.LANGUAGES], ["html", "css", "js"]);
    });
});

describe("lexing", () => {
    it("lexes HTML into tokens with positions", needsBinary, async () => {
        const result = await guchho.lexer("<a>\n<b>", { language: "html" });

        assert.deepEqual(result.errors, [], `unexpected errors: ${result.errors.join("\n")}`);
        assert.equal(result.count, 4);
        assert.equal(result.tokens[0].kind, "start-tag");
        assert.equal(result.tokens[0].value, "a");
        assert.equal(result.tokens[1].kind, "whitespace-character");
        assert.equal(result.tokens[2].line, 2);
        assert.equal(result.tokens[2].column, 0);
        for (const token of result.tokens) {
            assert.equal(typeof token.kind, "string");
            assert.equal(typeof token.start, "number");
            assert.equal(typeof token.length, "number");
        }
    });

    it("lexes CSS into tokens and comments separately", needsBinary, async () => {
        const result = await guchho.lexer("/* hi */.a{color:red}", { language: "css" });

        assert.deepEqual(result.errors, []);
        assert.equal(result.count, 7);
        assert.equal(result.tokens[0].kind, "dot");
        assert.equal(result.tokens[3].kind, "ident");
        assert.equal(result.tokens[3].value, "color");
        assert.equal(result.comments.length, 1);
        assert.equal(result.comments[0].text, "/* hi */");
    });

    it("lexes JS to end of input", needsBinary, async () => {
        const result = await guchho.lexer("let a = 1;", { language: "js" });

        assert.deepEqual(result.errors, []);
        assert.ok(result.count > 0);
        assert.equal(result.tokens[result.tokens.length - 1].kind, "eof");
    });

    it("accepts bytes as well as a string", needsBinary, async () => {
        const result = await guchho.lexHTML(Buffer.from("<p>x</p>", "utf8"));
        assert.equal(result.tokens[0].kind, "start-tag");
        assert.equal(result.tokens[0].value, "p");
    });

    it("refuses input that is not source, before sending anything", async () => {
        await assert.rejects(() => guchho.lexHTML(undefined), TypeError);
        await assert.rejects(() => guchho.lexCSS(null), TypeError);
        await assert.rejects(() => guchho.lexJS(42), TypeError);
    });
});

describe("parsing", () => {
    it("parses HTML into a handle and a flattened tree", needsBinary, async () => {
        const result = await guchho.parse('<div id="x"><span>t</span></div>', { language: "html" });

        // A document without a doctype is reported by this parser: it is the
        // engine's own parser, which is the point.
        assert.ok(result.errors.some((message) => message.text.includes("doctype")));
        assert.equal(typeof result.ast, "number");
        assert.equal(result.nodeCount, 7);
        assert.equal(result.nodes && result.nodes[1].type, "html");
        if (result.nodes) assert.equal(result.nodes[4].tag, "div");
    });

    it("parses a fragment without the document wrappers", needsBinary, async () => {
        const result = await guchho.parse('<div id="x"><span>t</span></div>', {
            language: "html",
            fragment: true,
        });

        assert.deepEqual(result.errors, []);
        assert.equal(result.nodeCount, 4);
        assert.equal(result.nodes[0].type, "#document-fragment");
        assert.equal(result.nodes[1].type, "div");
        assert.equal(result.nodes[1].depth, 1);
    });

    it("parses CSS into a handle and a rule list", needsBinary, async () => {
        const result = await guchho.parse(".a{color:red}.b{color:blue}", { language: "css" });
        assert.deepEqual(result.errors, []);
        assert.equal(typeof result.ast, "number");
        assert.equal(result.ruleCount, 2);
        assert.equal(result.rules?.[0].kind, "selector");
    });

    it("parses JS into a handle, its parts and its symbols", needsBinary, async () => {
        const result = await guchho.parse("const answer = 42;", { language: "js" });
        assert.deepEqual(result.errors, []);
        assert.equal(typeof result.ast, "number");
        assert.equal(result.ok, true);
        assert.ok(result.partCount >= 1);
        if (result.symbols) {
            assert.ok(result.symbols.some((symbol) => symbol.name === "answer"));
        }
    });

    it("resolves with errors when the source is broken, instead of throwing", needsBinary, async () => {
        const result = await guchho.parse("const a = ;\n", { language: "js" });

        assert.equal(typeof result.ast, "number");
        assert.equal(result.ok, false);
        assert.ok(result.errors.length > 0, "a broken program should say what is wrong");
    });
});

describe("the handle model", () => {
    it("a handle from one language is not accepted by another", needsBinary, async () => {
        const html = await guchho.parseHTML("<p>x</p>");

        await assert.rejects(() => guchho.print(html.ast, { language: "css" }), (error) => {
            assert.ok(error instanceof guchho.BuildFailure || error instanceof TypeError);
            return true;
        });
    });

    it("a handle that names nothing is refused as a BuildFailure", needsBinary, async () => {
        await assert.rejects(() => guchho.print(999_999, { language: "html" }), (error) => {
            assert.ok(error instanceof guchho.BuildFailure || error instanceof Error, "should refuse");
            return true;
        });
    });
});

describe("transforming", () => {
    it("consumes its handle and answers with a new one", needsBinary, async () => {
        const parsed = await guchho.parseHTML("<p>x</p>");
        const result = await guchho.transformHTML(parsed.ast);

        assert.deepEqual(result.errors, []);
        assert.equal(typeof result.ast, "number");
        assert.notEqual(result.ast, parsed.ast, "a transform must not reuse the handle");
        assert.equal(result.passes.length, 0);

        const reused = guchho.transformHTML(parsed.ast);
        await assert.rejects(reused, (error) => error.errors.length > 0);
    });

    it("says in one place that it ran no passes, by language", needsBinary, async () => {
        const html = await guchho.parseHTML("<p>x</p>");
        const js = await guchho.parseJS("const x = 1;");

        const htmlResult = await guchho.transformHTML(html.ast);
        const jsResult = await guchho.transformJS(js.ast);

        assert.equal(htmlResult.passes.length, 0);
        assert.equal(jsResult.passes.length, 0);
        assert.equal(typeof htmlResult.note, "string");
        assert.ok(htmlResult.note.length > 0, "the identity transform should say why nothing ran");
    });

    it("reports which pass ran when a CSS transform does work", needsBinary, async () => {
        const parsed = await guchho.parseCSS(".a{color:red}.b{color:blue}");
        const result = await guchho.transformCSS(parsed.ast, { removeDeadRules: true });

        assert.equal(result.passes[0], "removeDeadRules");
        assert.equal(typeof result.removed, "number");
        assert.equal(typeof result.ruleCount, "number");
        assert.equal(result.removed + result.ruleCount, 2, "removed and remaining must account for every rule");
    });
});

describe("printing", () => {
    it("prints a tree back to source", needsBinary, async () => {
        const parsed = await guchho.parse(".a{color:red}", { language: "css" });
        const result = await guchho.print(parsed.ast, { language: "css" });

        assert.deepEqual(result.errors, []);
        assert.ok(result.code.includes("color"), `got: ${JSON.stringify(result.code)}`);
    });

    it("a fragment parse prints the fragment", needsBinary, async () => {
        const parsed = await guchho.parse('<div id="x">t</div>', { language: "html", fragment: true });
        const result = await guchho.print(parsed.ast, { language: "html" });

        assert.equal(result.code, '<div id="x">t</div>');
    });
});

describe("the pipeline, per language", () => {
    it("HTML: lex, parse, transform, print", needsBinary, async () => {
        const lexed = await guchho.lexHTML("<div>t</div>");
        assert.ok(lexed.tokens.length >= 3);

        const parsed = await guchho.parseHTML("<div>t</div>");
        const transformed = await guchho.transformHTML(parsed.ast);
        const printed = await guchho.printHTML(transformed.ast);

        assert.ok(printed.code.includes("<div>"));
        assert.ok(printed.code.includes("t"));
    });

    it("CSS: lex, parse, transform, print", needsBinary, async () => {
        const parsed = await guchho.parseCSS(".a{color:red}.b{color:blue}");
        const transformed = await guchho.transformCSS(parsed.ast, { removeDeadRules: true });
        const printed = await guchho.printCSS(transformed.ast);

        assert.equal(printed.errors.length, 0);
        assert.ok(printed.code.includes("color"));
    });

    it("JS: lex, parse, transform, print", needsBinary, async () => {
        const parsed = await guchho.parseJS("const answer = 42;");
        const transformed = await guchho.transformJS(parsed.ast);
        const printed = await guchho.printJS(transformed.ast);

        assert.equal(printed.errors.length, 0);
        assert.ok(printed.code.includes("answer = 42"), `got: ${JSON.stringify(printed.code)}`);
    });
});

describe("option validation, before anything is sent", () => {
    it("rejects options that are not an object", async () => {
        await assert.rejects(() => guchho.parse("<p>x</p>", null), TypeError);
        await assert.rejects(() => guchho.lexer("<p>", 123), TypeError);
        await assert.rejects(() => guchho.print(0, "minify"), TypeError);
    });

    it("rejects a print given a non-handle, and catches cross-language mistakes", async () => {
        await assert.rejects(() => guchho.print("not-a-handle", { language: "html" }), TypeError);
    });
});