// @guchho/wasm — what a publish would contain, and whether the engine in it
// actually builds anything.
//
// The pack half asks npm, the same way the main package's pack test does: the
// "files" field, the always-included files and the size budget are things a
// manifest can be self-consistent about and still get wrong.
//
// The smoke half loads the real engine from the packaging directory. That
// makes these tests dependent on a wasm build existing — scripts/build-wasm.sh
// puts lib/guchho.wasm and lib/guchho.js there — so they skip, rather than
// fail, when it does not. A machine that has not installed emsdk is a machine
// that cannot check this, not a machine with a broken package.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const { after, describe, it } = require("node:test");

const { npmPackDryRun, makeTempDir } = require("./helpers/workspace");
const { WASM_DIR, readManifest } = require("./helpers/paths");

const manifest = readManifest(WASM_DIR);

// Every engine the engine tests create, so they can be disposed on the way
// out. A wasm module left loaded keeps its memory and its exit hooks alive
// past the end of the run, and node --test then reports a file-level failure
// for a rejection that happens after every test has already passed.
const engines = [];

async function makeEngine(options = {}) {
    const { createGuchho } = await import("../@guchho/wasm/index.mjs");
    const engine = await createGuchho({ print: () => {}, printErr: () => {}, ...options });
    engines.push(engine);
    return engine;
}

after(() => {
    for (const engine of engines) {
        try {
            engine.dispose();
        } catch {
            // Already gone, or never held anything — either is fine here.
        }
    }
});

// The engine is two files: the glue Emscripten emits and the wasm binary it
// compiles. Both have to be in the tarball for the package to run anywhere,
// and neither is replaceable by the other.
function hasBuiltEngine() {
    return (
        fs.existsSync(path.join(WASM_DIR, "lib", "guchho.wasm")) &&
        fs.existsSync(path.join(WASM_DIR, "lib", "guchho.js"))
    );
}

describe("@guchho/wasm pack", () => {
    it("is a manifest at all", () => {
        assert.ok(manifest, "package/npm/@guchho/wasm/package.json could not be read");
        assert.equal(manifest.name, "@guchho/wasm");
        assert.equal(manifest.type, "module");
        assert.ok(manifest.engines?.node, "the manifest should say which Node it needs");
    });

    it("has no postinstall and no native dependencies", () => {
        // The package's whole pitch is that it installs anywhere Node runs,
        // with nothing to compile and nothing to execute on the way in. A
        // postinstall would be a script running with the user's privileges on
        // every install; a native dependency would fail on the platforms the
        // package exists to serve.
        assert.ok(!manifest.scripts || !manifest.scripts.postinstall, "no postinstall script");
        assert.ok(
            !manifest.dependencies || Object.keys(manifest.dependencies).length === 0,
            "no runtime dependencies"
        );
    });

    it("packs the API, the CLI, the glue and the wasm binary", { skip: !hasBuiltEngine() }, () => {
        const packed = new Set(npmPackDryRun(WASM_DIR).files.map((f) => f.path));

        for (const required of [
            "package.json",
            "index.mjs",
            "index.cjs",
            "index.d.ts",
            "bin/guchho.js",
            "lib/guchho.js",
            "lib/guchho.wasm",
            "README.md",
            "LICENSE",
        ]) {
            assert.ok(packed.has(required), `${required} would be missing from the tarball`);
        }
    });

    it("declares its types entry and ships it", () => {
        // TypeScript resolves this package through the manifest's "types"
        // field and the "types" condition in exports. A declaration file
        // that exists but is not packed would typecheck in the repository
        // and fail for every consumer, which is the worst place to find
        // that out.
        assert.equal(manifest.types, "index.d.ts");
        assert.equal(manifest.exports["."].types, "./index.d.ts");
        assert.ok(
            fs.existsSync(path.join(WASM_DIR, manifest.types)),
            "the file manifest.types names does not exist"
        );

        const packed = new Set(npmPackDryRun(WASM_DIR).files.map((f) => f.path));
        assert.ok(packed.has("index.d.ts"), "index.d.ts would be missing from the tarball");
    });

    it("does not pack a .data preload file", () => {
        // The engine takes its input from the file system mount, not from a
        // preload baked in at link time. A .data file would mean the opposite
        // design had crept back in, and would double the download for nothing.
        const packed = new Set(npmPackDryRun(WASM_DIR).files.map((f) => f.path));
        for (const name of packed) {
            assert.ok(!name.endsWith(".data"), `${name} should not be in the tarball`);
        }
    });

    it("stays inside the size budget unpacked", { skip: !hasBuiltEngine() }, () => {
        // 25 MB unpacked is roughly three times the current engine, enough
        // headroom that a legitimate change does not trip it and an accident
        // — a debug build, a preload, a second binary — does.
        const result = npmPackDryRun(WASM_DIR);
        const unpacked = result.unpackedSize ?? result.size;
        assert.ok(
            unpacked < 25 * 1024 * 1024,
            `the package would unpack to ${Math.round(unpacked / 1024 / 1024)}MB, over the 25MB budget`
        );
    });
});

// The glue is a contract, not an implementation detail: index.mjs imports it
// as a default-exported factory and reaches the engine through four C ABI
// symbols with ccall. Both halves are checked against the file the build
// produced, so a glue emitted with a different MODULARIZE shape or a renamed
// export fails here rather than in a caller's first run.
describe("@guchho/wasm glue", () => {
    // The four exports CMakePresets.json links in (-sEXPORTED_FUNCTIONS) and
    // src/cli/wasm_abi.cpp defines. The glue has to publish each one on the
    // module object — ccall looks up Module["_" + name] — or every call
    // through it fails with a "not a function" that names nothing useful.
    const GLUE_SYMBOLS = [
        "_guchho_build",
        "_guchho_last_error",
        "_guchho_service",
        "_guchho_version",
    ];

    it("default-exports the module factory as a function", { skip: !hasBuiltEngine() }, async () => {
        const glueUrl = pathToFileURL(path.join(WASM_DIR, "lib", "guchho.js")).href;
        const glue = await import(glueUrl);
        assert.equal(
            typeof glue.default,
            "function",
            "lib/guchho.js should default-export the MODULARIZE factory"
        );
    });

    it("publishes the four _guchho_* C ABI symbols on the module", { skip: !hasBuiltEngine() }, () => {
        const source = fs.readFileSync(path.join(WASM_DIR, "lib", "guchho.js"), "utf8");
        for (const symbol of GLUE_SYMBOLS) {
            assert.ok(
                source.includes(`Module["${symbol}"]`),
                `the glue should bind ${symbol} onto the module object`
            );
        }
    });
});

describe("@guchho/wasm engine", () => {
    // Loaded once for the whole file: instantiating the module reads and
    // compiles the wasm binary, which is the slow part, and the version test
    // does not need one of its own. The other tests create their own because
    // they need different files or a different working directory.
    let sharedEngine = null;

    async function engine() {
        if (!sharedEngine) {
            sharedEngine = makeEngine();
        }
        return sharedEngine;
    }

    it("reports the version the manifest claims", { skip: !hasBuiltEngine() }, async () => {
        const e = await engine();
        assert.equal(e.version(), manifest.version);
    });

    it("lexes source through the in-process service", { skip: !hasBuiltEngine() }, async () => {
        const e = await engine();

        const result = await e.lexer("<a>\n<b>", { language: "html" });
        assert.deepEqual(result.errors, [], `unexpected errors: ${JSON.stringify(result.errors)}`);
        assert.equal(result.count, 4);
        assert.equal(result.tokens[0].kind, "start-tag");
        assert.equal(result.tokens[0].value, "a");
    });

    it("lexes bytes through the __bytes__ envelope", { skip: !hasBuiltEngine() }, async () => {
        const e = await engine();

        const result = await e.lexer(Buffer.from("<p>x</p>", "utf8"), { language: "html" });
        assert.equal(result.count, result.tokens.length);
        assert.ok(result.count > 0, "a byte input should still lex");
    });

    it("parses and prints a tree through one handle", { skip: !hasBuiltEngine() }, async () => {
        const e = await engine();

        const parsed = await e.parse("const a = 1;", { language: "js" });
        assert.equal(typeof parsed.ast, "number");
        assert.equal(parsed.ok, true);

        const printed = await e.print(parsed.ast, { language: "js" });
        assert.match(printed.code, /const a = 1/);
    });

    it("answers a raw service request", { skip: !hasBuiltEngine() }, async () => {
        const e = await engine();

        const response = await e.request({ command: "lex-js", input: "let a = 1;" });
        assert.equal(response.count, response.tokens.length);
        assert.ok(response.count > 0);
    });

    it("builds a small project from a files map and answers with the outputs", { skip: !hasBuiltEngine() }, async () => {
        // The files map has to be given at creation: on Node the engine mounts
        // the host file system unless a files map says otherwise, and a map
        // handed over afterwards would land in MEMFS while the build looked on
        // the mounted disk. Passing it up front is the browser path and the
        // in-memory sandbox path, which is what this exercises.
        const e = await makeEngine({
            files: {
                "smoke/src/index.html":
                    '<!doctype html><html><head><link rel="stylesheet" href="./style.css"></head>' +
                    '<body><script type="module" src="./main.js"></script></body></html>',
                "smoke/src/main.js": 'console.log("hello from the wasm engine");\n',
                "smoke/src/style.css": "body { color: rebeccapurple; }\n",
            },
        });

        const code = e.callMain([
            "build",
            "smoke/src/index.html",
            "--outdir=smoke/dist",
            "--log-level=error",
        ]);
        assert.equal(code, 0, "the build should succeed");

        const out = e.FS.readdir("smoke/dist");
        assert.ok(out.includes("index.html"), `smoke/dist should hold index.html, has ${out.join(", ")}`);
        assert.ok(out.includes("assets"), `smoke/dist should hold an assets directory, has ${out.join(", ")}`);

        const html = e.FS.readFile("smoke/dist/index.html", { encoding: "utf8" });
        assert.match(html, /assets\/main-[A-Z0-9]+\.js/, "the HTML should reference the hashed script");
        assert.match(html, /assets\/style-[A-Z0-9]+\.css/, "the HTML should reference the hashed stylesheet");

        const assets = e.FS.readdir("smoke/dist/assets");
        const script = assets.find((name) => name.startsWith("main-") && name.endsWith(".js"));
        const style = assets.find((name) => name.startsWith("style-") && name.endsWith(".css"));
        assert.ok(script, `assets should hold the hashed script, has ${assets.join(", ")}`);
        assert.ok(style, `assets should hold the hashed stylesheet, has ${assets.join(", ")}`);

        const scriptText = e.FS.readFile(`smoke/dist/assets/${script}`, { encoding: "utf8" });
        assert.match(scriptText, /hello from the wasm engine/, "the script should carry the module's source");
    });

    it("builds from a real directory through NODEFS", { skip: !hasBuiltEngine() }, async () => {
        // The files-map path above never touches the host disk. This one goes
        // the other way: a project written to a temporary directory, mounted
        // through NODEFS the way the CLI mounts the working directory, with
        // the outputs landing back on the real disk.
        //
        // The project carries its own empty config so config discovery — which
        // walks up the directory tree and stops at the first config it
        // finds — cannot pick up a guchho.config.js from a parent of the
        // temp directory. (On Windows os.tmpdir() sits under the user's home,
        // so a home-directory config is a real possibility.) Without this the
        // engine would try to spawn Node to evaluate that config, which the
        // WASM build cannot do, and the build would fail for a reason this
        // test is not about.
        const project = makeTempDir("wasm-nodefs");
        fs.mkdirSync(path.join(project, "src"), { recursive: true });
        fs.writeFileSync(path.join(project, "guchho.config.json"), "{}\n");
        fs.writeFileSync(
            path.join(project, "src", "index.html"),
            '<!doctype html><html><body><script type="module" src="./main.js"></script></body></html>\n'
        );
        fs.writeFileSync(path.join(project, "src", "main.js"), 'console.log(42);\n');

        const e = await makeEngine({ cwd: project });
        const code = e.callMain(["build", "src/index.html", "--outdir=dist", "--log-level=error"]);
        assert.equal(code, 0, "the build should succeed");

        assert.ok(fs.existsSync(path.join(project, "dist", "index.html")), "index.html should be on the real disk");
        const assets = fs.readdirSync(path.join(project, "dist", "assets"));
        assert.ok(
            assets.some((name) => name.startsWith("main-") && name.endsWith(".js")),
            `dist/assets should hold the hashed script, has ${assets.join(", ")}`
        );
    });

    it("packs and unpacks inside the engine's filesystem", { skip: !hasBuiltEngine() }, async () => {
        const e = await makeEngine({
            files: {
                "archive/src/app.js": 'console.log("packed");\n',
                "archive/src/index.html": "<!doctype html><p>hi</p>\n",
            },
        });

        const packed = await e.pack({
            inputs: ["archive/src"],
            outFile: "archive/release.zip",
            level: 9,
            date: new Date("2024-01-02T03:04:05Z"),
        });
        assert.equal(packed.path, "archive/release.zip");
        assert.ok(packed.size > 0, "the archive should have a size");
        assert.deepEqual(packed.warnings, []);

        const bytes = e.FS.readFile("archive/release.zip");
        assert.deepEqual(
            Array.from(bytes.subarray(0, 4)),
            [0x50, 0x4b, 0x03, 0x04],
            "the archive should carry the ZIP local-file signature"
        );

        const unpacked = await e.unpack({ archive: "archive/release.zip", outDir: "archive/check" });
        assert.equal(unpacked.outDir, "archive/check");
        assert.deepEqual([...unpacked.files].sort(), ["src/app.js", "src/index.html"]);
        assert.deepEqual(unpacked.warnings, []);

        const app = e.FS.readFile("archive/check/src/app.js", { encoding: "utf8" });
        assert.match(app, /packed/, "the extracted file should keep its contents");
    });

    it("packs and unpacks bytes without naming an archive path", { skip: !hasBuiltEngine() }, async () => {
        const e = await makeEngine({
            files: { "bytes/src/app.js": 'console.log("from bytes");\n' },
        });

        const packed = await e.packBytes({ inputs: ["bytes/src"], format: "tar.gz" });
        assert.ok(packed.bytes instanceof Uint8Array);
        assert.deepEqual(Array.from(packed.bytes.subarray(0, 2)), [0x1f, 0x8b], "gzip magic");
        assert.deepEqual(packed.warnings, []);

        const unpacked = await e.unpackBytes(packed.bytes, { format: "tar.gz" });
        assert.deepEqual(Object.keys(unpacked.files), ["src/app.js"]);
        const text = new TextDecoder().decode(unpacked.files["src/app.js"]);
        assert.match(text, /from bytes/);
        assert.deepEqual(unpacked.warnings, []);
    });

    it("validates pack and unpack options before reaching the engine", { skip: !hasBuiltEngine() }, async () => {
        const e = await engine();

        assert.throws(() => e.pack({ inputs: [], outFile: "a.zip" }), TypeError);
        assert.throws(() => e.pack({ inputs: ["a"], outFile: "" }), TypeError);
        assert.throws(() => e.pack({ inputs: ["a"], outFile: "a.zip", format: "rar" }), RangeError);
        assert.throws(() => e.pack({ inputs: ["a"], outFile: "a.zip", level: 10 }), RangeError);

        assert.throws(() => e.unpack({}), TypeError);
        assert.throws(() => e.unpack({ archive: "a.zip", stripComponents: -1 }), RangeError);
        assert.throws(() => e.unpackBytes("not bytes"), TypeError);
        assert.throws(() => e.packBytes({ inputs: ["a"], format: "rar" }), RangeError);
    });

    it("surfaces a syntax error with the file and line it is in", { skip: !hasBuiltEngine() }, async () => {
        const diagnostics = [];
        const e = await makeEngine({
            files: {
                "broken/src/index.html":
                    '<!doctype html><html><body><script type="module" src="./bad.js"></script></body></html>',
                "broken/src/bad.js": "const = ;\n",
            },
            printErr: (line) => diagnostics.push(line),
        });

        const code = e.callMain(["build", "broken/src/index.html", "--outdir=broken/dist", "--log-level=error"]);
        assert.notEqual(code, 0, "a syntax error should be a failed build");

        const text = diagnostics.join("\n");
        assert.match(text, /bad\.js/, "the diagnostic should name the file");
        assert.match(text, /bad\.js:1:\d+/, "the diagnostic should carry file:line:column");
    });
});
