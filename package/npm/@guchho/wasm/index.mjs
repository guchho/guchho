// =============================================================================
// @guchho/wasm — the JavaScript surface of the WebAssembly engine
// =============================================================================
//
// The engine itself is the same C++ binary the native npm packages ship,
// compiled to WebAssembly and wrapped in Emscripten's MODULARIZE glue. This
// file is the thin layer that makes that binary usable from JavaScript:
//
//   Node (the primary target): the glue's filesystem starts empty, so the
//   host's real file system is mounted through NODEFS at "/host" and the
//   module's working directory is changed to the mount point corresponding
//   to process.cwd(). After that every relative path — entry points,
//   --outdir, imports walked by the resolver — behaves exactly as it does in
//   the native binary, and outputs land on the real disk with no copy-back.
//   -sNODERAWFS is deliberately not used: it is broken on Windows
//   (NotFound: FileSystem.access), and a mount is enough.
//
//   Browser (secondary): MEMFS only. Passing a `files` map to createGuchho
//   populates the in-memory tree; build() then returns the outputs as bytes.
//   No COOP/COEP headers are needed because the engine is single-threaded —
//   there are no pthreads to fence.
//
// The CLI is the same story one level down: bin/guchho.js calls runCli with
// process.argv, which mounts the host and hands the arguments straight to
// callMain. Flag grammar, diagnostics and exit codes are the engine's own.

// No host modules are imported at the top. This file has to load in a browser,
// where "node:path" and "node:url" do not resolve, so the Node-only helpers
// reach for them lazily inside the branches that only run on Node.

const isNode = typeof process !== "undefined" && process.versions?.node;

// node:path, loaded the first time a Node-only path helper runs. Null in a
// browser, where those helpers are never reached.
let nodePath = null;

// Loads the Emscripten glue as an ES module. The .wasm binary is resolved
// relative to this package file rather than the working directory, so the
// engine can be installed anywhere and still find its own binary.
async function instantiate(options = {}) {
    const { default: createGuchhoModule } = await import("./lib/guchho.js");

    // On Node the glue reads the binary from disk and wants a real path; in the
    // browser it fetches, and a URL relative to this module is the right answer.
    // The two are chosen here rather than by a build step.
    let locateFile;
    if (isNode) {
        const { fileURLToPath } = await import("node:url");
        nodePath = (await import("node:path")).default;
        const here = nodePath.dirname(fileURLToPath(import.meta.url));
        locateFile = (file) => nodePath.join(here, "lib", file);
    } else {
        locateFile = (file) => new URL(`lib/${file}`, import.meta.url).href;
    }

    const moduleConfig = {
        locateFile,
        noInitialRun: true,
    };
    if (options.print) moduleConfig.print = options.print;
    if (options.printErr) moduleConfig.printErr = options.printErr;

    return createGuchhoModule(moduleConfig);
}

// Maps an absolute host path to its "/host" counterpart inside the module's
// file system. "C:\proj" becomes "/host/proj"; "/home/u/proj" becomes
// "/host/home/u/proj". The drive root (or "/" on POSIX) is what gets mounted,
// so the resolver's walk up towards node_modules crosses out of the project
// the same way it does natively.
function toVirtual(absPath) {
    const root = nodePath.parse(absPath).root;
    const rest = absPath.slice(root.length).replace(/\\/g, "/");
    return "/host" + (rest === "" ? "" : "/" + rest);
}

// Mounts the host file system and chdirs into the mount point for "cwd".
// Returns the helpers a caller needs to translate paths the other way.
function mountHost(module, cwd = process.cwd()) {
    const FS = module.FS;
    const abs = nodePath.resolve(cwd);
    const root = nodePath.parse(abs).root;

    try {
        FS.mkdir("/host");
    } catch {
        // Already there — a second engine in the same process is fine.
    }
    FS.mount(FS.filesystems.NODEFS, { root }, "/host");
    FS.chdir(toVirtual(abs));

    return { FS, root, virtual: (p) => toVirtual(nodePath.resolve(abs, p)) };
}

// =========================================================================
// The compiler stages and the service request, over the C ABI
// =========================================================================
//
// guchho_service runs the engine's own service dispatch in this process, so
// every request the npm package would have sent over a pipe can be sent here
// instead. request() is that primitive. The three functions above it — lexer,
// parse and print — are the same public shapes guchho publishes, told which
// language they are working on, so a browser caller writes the same call it
// would write on the command line.
//
// The table below is the one place the public stage names meet the wire names.
// It is deliberately the same table package/npm/guchho/lib/compile.js keeps;
// a contract test checks that the two agree, which is cheaper than a shared
// package for forty lines of names and does not make the wasm engine depend on
// the native package.
const LANGUAGES = ["html", "css", "js"];

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

// The protocol's way of saying "this message carries no detail", mapped to
// undefined so a browser caller sees the same message object a Node caller
// does. Kept in sync with the engine by the same contract test.
const NO_DETAIL = -1;

function toMessages(messages) {
    if (!Array.isArray(messages)) return [];
    return messages.map((message) => ({
        id: typeof message.id === "string" ? message.id : "",
        pluginName: typeof message.pluginName === "string" ? message.pluginName : "",
        text: typeof message.text === "string" ? message.text : "",
        location: message.location ?? null,
        notes: Array.isArray(message.notes)
            ? message.notes.map((note) => ({
                  text: typeof note.text === "string" ? note.text : "",
                  location: note.location ?? null,
              }))
            : [],
        detail: message.detail === NO_DETAIL ? undefined : message.detail,
    }));
}

// JSON has no bytes, so a Uint8Array is written as the one value the engine
// decodes back into bytes. Everything else passes through untouched.
function encodeBytes(value) {
    if (value instanceof Uint8Array) {
        return { __bytes__: toBase64(value) };
    }
    if (Array.isArray(value)) {
        return value.map(encodeBytes);
    }
    if (value !== null && typeof value === "object") {
        const out = {};
        for (const [key, entry] of Object.entries(value)) {
            out[key] = encodeBytes(entry);
        }
        return out;
    }
    return value;
}

function toBase64(bytes) {
    if (typeof Buffer !== "undefined") {
        return Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength).toString("base64");
    }
    let binary = "";
    for (const byte of bytes) binary += String.fromCharCode(byte);
    return btoa(binary);
}

// The language a call is for, checked here rather than sent on to be refused,
// exactly as the Node package checks it — one message at the call site naming
// the three the engine has, not a request answered with an unknown command.
function languageOf(options, stage) {
    if (options === null || typeof options !== "object") {
        throw new TypeError(`${stage} takes an options object with a "language"`);
    }
    const given = options.language;
    if (given === undefined) {
        throw new TypeError(
            `${stage} needs a "language" — one of ${LANGUAGES.map((n) => `"${n}"`).join(", ")}. ` +
                "There is no default, because guessing from the text is guessing."
        );
    }
    if (!LANGUAGES.includes(given)) {
        throw new TypeError(
            `${stage} has no lexer for ${JSON.stringify(given)}; ` +
                `this engine has ${LANGUAGES.map((n) => `"${n}"`).join(", ")}`
        );
    }
    return given;
}

function sourceOf(input) {
    if (typeof input === "string") return input;
    if (input instanceof Uint8Array) return input;
    throw new TypeError("a compile command needs its input as a string or as bytes");
}

function requestOf(stage, language, source, options) {
    const payload = { command: STAGES[stage][language].command };
    if (source !== undefined) payload.input = sourceOf(source);
    for (const name of STAGES[stage][language].options) {
        if (options[name] !== undefined) payload[name] = options[name];
    }
    return payload;
}

// One request, sent. This is the raw half: it returns the engine's response
// untouched, including a refusal's "error" field, and lets the caller decide
// what a refusal means. lib/transport-wasm.js in the guchho package uses this
// so a build and a compile stage can each read the response their own way —
// the same split the native service draws between request() and call().
function sendRaw(module, payload) {
    if (Array.isArray(payload.plugins) && payload.plugins.length > 0) {
        throw new Error(
            "this request names plugins; plugin hooks are callbacks, and the WebAssembly engine " +
                "has no callback transport in process. Run a plugin project with the native engine."
        );
    }
    if (Array.isArray(payload.pluginGroups) && payload.pluginGroups.length > 0) {
        throw new Error(
            "this request names plugins; plugin hooks are callbacks, and the WebAssembly engine " +
                "has no callback transport in process. Run a plugin project with the native engine."
        );
    }

    const reply = module.ccall(
        "guchho_service",
        "string",
        ["string"],
        [JSON.stringify(encodeBytes(payload))]
    );

    let response;
    try {
        response = JSON.parse(reply);
    } catch {
        throw new Error("the Guchho engine sent something that was not a response");
    }

    return response;
}

// One request, sent and unwrapped. A refusal — an "error" field rather than a
// populated "errors" list — becomes a rejection carrying the engine's own
// wording, matching what the Node package throws.
function send(module, payload) {
    const response = sendRaw(module, payload);

    if (response !== null && typeof response === "object" && typeof response.error === "string") {
        const errors = toMessages(response.errors);
        if (errors.length === 0) {
            errors.push({
                id: "protocol-error",
                pluginName: "",
                text: response.error,
                location: null,
                notes: [],
                detail: undefined,
            });
        }
        const failure = new Error(errors[0].text);
        failure.errors = errors;
        failure.warnings = toMessages(response.warnings);
        throw failure;
    }

    return response;
}

function resultOf(response, fields) {
    const result = { errors: toMessages(response.errors), warnings: toMessages(response.warnings) };
    for (const field of fields) {
        if (response[field] !== undefined) result[field] = response[field];
    }
    return result;
}

// Builds the request/lexer/parse/print surface, bound to one module. The
// handle-to-language map lives here rather than on the engine object so a
// caller cannot see or disturb it: a handle is a number, and the only useful
// thing to know about one is which language it came from.
function compilerFor(module) {
    const handleLanguages = new Map();

    function lexer(source, options = {}) {
        const language = languageOf(options, "lexer");
        const stage = STAGES.lexer[language];
        return Promise.resolve(
            resultOf(send(module, requestOf("lexer", language, source, options)), stage.fields)
        );
    }

    function parse(source, options = {}) {
        const language = languageOf(options, "parse");
        const stage = STAGES.parse[language];
        const payload = requestOf("parse", language, source, options);
        if (typeof options.sourcefile === "string" && options.sourcefile !== "") {
            payload.sourcefile = options.sourcefile;
        }
        const response = send(module, payload);
        const result = resultOf(response, stage.fields);
        result.ast = response.id;
        if (typeof result.ast === "number") handleLanguages.set(result.ast, language);
        return Promise.resolve(result);
    }

    function print(ast, options = {}) {
        const language = languageOf(options, "print");
        assertHandle(ast, language);
        const stage = STAGES.print[language];
        const payload = requestOf("print", language, undefined, options);
        payload.ast = ast;
        return Promise.resolve(resultOf(send(module, payload), stage.fields));
    }

    function assertHandle(ast, language) {
        if (typeof ast !== "number" || !Number.isInteger(ast) || ast < 0) {
            throw new TypeError("print needs the handle a parse returned (a non-negative integer)");
        }
        const owner = handleLanguages.get(ast);
        if (owner !== undefined && owner !== language) {
            throw new TypeError(
                `handle ${ast} is a ${owner} tree, and this is print for ${language}. ` +
                    `Print it with the language it was parsed for: ${JSON.stringify(owner)}.`
            );
        }
    }

    return { lexer, parse, print };
}

// The absolute, normalised form of a virtual path — the two posix operations
// writeFiles needs, without node:path, which does not exist in a browser. A
// leading "/" is always produced; "." and empty segments are dropped and ".."
// pops, so far enough that it cannot climb above the root.
function posixResolve(relPath) {
    const out = [];
    for (const part of String(relPath).split("/")) {
        if (part === "" || part === ".") continue;
        if (part === "..") {
            out.pop();
            continue;
        }
        out.push(part);
    }
    return "/" + out.join("/");
}

// Populates the in-memory file system from a {"src/index.html": "..."} map.
// This is the browser path, and it is also the Node path when a caller wants
// to build something that never touches disk.
function writeFiles(FS, files) {
    for (const [relPath, content] of Object.entries(files)) {
        const abs = posixResolve(relPath);
        const parts = abs.split("/").filter(Boolean);
        let dir = "";
        for (const part of parts.slice(0, -1)) {
            dir += "/" + part;
            try {
                FS.mkdir(dir);
            } catch {
                // Exists — fine.
            }
        }
        FS.writeFile(abs, content);
    }
}

// Creates an engine instance.
//
// Input:  options.cwd        — Node only; the directory to mount and chdir to
//                             (default process.cwd()).
//         options.files      — a { path: string|Uint8Array } map written into
//                             MEMFS before the first call (browser / sandbox).
//         options.print / options.printErr
//                           — replace the console sinks diagnostics use.
// Output: an engine object: { module, FS, callMain, build, version }.
export async function createGuchho(options = {}) {
    const module = await instantiate(options);

    if (isNode && !options.files) {
        mountHost(module, options.cwd);
    } else if (options.files) {
        writeFiles(module.FS, options.files);
        module.FS.chdir("/");
    }

    const engine = {
        module,
        FS: module.FS,

        // Runs the CLI exactly as the native binary would. Arguments are the
        // ones after the program name. Returns the exit code.
        //
        // Emscripten's callMain does not merely return main's code — when the
        // glue is built for Node its quit hook assigns the code to
        // process.exitCode, so a build that fails inside an embedded engine
        // would take the embedding process down with it on the way out. The
        // engine's own code is returned to the caller, and the host's exit
        // status is left as the host set it: the two are different things, and
        // only the second one is the host's to end with.
        callMain(argv) {
            const hostExitCode = isNode ? process.exitCode : undefined;
            try {
                return module.callMain(argv);
            } finally {
                if (isNode) process.exitCode = hostExitCode;
            }
        },

        // One-shot build through the C ABI. Node paths are host paths;
        // in-memory builds pass a files map at creation time and use paths
        // relative to "/".
        build(entry, opts = {}) {
            const flags = {};
            if (opts.minify) flags.minify = true;
            if (opts.quiet) flags.quiet = true;
            if (opts.verbose) flags.verbose = true;
            if (opts.name) flags.name = opts.name;

            const code = module.ccall(
                "guchho_build",
                "number",
                ["string", "string", "string"],
                [entry, opts.outdir ?? "", Object.keys(flags).length ? JSON.stringify(flags) : ""],
            );
            if (code !== 0) {
                const message = module.ccall("guchho_last_error", "string", [], []);
                throw new Error(message || `guchho build failed with exit code ${code}`);
            }
            return { exitCode: 0 };
        },

        version() {
            return module.ccall("guchho_version", "string", [], []);
        },

        // One service request, in this process: the same JSON the engine would
        // have framed over a pipe, answered with the same response. The lower
        // level of the compiler API — lexer, parse and print are built on it —
        // and the way to reach any other service command (pack, format-msgs,
        // analyze-metafile) without the native binary.
        request(payload) {
            return Promise.resolve(send(module, payload));
        },

        // The same request without the refusal handling: the engine's response
        // is returned as-is, "error" field and all, so the caller can unwrap it
        // the way its own layer expects. This is what the guchho package's WASM
        // transport uses, where a build and a compiler stage read the same
        // response differently.
        requestRaw(payload) {
            return Promise.resolve(sendRaw(module, payload));
        },

        // The engine's own lexer, parser and printer, told which language they
        // are for. Same public shapes as the guchho package publishes.
        ...compilerFor(module),

        // Mounts a files map into this instance after creation.
        loadFiles(files) {
            writeFiles(module.FS, files);
        },

        dispose() {
            // The module frees itself with the process; nothing is retained
            // on this side beyond the object returned here.
        },
    };

    return engine;
}

// The CLI entry the npm bin field points at: mount, run, exit. Kept here so
// tests and hosts that spawn themselves get the same path mapping the bin
// script does.
export async function runCli(argv) {
    const engine = await createGuchho();
    return engine.callMain(argv);
}

export { toVirtual, mountHost };

// =========================================================================
// The module-level functions
// =========================================================================
//
// createGuchho() is the explicit form and stays the primary API: an engine per
// caller, disposed per caller. The functions below are for the caller who just
// wants to lex a string or run one build and does not want to hold an engine to
// do it. They build one engine on first use and reuse it for the life of the
// module.
//
// A build needs its files present before the first call, and an engine's file
// system is populated at creation, so configure({ files }) sets the in-memory
// tree the shared engine is created from. configure() must come before the
// first of these calls; a caller who needs more than one engine, or a
// different tree per build, keeps using createGuchho().
//
// The tree is the whole reason these exist for a browser, where there is no
// working directory to mount: the files a build reads are the ones passed here.

let sharedEngine = null;
const sharedConfig = { files: undefined, print: undefined, printErr: undefined };

// Sets what the shared engine is created from. Call before any other function
// in this section; calling it after the engine exists is a mistake worth a
// sentence rather than a silent no-op.
export function configure(options = {}) {
    if (sharedEngine !== null) {
        throw new Error(
            "the shared Guchho engine has already been created; configure() must be " +
                "called before the first build, lex, parse or print"
        );
    }
    if (options.files !== undefined) sharedConfig.files = options.files;
    if (options.print !== undefined) sharedConfig.print = options.print;
    if (options.printErr !== undefined) sharedConfig.printErr = options.printErr;
}

function engine() {
    if (sharedEngine === null) {
        sharedEngine = createGuchho({
            files: sharedConfig.files,
            print: sharedConfig.print,
            printErr: sharedConfig.printErr,
        });
    }
    return sharedEngine;
}

// Adds files to the shared engine after it exists. The same as calling
// loadFiles on the engine from createGuchho().
export async function loadFiles(files) {
    (await engine()).loadFiles(files);
}

// The compiler stages, unbound from an engine: the same shapes createGuchho()
// returns, resolved against the shared one.
export async function lexer(source, options) {
    return (await engine()).lexer(source, options);
}

export async function parse(source, options) {
    return (await engine()).parse(source, options);
}

export async function print(ast, options) {
    return (await engine()).print(ast, options);
}

// One build through the shared engine. See createGuchho().build for options.
export async function build(entry, options) {
    return (await engine()).build(entry, options);
}

// The raw service request, for any command the engine serves.
export async function request(payload) {
    return (await engine()).request(payload);
}

export async function version() {
    return (await engine()).version();
}
