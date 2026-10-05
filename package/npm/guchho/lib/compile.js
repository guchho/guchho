"use strict";

// The compiler half of the API: lexer(source), parse(source) and print(ast),
// each told which language it is working on.
//
// One function per stage rather than one per stage per language. A function per
// language is a spelling twelve times over for three operations, and it means
// every new language is twelve new exports, twelve new entries in a type
// declaration, and twelve new lines of documentation that differ from each other
// only in a suffix.
//
// None of these functions contain a compiler. Each is one request to the same
// service a build uses, and the engine on the other end of the pipe is the same
// lexer, parser and printer a bundle goes through. A reimplementation in
// JavaScript would be a second answer to a question the engine already answers,
// and the two would disagree about what "the same parse" means before long.
//
// The AST never crosses the wire. None of the three trees is a value — they are
// graphs of engine-owned nodes with interned symbols and pointers into shared
// tables — so parse answers with an opaque handle and a structural summary made
// by walking the engine's own tree. print turns a handle back into text. A caller
// gets to ask what the parser made of a source without this package containing a
// parser.
//
// There is no transformAst. The engine's transform commands are one language's
// worth of dead-rule removal and nothing else, and the high-level
// transform(source, { loader }) already reaches that work. A fourth function
// taking a handle would be a second way to do it, and two ways to do it is the
// thing this module exists to avoid.

const { getService, BuildFailure, ServiceError } = require("./service");
const { toMessages } = require("./convert");
const { protocolError } = require("./types");

// The languages the engine has a lexer, a parser and a printer for.
//
// This is the list the validation below checks against, and it is written out
// rather than derived, because a caller who asks for a language this engine
// cannot lex deserves an answer at the call site naming the three it can — not a
// request sent to the far end of a pipe and refused there, or worse, accepted and
// answered with an empty tree.
//
// "js" covers the JS parser's dialects. TypeScript and JSX are loader concerns at
// the bundler level and are not separate lexers here.
const LANGUAGES = ["html", "css", "js"];

// Per language, per stage: the command name on the wire, the options it reads,
// and the fields its answer carries.
//
// A table rather than a switch, because a command and its options are a settled
// list and this is the one place they are written down. Each entry names the
// option's public spelling and the wire uses the same spelling, so forwarding is
// copying. Options the caller did not give are left out, which is what lets the
// engine's default stand.
const STAGES = {
    lexer: {
        html: { command: "lex-html", options: [], fields: ["tokens", "count"] },
        css: {
            command: "lex-css",
            options: ["includeComments"],
            fields: ["tokens", "comments", "count"],
        },
        js: { command: "lex-js", options: [], fields: ["tokens", "count"] },
    },
    parse: {
        html: {
            command: "parse-html",
            options: ["fragment", "collectImportRecords", "collectInlineCode"],
            fields: ["nodes", "nodeCount", "importRecords", "inlineScripts", "inlineStyles"],
        },
        css: {
            command: "parse-css",
            options: ["minifyWhitespace", "minifySyntax", "minifyIdentifiers"],
            fields: ["rules", "ruleCount", "symbolCount", "importRecords"],
        },
        js: {
            command: "parse-js",
            options: ["minifyWhitespace", "minifySyntax", "minifyIdentifiers"],
            fields: ["ok", "partCount", "symbols"],
        },
    },
    print: {
        html: { command: "print-html", options: ["pretty", "minify", "scriptingEnabled"], fields: ["code"] },
        css: { command: "print-css", options: ["minifyWhitespace", "asciiOnly"], fields: ["code"] },
        js: {
            command: "print-js",
            options: ["minifyWhitespace", "minifySyntax", "minifyIdentifiers", "asciiOnly"],
            fields: ["code"],
        },
    },
};

// Which language each outstanding handle belongs to.
//
// The engine numbers handles per language, so handle 0 is a valid JS tree and a
// valid CSS tree and they are not the same tree. Handing one to the wrong printer
// is refused by the engine, but the engine's refusal is "that AST has been
// released, or was never created" — which is a true sentence about a tree the
// caller did not pass, and a useless one. Remembering the language here makes
// the mistake a message that names it: a handle is a value the caller was just
// given, and the one thing they cannot see about it is which language it is for.
//
// Bounded by the number of parse calls in a process, and never cleared: a handle
// is only forgotten by a process that has stopped parsing, which is a process
// that is about to exit.
const handleLanguages = new Map();

/**
 * Runs the engine's lexer over some source.
 *
 * @param {string|Uint8Array} source The source to read. Bytes pass through as
 *   bytes: a caller that read a file off disk already has a buffer, and decoding
 *   it here would be a copy it did not ask for.
 * @param {object} [options]
 * @param {'html'|'css'|'js'} options.language Required. There is no default,
 *   because guessing from the text is guessing.
 * @param {boolean} [options.includeComments] CSS only. Whether the comment list
 *   is populated. Off is also cheaper, which is the only reason to ask for it.
 * @returns {Promise<{errors: object[], warnings: object[], tokens: object[], count: number, comments?: object[]}>}
 * @throws {BuildFailure} When the engine refused the request. A source the lexer
 *   could not read comes back as a populated "errors" list instead — that is an
 *   answer, not a refusal.
 */
async function lexer(source, options = {}) {
    const { language } = languageOf(options, "lexer");
    const payload = { command: STAGES.lexer[language].command, input: sourceOf(source) };
    forward(payload, options, STAGES.lexer[language]);

    return resultOf(await compileRequest(payload), STAGES.lexer[language].fields);
}

/**
 * Runs the engine's parser over some source.
 *
 * @param {string|Uint8Array} source The source to read.
 * @param {object} [options]
 * @param {'html'|'css'|'js'} options.language Required.
 * @param {string} [options.sourcefile] The name a diagnostic should blame.
 *   Worth setting: without it a syntax error reports a position and no file.
 * @returns {Promise<{errors: object[], warnings: object[], ast: number}>} The
 *   summary's fields are the language's — nodes for HTML, rules for CSS, parts
 *   and symbols for JS. "ast" is the handle to pass to print().
 * @throws {BuildFailure} When the engine refused the request.
 */
async function parse(source, options = {}) {
    const { language } = languageOf(options, "parse");
    const stage = STAGES.parse[language];

    const payload = { command: stage.command, input: sourceOf(source) };
    // An empty sourcefile is not sent. The engine takes its absence to mean "this
    // came from a string", and an empty string would instead be a file with no
    // name, which is the same answer for a caller who did not ask for it.
    if (typeof options.sourcefile === "string" && options.sourcefile !== "") {
        payload.sourcefile = options.sourcefile;
    }
    forward(payload, options, stage);

    const response = await compileRequest(payload);
    const result = resultOf(response, stage.fields);

    // The engine's "id" is renamed to "ast" here so one name means one thing
    // through the whole pipeline: what parse answers with is what print takes.
    result.ast = response.id;
    if (typeof result.ast === "number") handleLanguages.set(result.ast, language);

    return result;
}

/**
 * Prints a parsed tree back to source.
 *
 * @param {number} ast The handle parse() returned.
 * @param {object} [options]
 * @param {'html'|'css'|'js'} options.language Required, and it must be the
 *   language parse() was given. Mismatched, this throws rather than sending the
 *   handle somewhere it does not belong.
 * @returns {Promise<{errors: object[], warnings: object[], code: string}>}
 * @throws {TypeError} When the handle is not one parse() returned, or belongs to
 *   another language.
 * @throws {BuildFailure} When the engine refused the request.
 */
async function print(ast, options = {}) {
    const { language } = languageOf(options, "print");
    assertHandle(ast, language);

    const stage = STAGES.print[language];
    const payload = { command: stage.command, ast };
    forward(payload, options, stage);

    return resultOf(await compileRequest(payload), stage.fields);
}

// The result every command ends with: the two diagnostic lists, converted to the
// published shape, and the rest of the response copied across. The rest is
// passed whole because it is already the published shape — the engine writes
// tokens, nodes, rules, symbols and code in the names this API documents, and a
// second converter would be a second source of truth for the same names.
function resultOf(response, fields) {
    const result = { errors: toMessages(response.errors), warnings: toMessages(response.warnings) };
    for (const field of fields) {
        if (response[field] !== undefined) result[field] = response[field];
    }
    return result;
}

// Turns "the engine said no to this request" into a rejection, and leaves an
// answer alone. A refusal carries its reason twice — a field the protocol reads
// and a diagnostic the caller reads — and the diagnostic is the one that becomes
// the BuildFailure, so the wording is the engine's own.
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
        const errors = Array.isArray(response.errors) ? toMessages(response.errors) : [];
        if (errors.length === 0) {
            errors.push(protocolError(response.error));
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

/**
 * The language a call is for, checked here rather than sent on to be refused.
 *
 * "loader" is accepted as a spelling of "language" because transform() takes its
 * loader under that name and a caller holding a loader has it in hand already.
 * Two spellings for one thing in one package is the inconsistency this module
 * removes, so one of them is accepted and the other is documented as the
 * canonical one. "language" is canonical because this is not a bundler and there
 * is no loader resolution happening here.
 */
function languageOf(options, stage) {
    if (options === null || typeof options !== "object") {
        throw new TypeError(`${stage} takes an options object with a "language"`);
    }

    const given = options.language !== undefined ? options.language : options.loader;

    if (given === undefined) {
        throw new TypeError(
            `${stage} needs a "language" — one of ${quoted(LANGUAGES)}. ` +
                "There is no default, because guessing from the text is guessing."
        );
    }
    if (!LANGUAGES.includes(given)) {
        throw new TypeError(
            `${stage} has no lexer for ${JSON.stringify(given)}; ` +
                `this engine has ${quoted(LANGUAGES)}`
        );
    }

    return { language: given };
}

/** The languages, spelled for an error message. */
function quoted(names) {
    return names.map((name) => `"${name}"`).join(", ");
}

// The handle a print is told to work on, and the language it has to be.
//
// Two refusals, because they are two different mistakes. A value that is not a
// handle at all means the caller has misunderstood the pipeline, which is plainer
// here than as an error from the engine. A handle from another language is a
// subtler mistake — it is a real handle — and the engine's own answer for it
// ("released, or was never created") describes a tree the caller never passed.
function assertHandle(ast, language) {
    if (typeof ast !== "number" || !Number.isInteger(ast) || ast < 0) {
        throw new TypeError(
            "print needs the handle a parse returned (a non-negative integer)"
        );
    }

    const owner = handleLanguages.get(ast);
    if (owner !== undefined && owner !== language) {
        throw new TypeError(
            `handle ${ast} is a ${owner} tree, and this is print for ${language}. ` +
                `Handles are numbered per language, so ${ast} also names a ${language} tree — ` +
                "which is a different one. Print it with the language it was parsed for."
        );
    }
}

// The source text a compile command reads, and nothing else. A string passes
// through, bytes pass through as bytes, and anything else is refused in the same
// breath as a build call would refuse its options, rather than sent on to be
// refused a round trip later.
function sourceOf(input) {
    if (typeof input === "string") return input;
    if (input instanceof Uint8Array) return input;
    throw new TypeError("a compile command needs its input as a string or as bytes");
}

// Copies the options a command reads across to the request.
function forward(payload, options, stage) {
    for (const name of stage.options) {
        if (options[name] !== undefined) payload[name] = options[name];
    }
}

module.exports = { lexer, parse, print, LANGUAGES };
