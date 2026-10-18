// What the browser entry actually pulls in.
//
// The "browser" export condition resolves to lib/browser/index.js, and that
// file's require graph is what a bundler will follow when it builds for the
// browser. A require of "fs" or "child_process" anywhere in it is a module that
// does not load in a browser; a require of service.js or main.js is worse,
// because those do load in some bundlers and then reach for a native binary that
// is not there.
//
// The graph is walked statically from the entry, following relative requires
// only. A bare specifier — @guchho/wasm — is recorded and not followed: it is the
// one cross-package dependency the browser needs, and its own graph is the wasm
// package's business.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { builtinModules } = require("node:module");
const { describe, it } = require("node:test");

const { GUCHHO_DIR } = require("./helpers/paths");

const LIB = path.join(GUCHHO_DIR, "lib");
const ENTRIES = [
    path.join(LIB, "browser", "index.js"),
    path.join(LIB, "browser", "index.mjs"),
];

// Every Node builtin, bare and "node:"-prefixed. The walker records bare
// specifiers and never follows them, so this is checked against that set.
const BUILTINS = new Set([
    ...builtinModules,
    ...builtinModules.map((name) => `node:${name}`),
]);

// The modules a browser cannot have. Each needs a host: a process to spawn, a
// socket to listen on, a watcher to hang an EventEmitter from, a working
// directory to read a config from, or a native binary to start.
const HOST_ONLY = ["service.js", "main.js", "watch.js", "serve.js", "config.js", "engine.js"];

// The specifier forms the package uses. A dynamic import("@guchho/wasm") and a
// require("./build") are both wanted; the "from" form catches an ESM import.
const SPECIFIER_PATTERNS = [
    /\brequire\(\s*["']([^"']+)["']\s*\)/g,
    /\bimport\(\s*["']([^"']+)["']\s*\)/g,
    /\bfrom\s+["']([^"']+)["']/g,
];

// Removes comments while respecting string literals, so a "//" inside a URL
// string is not mistaken for a comment and a comment that mentions a module
// name is not mistaken for an import. Template interpolation is not parsed
// beyond its backticks; the files in this graph do not put requires inside one.
function stripComments(source) {
    let out = "";
    let quote = null;

    for (let i = 0; i < source.length; i += 1) {
        const ch = source[i];
        const next = source[i + 1];

        if (quote !== null) {
            out += ch;
            if (ch === "\\") {
                out += next ?? "";
                i += 1;
            } else if (ch === quote) {
                quote = null;
            }
            continue;
        }

        if (ch === '"' || ch === "'" || ch === "`") {
            quote = ch;
            out += ch;
            continue;
        }

        if (ch === "/" && next === "/") {
            while (i < source.length && source[i] !== "\n") i += 1;
            out += "\n";
            continue;
        }

        if (ch === "/" && next === "*") {
            i += 2;
            while (i < source.length && !(source[i] === "*" && source[i + 1] === "/")) i += 1;
            i += 1;
            continue;
        }

        out += ch;
    }

    return out;
}

function specifiersOf(source) {
    const found = [];
    for (const pattern of SPECIFIER_PATTERNS) {
        pattern.lastIndex = 0;
        let match;
        while ((match = pattern.exec(source)) !== null) found.push(match[1]);
    }
    return found;
}

// Resolves a relative specifier the way Node's CommonJS resolver would for the
// extensions this package uses: the name as written, then with ".js" or
// ".json", then as a directory index.
function resolveRelative(fromFile, specifier) {
    const base = path.resolve(path.dirname(fromFile), specifier);
    for (const candidate of [base, `${base}.js`, `${base}.json`, path.join(base, "index.js")]) {
        if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) return candidate;
    }
    return null;
}

// Breadth-first over relative requires. Returns every file reached, every bare
// specifier seen, and for each reached file the file that required it.
function walk(roots) {
    const reached = new Set();
    const bare = new Set();
    const requiredBy = new Map();
    const queue = roots.map((file) => ({ file, from: null }));

    while (queue.length > 0) {
        const { file, from } = queue.shift();
        if (reached.has(file)) continue;
        reached.add(file);
        if (from !== null) requiredBy.set(file, from);

        const source = fs.readFileSync(file, "utf8");
        for (const specifier of specifiersOf(stripComments(source))) {
            if (!specifier.startsWith(".")) {
                bare.add(specifier);
                continue;
            }
            const target = resolveRelative(file, specifier);
            if (target !== null && !reached.has(target)) {
                queue.push({ file: target, from: file });
            }
        }
    }

    return { reached, bare, requiredBy };
}

describe("the browser require graph", () => {
    const graph = walk(ENTRIES);

    it("starts from files that exist", () => {
        for (const entry of ENTRIES) {
            assert.ok(fs.existsSync(entry), `${entry} should exist`);
        }
        assert.ok(graph.reached.size > 1, "the walk should reach more than the entry itself");
    });

    it("reaches the shared orchestrators", () => {
        const names = new Set([...graph.reached].map((file) => path.basename(file)));
        for (const expected of ["build.js", "transform.js", "pack.js", "context.js", "compile.js"]) {
            assert.ok(names.has(expected), `${expected} should be in the browser graph`);
        }
    });

    it("imports no Node builtins", () => {
        for (const specifier of graph.bare) {
            assert.ok(
                !BUILTINS.has(specifier),
                `the browser graph imports the Node builtin ${JSON.stringify(specifier)}`
            );
        }
    });

    it("does not reach a host-only module", () => {
        for (const name of HOST_ONLY) {
            const forbidden = path.join(LIB, name);
            assert.ok(
                !graph.reached.has(forbidden),
                `the browser graph reaches ${name} via ${describeChain(forbidden, graph.requiredBy)}`
            );
        }
    });
});

// The path from an entry to a file, so a failure names the require that let it
// in rather than only the file.
function describeChain(file, requiredBy) {
    const chain = [];
    let cursor = file;
    const seen = new Set();
    while (cursor !== undefined && !seen.has(cursor)) {
        seen.add(cursor);
        chain.unshift(path.relative(LIB, cursor));
        cursor = requiredBy.get(cursor);
    }
    return chain.join(" -> ");
}
