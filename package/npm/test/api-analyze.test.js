// guchho.analyze(), reached the way a caller reaches it.
//
//     import { analyze } from "guchho";
//     const report = await analyze({ entryPoints: ["src/main.js"] });
//
// analyze takes a configuration and answers a question about it: what went in,
// what came out, and what was left as an import. It used to take a metafile and
// return a text report instead, which is still available as analyzeMetafile.
//
// The claim held here is that the numbers come from the bundler's own graph. A
// scanner walking the tree would agree on a small project and quietly disagree
// on any project that resolves through tsconfig paths, package exports or an
// extension it guessed wrong — and it disagrees by listing files the build never
// read, which is the direction that makes an analysis worth having. So the test
// that matters is the one below that puts a file in the project which the build
// does NOT read, and checks that the analysis does not pretend it did.
//
// Two things about guchho's metafile shape the tests below rely on, both found
// by reading a real one rather than by assuming esbuild's:
//
//   - the top-level "inputs" names the entry points and nothing else. The
//     transitive set is the contribution map on each output, under its own
//     "inputs". A summary built from the wrong one of those reports one file and
//     is confident about it.
//
//   - an output's "imports" are what the built code still requires at runtime.
//     bundle:false is the default, so under it a relative sibling import is
//     listed and left alone on purpose. Those are dependencies, and an analysis
//     that filtered them out for not being bare specifiers would report an empty
//     list for a bundle that plainly has some.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");
const { summarize } = require("../guchho/lib/analyze");

const { cleanup, skipWithoutBinary } = require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };

// A project with one entry point that imports a sibling, and an unused file
// sitting next to it.
function makeProject(files = {}) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "guchho-analyze-test-"));
    const sources = {
        "src/main.js": 'import { helper } from "./helper.js";\nconsole.log(helper());\n',
        "src/helper.js": "export function helper() {\n  return 42;\n}\n",
        "src/unused.js": "export const nobody = true;\n",
        ...files,
    };

    for (const [relative, contents] of Object.entries(sources)) {
        const file = path.join(root, relative);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, contents);
    }

    return root;
}

describe("analyze()", () => {
    it("answers with inputs, outputs, dependencies and a total", needsBinary, async () => {
        const root = makeProject();

        const report = await guchho.analyze({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
        });

        assert.deepEqual(Object.keys(report).sort(), [
            "dependencies",
            "inputs",
            "outputs",
            "totalBytes",
        ]);
        assert.ok(report.inputs.length > 0, "the entry point should be an input");
        assert.ok(report.outputs.length > 0, "a bundle should be an output");
        assert.ok(report.totalBytes > 0, "the output tree should not be empty");
    });

    it("reports the transitive imports, not just the entry point", needsBinary, async () => {
        // The reason to ask the bundler rather than read the entry point: helper.js
        // is not named anywhere a caller would look, and under bundle:true it is
        // inlined — so the only record that it was read is the contribution map.
        const root = makeProject();

        const report = await guchho.analyze({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            bundle: true,
        });

        const paths = report.inputs.map((input) => input.path);
        assert.ok(
            paths.some((file) => file.endsWith("main.js")),
            `entry point missing from ${JSON.stringify(paths)}`
        );
        assert.ok(
            paths.some((file) => file.endsWith("helper.js")),
            `imported sibling missing from ${JSON.stringify(paths)}`
        );

        // Summed across the output, so each file is reported once at the size it
        // contributed rather than twice at a partial amount.
        const helper = report.inputs.find((input) => input.path.endsWith("helper.js"));
        assert.ok(helper.bytes > 0, "an inlined file should have contributed bytes");
    });

    it("does not list a file the build never read", needsBinary, async () => {
        // The test the whole design rests on. A tree-walking scanner reports
        // unused.js because it is in the directory; the bundler does not, because
        // nothing imports it.
        const root = makeProject();

        const report = await guchho.analyze({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            bundle: true,
        });

        assert.ok(
            !report.inputs.some((input) => input.path.endsWith("unused.js")),
            "a file nothing imports should not be reported as an input"
        );
    });

    it("lists what the output still imports as dependencies", needsBinary, async () => {
        // bundle:false is the default, and under it the sibling is left as an
        // import on purpose. The built code needs it at runtime, so it is a
        // dependency — whether or not it is a bare specifier.
        const root = makeProject();

        const report = await guchho.analyze({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
        });

        assert.deepEqual(report.dependencies, ["./helper.js"]);
        assert.ok(
            !report.inputs.some((input) => input.path === "./helper.js"),
            "an import left in the output is not a file that was read"
        );
    });

    it("has no dependencies left once everything is inlined", needsBinary, async () => {
        // The same project as above, bundled. The sibling is gone from the output,
        // so it must be gone from the dependency list — otherwise the list is
        // describing the source tree rather than the build.
        const root = makeProject();

        const report = await guchho.analyze({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            bundle: true,
        });

        assert.deepEqual(report.dependencies, []);
    });

    it("lists an external package as a dependency", needsBinary, async () => {
        const root = makeProject({
            "src/main.js": 'import { readFileSync } from "node:fs";\nconsole.log(readFileSync);\n',
        });

        const report = await guchho.analyze({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            external: ["node:fs"],
            bundle: true,
        });

        assert.ok(
            report.dependencies.includes("node:fs"),
            `node:fs should be a dependency, got ${JSON.stringify(report.dependencies)}`
        );
        assert.ok(
            !report.inputs.some((input) => input.path === "node:fs"),
            "an external package is not a file that was read"
        );
    });

    it("sorts dependencies, so two runs answer identically", needsBinary, async () => {
        const root = makeProject({
            "src/main.js": 'import a from "zzz";\nimport b from "aaa";\nconsole.log(a, b);\n',
        });

        // bundle:true because "external" is only meaningful with it — the engine
        // refuses the combination rather than quietly ignoring one half, which is
        // the right answer and worth writing down here rather than rediscovering.
        const options = {
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            external: ["zzz", "aaa"],
            bundle: true,
        };

        const first = await guchho.analyze(options);
        const second = await guchho.analyze(options);

        assert.deepEqual(first.dependencies, ["aaa", "zzz"]);
        assert.deepEqual(second.dependencies, first.dependencies);
    });

    it("writes nothing", needsBinary, async () => {
        // The whole point of asking a question instead of running a build: no
        // outdir appears, and the project is left exactly as it was found.
        const root = makeProject();

        await guchho.analyze({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
        });

        assert.deepEqual(
            fs.readdirSync(root).sort(),
            ["src"],
            "an analysis should not leave a dist directory behind"
        );
    });

    it("ignores a write:true in the configuration", needsBinary, async () => {
        // Forced rather than honoured. A caller who passed write:true wants
        // outputs, not an analysis, and following it would be the side effect
        // this function exists to avoid.
        const root = makeProject();

        await guchho.analyze({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            write: true,
        });

        assert.deepEqual(fs.readdirSync(root).sort(), ["src"]);
    });

    it("takes the nested spelling as well as the flat one", needsBinary, async () => {
        const root = makeProject();

        const flat = await guchho.analyze({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
        });
        const nested = await guchho.analyze({
            build: { absWorkingDir: root, entry: "src/main.js" },
        });

        assert.deepEqual(
            nested.outputs.map((output) => output.path),
            flat.outputs.map((output) => output.path)
        );
    });

    it("does not modify the configuration it was given", needsBinary, async () => {
        // write and metafile are forced, so this is where a naive
        // Object.assign(normalized, ...) would quietly rewrite a caller's object
        // and leave write:true in it for the build they run next.
        const root = makeProject();
        const config = { absWorkingDir: root, entryPoints: ["src/main.js"] };

        await guchho.analyze(config);

        assert.deepEqual(config, { absWorkingDir: root, entryPoints: ["src/main.js"] });
    });

    it("reports a build that failed as the failure it is", needsBinary, async () => {
        // An analysis of a build that did not happen is an analysis of nothing.
        // Throwing says the configuration is wrong; an empty report says the
        // project has no files, and a caller reading that would ship nothing.
        const root = makeProject({ "src/main.js": "const = ;\n" });

        await assert.rejects(
            () => guchho.analyze({ absWorkingDir: root, entryPoints: ["src/main.js"] }),
            (error) => {
                assert.ok(error instanceof Error);
                assert.ok(error.errors.length > 0, "the failure should carry its diagnostics");
                return true;
            }
        );
    });
});

describe("summarize()", () => {
    // The pure half, checked without a binary so that the shaping rules hold on
    // a machine that has never run a build.

    // Shaped like a real one: a bundle that inlined both files, so the
    // contribution map on the output is the transitive set, and the top-level
    // inputs names only the entry point.
    const METAFILE = {
        inputs: {
            "src/main.js": { bytes: 23, format: "esm", imports: [] },
        },
        outputs: {
            "dist/main.js": {
                bytes: 91,
                imports: [],
                exports: [],
                entryPoint: "src/main.js",
                inputs: {
                    "src/main.js": { bytesInOutput: 23 },
                    "src/helper.js": { bytesInOutput: 35 },
                },
            },
            "dist/main.js.map": {
                bytes: 1500,
                imports: [],
                exports: [],
                inputs: {
                    "src/main.js": { bytesInOutput: 23 },
                    "src/helper.js": { bytesInOutput: 35 },
                },
            },
        },
    };

    it("reads inputs, outputs and the total", () => {
        const report = summarize(METAFILE);

        assert.equal(report.outputs.length, 2);
        assert.equal(report.totalBytes, 1591);
    });

    it("reads the transitive set from the contribution map, not the entry points", () => {
        // The top-level inputs has one file. The answer has two, because that is
        // what the build read — and reading the wrong one of the two maps would
        // report one file and be confident about it.
        const report = summarize(METAFILE);
        const paths = report.inputs.map((input) => input.path);

        assert.equal(paths.length, 2);
        assert.ok(paths.includes("src/main.js"));
        assert.ok(paths.includes("src/helper.js"));
    });

    it("sums a file's contribution across every output", () => {
        // 23 + 23 across the bundle and its map. Reported once at the total rather
        // than twice at a partial amount.
        const main = summarize(METAFILE).inputs.find((input) => input.path === "src/main.js");
        assert.equal(main.bytes, 46);
    });

    it("takes the format from the top-level inputs", () => {
        const main = summarize(METAFILE).inputs.find((input) => input.path === "src/main.js");
        assert.equal(main.format, "esm");
    });

    it("falls back to the entry points when there is no contribution map", () => {
        // Smaller, but honest — and better than an empty list that reads as "this
        // build read nothing".
        const report = summarize({
            inputs: { "src/main.js": { bytes: 23, format: "esm" } },
            outputs: { "dist/main.js": { bytes: 23, imports: [] } },
        });

        assert.deepEqual(report.inputs, [{ path: "src/main.js", bytes: 23, format: "esm" }]);
    });

    it("adds up the whole output tree", () => {
        // 91 + 1500. The source map is the bigger half, and a total that left it
        // out would under-report by more than the bundle.
        assert.equal(summarize(METAFILE).totalBytes, 1591);
    });

    it("lists an output's remaining imports as dependencies, relative ones included", () => {
        // bundle:false is the default and under it "./helper.js" is left on
        // purpose. Filtering it out for not being a bare specifier would report an
        // empty dependency list for a bundle that plainly has some.
        const report = summarize({
            inputs: { "src/main.js": { bytes: 61 } },
            outputs: {
                "dist/main.js": {
                    bytes: 61,
                    imports: [{ path: "./helper.js", kind: "import-statement", external: true }],
                    inputs: { "src/main.js": { bytesInOutput: 61 } },
                },
            },
        });

        assert.deepEqual(report.dependencies, ["./helper.js"]);
    });

    it("deduplicates a dependency several outputs share", () => {
        const report = summarize({
            inputs: {},
            outputs: {
                "dist/a.js": { bytes: 1, imports: [{ path: "react", external: true }] },
                "dist/b.js": { bytes: 1, imports: [{ path: "react", external: true }] },
            },
        });

        assert.deepEqual(report.dependencies, ["react"]);
    });

    it("sorts dependencies", () => {
        const report = summarize({
            inputs: {},
            outputs: {
                "dist/a.js": { bytes: 1, imports: [{ path: "zzz" }, { path: "aaa" }] },
            },
        });

        assert.deepEqual(report.dependencies, ["aaa", "zzz"]);
    });

    it("accepts JSON text as well as a parsed metafile", () => {
        assert.deepEqual(summarize(JSON.stringify(METAFILE)), summarize(METAFILE));
    });

    it("answers an empty report for an absent metafile", () => {
        // The build that produced it succeeded. A caller asking what it contained
        // deserves "nothing", not an exception about a document they may never
        // have asked for.
        for (const absent of [undefined, null, "{}", "not json at all", 42, []]) {
            assert.deepEqual(summarize(absent), {
                inputs: [],
                outputs: [],
                dependencies: [],
                totalBytes: 0,
            });
        }
    });

    it("skips entries that are not objects", () => {
        // A metafile is trusted to be well formed, but one that is not should
        // cost the caller the bad entry rather than a TypeError from inside a
        // summary it was only asking about.
        const report = summarize({
            inputs: { "a.js": { bytes: 1 }, "broken.js": null },
            outputs: { "dist/a.js": { bytes: 2 }, "broken.js": "nope" },
        });

        assert.equal(report.inputs.length, 1);
        assert.equal(report.outputs.length, 1);
        assert.equal(report.totalBytes, 2);
    });

    it("defaults a missing size to zero rather than to undefined", () => {
        // A caller summing result.outputs[].size gets a number. undefined would
        // turn that sum into NaN, which is not a number any caller can report.
        const report = summarize({
            inputs: { "a.js": {} },
            outputs: { "dist/a.js": { inputs: { "a.js": {} } } },
        });

        assert.equal(report.inputs[0].bytes, 0);
        assert.equal(report.outputs[0].size, 0);
        assert.equal(report.totalBytes, 0);
    });

    it("reports a total that matches the outputs it lists", () => {
        // The invariant that makes totalBytes usable at all: it is the sum of the
        // list beside it, so a caller can either trust the total or recompute it.
        const report = summarize(METAFILE);
        const summed = report.outputs.reduce((total, output) => total + output.size, 0);

        assert.equal(report.totalBytes, summed);
    });
});

describe("analyzeMetafile()", () => {
    // The old analyze, kept under the name that says what it does.

    it("is still on the surface, and is not analyze", needsBinary, () => {
        assert.equal(typeof guchho.analyzeMetafile, "function");
        assert.notEqual(guchho.analyzeMetafile, guchho.analyze);
    });

    it("renders a metafile as text", needsBinary, async () => {
        const root = makeProject();

        const { metafile } = await guchho.build({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            write: false,
            metafile: true,
        });

        const text = await guchho.analyzeMetafile(metafile);

        assert.equal(typeof text, "string");
        assert.ok(text.length > 0, "a rendered metafile should not be empty");
    });
});
