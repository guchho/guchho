// The compiler stage table, written once and copied once.
//
// The public compiler API (lexer/parse/print) lives in the guchho package and
// speaks to a service over a pipe; the same three functions live in
// @guchho/wasm and speak to the engine in process. The command names, the
// options each command reads, and the fields each answer carries are the same
// in both, because they are the engine's, and neither package can import the
// other's copy: guchho must run without @guchho/wasm installed, and
// @guchho/wasm must not depend on the native package. So the table is written
// out twice, in guchho/lib/compile.js and @guchho/wasm/index.mjs, and this file
// is the thing that notices when the two stop agreeing.
//
// Per language, per stage, the two entries have to be identical — a command
// named differently in one copy is a request the engine refuses, and a field
// missing from one copy is an answer silently dropped. The languages list and
// the "no detail" sentinel have the same problem, so they are compared too.
//
// The tables are not exported, so they are read out of the source text rather
// than required. That is a little unusual; it is also the only way to compare
// two module-private constants without widening either package's public surface
// for the test's benefit. The scan is a plain balanced-literal reader and the
// literals are plain data, so it has nothing to be clever about.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { describe, it } = require("node:test");

const { NO_DETAIL } = require("../guchho/lib/convert");

const REPO = path.join(__dirname, "..", "..", "..");

const NODE_SOURCE = fs.readFileSync(path.join(REPO, "package/npm/guchho/lib/compile.js"), "utf8");
const WASM_SOURCE = fs.readFileSync(path.join(REPO, "package/npm/@guchho/wasm/index.mjs"), "utf8");

// Reads the literal assigned to `const <name> = ...` out of a module's source.
// The literal is returned as text and evaluated by the caller, so this only has
// to find its bounds: from the opening brace or bracket to the one that closes
// it, ignoring braces that fall inside strings.
function literalText(source, name) {
    const declaration = source.indexOf(`const ${name} =`);
    assert.notEqual(declaration, -1, `${name} should be declared`);

    const start = source.indexOf("=", declaration) + 1;
    let cursor = start;
    while (source[cursor] === " " || source[cursor] === "\n") cursor++;

    const open = source[cursor];
    assert.ok(open === "{" || open === "[", `${name} should be an object or array literal`);
    const close = open === "{" ? "}" : "]";

    let depth = 0;
    let quote = null;
    for (let i = cursor; i < source.length; i++) {
        const ch = source[i];
        if (quote !== null) {
            if (ch === "\\") {
                i++;
            } else if (ch === quote) {
                quote = null;
            }
            continue;
        }
        if (ch === '"' || ch === "'" || ch === "`") {
            quote = ch;
        } else if (ch === open) {
            depth++;
        } else if (ch === close) {
            depth--;
            if (depth === 0) return source.slice(cursor, i + 1);
        }
    }
    throw new Error(`could not find the end of ${name}`);
}

function literal(source, name) {
    return new Function(`return (${literalText(source, name)});`)();
}

// A number constant, read the plain way: there is no bracket to balance.
function numberConstant(source, name) {
    const match = new RegExp(`const ${name} = (-?\\d+);`).exec(source);
    assert.notEqual(match, null, `${name} should be a number constant`);
    return Number(match[1]);
}

describe("the compiler stage table", () => {
    it("names the same languages in both packages", () => {
        assert.deepEqual(
            literal(NODE_SOURCE, "LANGUAGES"),
            literal(WASM_SOURCE, "LANGUAGES")
        );
    });

    it("spells every command, option and field the same in both packages", () => {
        assert.deepEqual(
            literal(NODE_SOURCE, "STAGES"),
            literal(WASM_SOURCE, "STAGES")
        );
    });

    it("agrees on the no-detail sentinel", () => {
        assert.deepEqual(numberConstant(WASM_SOURCE, "NO_DETAIL"), NO_DETAIL);
    });
});
