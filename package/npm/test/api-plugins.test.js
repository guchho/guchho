// Plugins, reached through build() and context().
//
//     import { build } from "guchho";
//     await build({ entrypoints: ["src/main.js"], plugins: [myPlugin] });
//
// A plugin is written in this process and run in the service child, so the thing
// worth checking here is the round trip: a hook written against Rollup's shape
// really does see the source the engine sees and its answer really does reach
// the build. Each test names what the plugin did rather than only what the
// output contained, because an output that came out right by accident — a
// resolveId that was never called, a load that never ran — is the failure this
// file exists to catch.
//
// The imports are from the package's own lib rather than from "guchho", for the
// same reason as api-build.test.js: the bare name is resolved in e2e.test.js.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");

const { cleanup, skipWithoutBinary } = require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };

function makeProject(files = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "guchho-plugins-test-"));
  const sources = { "src/main.js": "const answer = 42;\nconsole.log(answer);\n", ...files };

  for (const [relative, contents] of Object.entries(sources)) {
    const file = path.join(root, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, contents);
  }

  return root;
}

function textOf(result, ending) {
  const file = result.outputFiles.find((f) => f.path.endsWith(ending));
  return file === undefined ? undefined : file.text;
}

describe("plugins in build()", () => {
  it("runs a virtual module through resolveId and load", needsBinary, async () => {
    const root = makeProject({ "src/main.js": 'import { greeting } from "virtual:greeting";\nconsole.log(greeting);\n' });

    const calls = [];
    const plugin = {
      name: "virtual",
      resolveId(source, importer) {
        calls.push(["resolveId", source, importer]);
        if (source === "virtual:greeting") return { id: "\0virtual:greeting", namespace: "virtual" };
        return null;
      },
      load(id) {
        calls.push(["load", id]);
        if (id === "\0virtual:greeting") return 'export const greeting = "hello from a plugin";';
        return null;
      },
    };

    const result = await guchho.build({
      absWorkingDir: root,
      entrypoints: ["src/main.js"],
      write: false,
      bundle: true,
      format: "esm",
      plugins: [plugin],
    });

    assert.deepEqual(result.errors, [], `build failed: ${result.errors.map((e) => e.text).join("\n")}`);
    assert.ok(
      textOf(result, "main.js").includes("hello from a plugin"),
      "the virtual module's source should be in the bundle"
    );

    const resolved = calls.filter(([hook]) => hook === "resolveId").map(([, source]) => source);
    assert.ok(resolved.includes("virtual:greeting"), "resolveId should have been asked about the import");
    assert.ok(
      calls.some(([hook, id]) => hook === "load" && id === "\0virtual:greeting"),
      "load should have been asked for the resolved id"
    );
  });

  it("lets generateBundle rewrite the output and emit a file", needsBinary, async () => {
    const root = makeProject();

    const plugin = {
      name: "rewriter",
      generateBundle(outputOptions, bundle) {
        for (const file of Object.values(bundle)) {
          if (file.type === "chunk") file.code += "\n// touched by the rewriter";
        }
        this.emitFile({ type: "asset", fileName: "note.txt", source: "from the plugin" });
      },
    };

    const result = await guchho.build({
      absWorkingDir: root,
      entrypoints: ["src/main.js"],
      write: false,
      bundle: true,
      format: "esm",
      plugins: [plugin],
    });

    assert.deepEqual(result.errors, [], `build failed: ${result.errors.map((e) => e.text).join("\n")}`);
    assert.ok(
      textOf(result, "main.js").includes("touched by the rewriter"),
      "a code change made in generateBundle should reach the output"
    );
    assert.equal(textOf(result, "note.txt"), "from the plugin", "an emitted asset should be in the output");
  });

  it("fails the build with the message a hook threw", needsBinary, async () => {
    const root = makeProject();

    const plugin = {
      name: "boom",
      buildStart() {
        throw new Error("this plugin refuses to build");
      },
    };

    await assert.rejects(
      () =>
        guchho.build({
          absWorkingDir: root,
          entrypoints: ["src/main.js"],
          write: false,
          bundle: true,
          plugins: [plugin],
        }),
      (error) => {
        assert.ok(
          String(error.message).includes("this plugin refuses to build"),
          `the hook's own message should be the failure: ${error.message}`
        );
        return true;
      }
    );
  });

  it("leaves a build with no plugins exactly as it was", needsBinary, async () => {
    const root = makeProject();

    const result = await guchho.build({
      absWorkingDir: root,
      entrypoints: ["src/main.js"],
      write: false,
      bundle: true,
      format: "esm",
    });

    assert.deepEqual(result.errors, []);
    assert.ok(textOf(result, "main.js").includes("42"));
  });
});

describe("plugins in context()", () => {
  it("runs the hooks again on every rebuild and closeBundle on dispose", needsBinary, async () => {
    const root = makeProject();

    const builds = [];
    let starts = 0;
    let closes = 0;
    const plugin = {
      name: "counter",
      buildStart() {
        starts += 1;
      },
      load(id) {
        if (id.endsWith("main.js")) {
          builds.push("load");
          return "export const n = 1;\n";
        }
        return null;
      },
      closeBundle() {
        closes += 1;
      },
    };

    const ctx = await guchho.context({
      absWorkingDir: root,
      entrypoints: ["src/main.js"],
      write: false,
      bundle: true,
      format: "esm",
      plugins: [plugin],
    });

    try {
      await ctx.rebuild();
      await ctx.rebuild();
    } finally {
      await ctx.dispose();
    }

    assert.equal(starts, 2, "buildStart should run once per rebuild");
    assert.ok(builds.length >= 2, "load should run on each rebuild");
    assert.equal(closes, 1, "closeBundle should run once, on dispose");
  });
});
