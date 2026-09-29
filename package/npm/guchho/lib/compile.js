"use strict";

// The compiler half of the API: lex, parse, transform and print, one function
// per stage per language.
//
// None of these functions contain a compiler. Each is one request to the same
// service a build uses, and the engine that runs on the other end of the pipe
// is the same lexer, parser and printer a bundle goes through. That is the
// whole design: a reimplementation in JavaScript would be a second answer to a
// question the engine already answers, and the two would disagree about what
// "the same parse" means before long.
//
// The AST never crosses the wire. None of the three trees is a value — they
// are graphs of engine-owned nodes with interned symbols and pointers into
// shared tables — so a parse answers with an opaque handle and a structural
// summary produced by walking the engine's own tree. A transform turns one
// handle into another; a print turns a handle into text. A caller gets to ask
// what the parser made of a source without this package containing a parser.
//
// One shared request path, and the twelve functions differ only in the name of
// the command and which options they forward. Everything that is a refusal —
// a request the engine would not even begin — is a rejection carrying the
// engine's wording, which is the same rule the build path follows. A parse
// that found syntax errors is not a refusal: it is an answer, complete with an
// "errors" list, because a lexer and a parser that failed to read a file are
// still the thing a caller asked for an opinion from.

const { getService, BuildFailure, ServiceError } = require("./service");
const { toMessages } = require("./convert");

// The result every command ends with: the two diagnostic lists, converted to
// the published shape, and the rest of the response copied across. The rest is
// passed whole because it is already the published shape — the engine writes
// tokens, nodes, rules, symbols and code in the names this API documents, and
// a second converter would be a second source of truth for the same names.
function resultOf(response, fields) {
    const result = { errors: toMessages(response.errors), warnings: toMessages(response.warnings) };
    for (const field of fields) {
        if (response[field] !== undefined) result[field] = response[field];
    }
    return result;
}

// Turns "the engine said no to this request" into a rejection, and leaves an
// answer alone. A refusal carries its reason twice — a field the protocol
// reads and a diagnostic the caller reads — and the diagnostic is the one that
// becomes the BuildFailure, so the wording is the engine's own.
//
// This deliberately does not reject on a populated "errors" list. For a build,
// errors mean the build failed and the result would be a lie; for a lex or a
// parse, errors are the point of the call, and turning them into an exception
// would make "this source has a problem" indistinguishable from "this request
// was nonsense". The distinction is the response's "error" field: refusals set
// it, diagnostics do not.
function unwrap(response) {
    if (response === null || typeof response !== "object") {
        throw new ServiceError("the Guchho service sent something that was not a response");
    }

    if (typeof response.error === "string") {
        let errors = Array.isArray(response.errors) ? toMessages(response.errors) : [];
        if (errors.length === 0) {
            errors = [{
                id: "protocol-error",
                pluginName: "",
                text: response.error,
                location: null,
                notes: [],
                detail: undefined,
            }];
        }
        throw new BuildFailure(errors, toMessages(response.warnings));
    }

    return response;
}

// One compiler command, sent and unwrapped.
async function compileRequest(payload) {
    const service = await getService();
    const response = await service.request(payload);
    return unwrap(response);
}

// The fields to forward, for the commands that take them.
//
// A table rather than a switch, because a command and its options are a settled
// list and the table is the one place they are written down. Each entry names
// the option's public spelling; the wire uses the same spelling, so forwarding
// is copying. Options the caller did not give are left out, which is what lets
// the engine's default stand.
const OPTIONS = {
    "lex-html": [],
    "lex-css": ["includeComments"],
    "lex-js": [],
    "parse-html": ["fragment", "collectImportRecords", "collectInlineCode"],
    "parse-css": ["minifyWhitespace", "minifySyntax", "minifyIdentifiers"],
    "parse-js": ["minifyWhitespace", "minifySyntax", "minifyIdentifiers"],
    "transform-css": ["removeDeadRules"],
    "transform-html": [],
    "transform-js": [],
    "print-html": ["pretty", "minify", "scriptingEnabled"],
    "print-css": ["minifyWhitespace", "asciiOnly"],
    "print-js": ["minifyWhitespace", "minifySyntax", "minifyIdentifiers", "asciiOnly"],
};

// The fields the response is passed through with, per command. "ast" is never
// listed: it is how the caller holds the handle, and each command renames the
// engine's "id" to it so one name means one thing through the whole pipeline.
const FIELDS = {
    "lex-html": ["tokens", "count"],
    "lex-css": ["tokens", "comments", "count"],
    "lex-js": ["tokens", "count"],
    "parse-html": ["nodes", "nodeCount", "importRecords", "inlineScripts", "inlineStyles"],
    "parse-css": ["rules", "ruleCount", "symbolCount", "importRecords"],
    "parse-js": ["ok", "partCount", "symbols"],
    "transform-css": ["passes", "removed", "ruleCount"],
    "transform-html": ["passes", "note", "sourcefile"],
    "transform-js": ["passes", "note", "sourcefile"],
    "print-html": ["code"],
    "print-css": ["code"],
    "print-js": ["code"],
};

// Lex, parse, transform and print, one function per command.
//
// A stage shared by the three languages (and by both directions here) is one
// function rather than four nearly identical copies, because the alternative is
// the four copies. The difference between lexing HTML and lexing CSS is the
// name of the command and the options it reads, which is exactly what a table
// holds.
function makeLex(command) {
    return async function (input, options = {}) {
        const source = sourceOf(input);
        if (options === null || typeof options !== "object") {
            throw new TypeError(`${command} takes an options object`);
        }
        const payload = { command, input: source };
        forward(payload, options, command);
        return resultOf(await compileRequest(payload), FIELDS[command]);
    };
}

function makeParse(command) {
    return async function (input, options = {}) {
        const source = sourceOf(input);
        if (options === null || typeof options !== "object") {
            throw new TypeError(`${command} takes an options object`);
        }
        const payload = { command, input: source };
        if (typeof options.sourcefile === "string" && options.sourcefile !== "") {
            payload.sourcefile = options.sourcefile;
        }
        forward(payload, options, command);
        const response = await compileRequest(payload);
        const result = resultOf(response, FIELDS[command]);
        result.ast = response.id;
        return result;
    };
}

function makeTransform(command) {
    return async function (ast, options = {}) {
        const handle = handleOf(ast, command);
        if (options === null || typeof options !== "object") {
            throw new TypeError(`${command} takes an options object`);
        }
        const payload = { command, ast: handle };
        forward(payload, options, command);
        const response = await compileRequest(payload);
        const result = resultOf(response, FIELDS[command]);
        result.ast = response.id;
        return result;
    };
}

function makePrint(command) {
    return async function (ast, options = {}) {
        const handle = handleOf(ast, command);
        if (options === null || typeof options !== "object") {
            throw new TypeError(`${command} takes an options object`);
        }
        const payload = { command, ast: handle };
        forward(payload, options, command);
        return resultOf(await compileRequest(payload), FIELDS[command]);
    };
}

// The source text a compile command reads. A string passes through, and bytes
// pass through as bytes: a caller that read a file off disk already has a
// buffer, and decoding it here would be a copy it did not ask for. Anything
// else is refused in the same breath as a build call would refuse its options,
// rather than sent on to be refused a round trip later.
function sourceOf(input) {
    if (typeof input === "string") return input;
    if (input instanceof Uint8Array) return input;
    throw new TypeError("a compile command needs its input as a string or as bytes");
}

// The handle a transform or a print is told to work on. It is a number — the
// service's id for a tree it holds — and a caller that passes anything else
// has misunderstood the pipeline, which a refusal at this end makes plainer
// than an error from the engine would.
function handleOf(ast, command) {
    if (typeof ast !== "number" || !Number.isInteger(ast) || ast < 0) {
        throw new TypeError(`${command} needs the handle a parse returned (a non-negative integer)`);
    }
    return ast;
}

// Copies the options a command reads across to the request.
function forward(payload, options, command) {
    for (const name of OPTIONS[command]) {
        if (options[name] !== undefined) payload[name] = options[name];
    }
}

module.exports = {
    lexHTML: makeLex("lex-html"),
    lexCSS: makeLex("lex-css"),
    lexJS: makeLex("lex-js"),
    parseHTML: makeParse("parse-html"),
    parseCSS: makeParse("parse-css"),
    parseJS: makeParse("parse-js"),
    transformHTML: makeTransform("transform-html"),
    transformCSS: makeTransform("transform-css"),
    transformJS: makeTransform("transform-js"),
    printHTML: makePrint("print-html"),
    printCSS: makePrint("print-css"),
    printJS: makePrint("print-js"),
};