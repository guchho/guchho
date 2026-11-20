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

  it("registers virtual modules from buildStart", needsBinary, async () => {
    const root = makeProject({
      "src/helper.js": 'export const helper = "helper";\n',
      "src/main.js":
        'import { value } from "virtual:config?raw";\nimport { returned } from "virtual:from-return";\nconsole.log(value, returned);\n',
    });
    const seen = [];

    const plugin = {
      name: "virtual-modules",
      buildStart() {
        this.addVirtualModule({
          id: "virtual:config",
          namespace: "virtual",
          suffix: "?raw",
          loader: "js",
          resolveDir: path.join(root, "src"),
          meta: { token: "from-meta" },
          code:
            'import { helper } from "./helper.js";\nexport const value = helper + "!";\n',
        });
        return {
          virtualModules: [
            {
              id: "virtual:from-return",
              code: 'export const returned = "returned";\n',
            },
          ],
        };
      },
      transform(code, id, info) {
        if (id === "virtual:config") {
          seen.push({ namespace: info.namespace, suffix: info.suffix, loader: info.loader, meta: info.meta });
        }
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
    const output = textOf(result, "main.js");
    assert.ok(
      output.includes('var helper = "helper"') && output.includes('var value = helper + "!"'),
      `the virtual module should use its resolveDir: ${output}`
    );
    assert.ok(textOf(result, "main.js").includes("returned"), "the returned virtual module should be bundled");
    assert.deepEqual(seen, [
      {
        namespace: "virtual",
        suffix: "?raw",
        loader: "js",
        meta: { token: "from-meta" },
      },
    ]);
  });

  it("reports duplicate virtual modules", needsBinary, async () => {
    const root = makeProject();

    const duplicate = (name) => ({
      name,
      buildStart() {
        return {
          virtualModules: [
            { id: "virtual:duplicate", namespace: "virtual", code: "export default 1;" },
          ],
        };
      },
    });

    await assert.rejects(
      () =>
        guchho.build({
          absWorkingDir: root,
          entrypoints: ["src/main.js"],
          write: false,
          bundle: true,
          format: "esm",
          plugins: [duplicate("first"), duplicate("second")],
        }),
      (error) =>
        (error.errors || []).some(
          (item) =>
            item.text.includes("Duplicate virtual module") &&
            item.text.includes('"first"') &&
            item.text.includes('"second"')
        )
    );
  });

  it("runs a transform hook before the source is parsed", needsBinary, async () => {
    const root = makeProject();

    const seen = [];
    const plugin = {
      name: "transformer",
      transform(code, id, info) {
        seen.push([id, info.namespace, info.loader]);
        return code.replace("42", "43");
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
    assert.ok(textOf(result, "main.js").includes("43"), "the transformed source should reach the bundle");
    assert.ok(!textOf(result, "main.js").includes("const answer = 42"), "the original value should be gone");
    assert.ok(seen.some(([id]) => id.endsWith("main.js")), "transform should see the module id");
    assert.ok(seen.some(([, , loader]) => loader === "js"), "transform should be told the loader");
  });

  it("chains transform hooks in plugin order and lets one decline", needsBinary, async () => {
    const root = makeProject();

    const calls = [];
    const first = {
      name: "first",
      transform(code) {
        calls.push("first");
        return code.replace("42", "43");
      },
    };
    const second = {
      name: "second",
      transform(code) {
        calls.push("second");
        return code.replace("43", "44");
      },
    };
    const third = {
      name: "third",
      transform() {
        calls.push("third");
        return null;
      },
    };

    const result = await guchho.build({
      absWorkingDir: root,
      entrypoints: ["src/main.js"],
      write: false,
      bundle: true,
      format: "esm",
      plugins: [first, second, third],
    });

    assert.deepEqual(result.errors, [], `build failed: ${result.errors.map((e) => e.text).join("\n")}`);
    assert.deepEqual(calls, ["first", "second", "third"]);
    assert.ok(textOf(result, "main.js").includes("44"), "the last replacement should reach the bundle");
  });

  it("resolves a path from a hook with the build's resolver", needsBinary, async () => {
    const root = makeProject({ "src/dep.js": "export const dep = 1;\n" });
    const seen = [];
    const plugin = {
      name: "asker",
      async transform(code, id) {
        const resolved = await this.resolve("./dep.js", id);
        seen.push(resolved === null ? null : resolved.id);
        return code;
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
    assert.equal(seen.length, 1, "the transform should have asked one resolve question");
    assert.ok(
      typeof seen[0] === "string" && seen[0].replace(/\\/g, "/").endsWith("/src/dep.js"),
      `this.resolve should return the engine's absolute path: ${seen[0]}`
    );
  });

  it("skips the calling plugin for custom resolve questions", needsBinary, async () => {
    const root = makeProject();
    const seen = [];
    const claim = (name, id) => ({
      name,
      resolveId(source) {
        return source === "virtual:shared" ? { id, namespace: "virtual" } : null;
      },
    });
    const asker = {
      ...claim("asker", "virtual:asker"),
      async transform(code, id) {
        const resolved = await this.resolve("virtual:shared", id);
        seen.push(resolved && resolved.id);
        return code;
      },
    };

    const result = await guchho.build({
      absWorkingDir: root,
      entrypoints: ["src/main.js"],
      write: false,
      bundle: true,
      format: "esm",
      plugins: [asker, claim("helper", "virtual:helper")],
    });

    assert.deepEqual(result.errors, [], `build failed: ${result.errors.map((e) => e.text).join("\n")}`);
    assert.deepEqual(seen, ["virtual:helper"], "custom: true should skip the asking plugin");
  });

  it("fails the build with the message a transform threw", needsBinary, async () => {
    const root = makeProject();
    const plugin = {
      name: "transform-boom",
      transform() {
        throw new Error("the transform refused this module");
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
          String(error.message).includes("the transform refused this module"),
          `the hook's own message should be the failure: ${error.message}`
        );
        return true;
      }
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
