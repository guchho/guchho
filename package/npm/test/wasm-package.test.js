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
            "bin/guchho.js",
            "lib/guchho.js",
            "lib/guchho.wasm",
            "README.md",
            "LICENSE",
        ]) {
            assert.ok(packed.has(required), `${required} would be missing from the tarball`);
        }
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
        const project = makeTempDir("wasm-nodefs");
        fs.mkdirSync(path.join(project, "src"), { recursive: true });
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
