// Reading a project's plugins out of its config file, in the host process.
//
//     import banner from "@guchho/banner";
//     export default [{ build: { ... }, plugins: [banner({ ... })] }, ...]
//
// The engine reads the same file but cannot run a JavaScript plugin, so the
// host is the one that finds them. The shape that matters here is the root:
// one configuration object with a "plugins" key, or an array of them, each
// with its own "plugins". This file pins both, and pins that a plugin named
// by more than one element is handed over once.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const { loadConfigPlugins } = require("../guchho/lib/config.js");

const { makeTempDir, cleanup } = require("./helpers/workspace");

after(cleanup);

// A project whose config file is the given text. The "type": "module" line is
// what lets the config be written with "export default", which is how a config
// file in a project that ships ESM spells itself.
function projectWithConfig(configText) {
  const root = makeTempDir("config-plugins");
  fs.writeFileSync(path.join(root, "package.json"), JSON.stringify({ type: "module" }));
  fs.writeFileSync(path.join(root, "guchho.config.js"), configText);
  return root;
}

describe("loadConfigPlugins", () => {
  it("reads the plugins of a single object root", async () => {
    const root = projectWithConfig(
      `const alpha = { name: "alpha" };\n` +
        `export default { build: { entry: "src/main.js" }, plugins: [alpha] };\n`
    );

    const plugins = await loadConfigPlugins(root);

    assert.equal(plugins.length, 1);
    assert.equal(plugins[0].name, "alpha");
  });

  it("collects the plugins of every element of an array root, in order", async () => {
    const root = projectWithConfig(
      `export default [\n` +
        `  { build: { outfile: "a.js" }, plugins: [{ name: "alpha" }] },\n` +
        `  { build: { outfile: "b.js" }, plugins: [{ name: "beta" }] },\n` +
        `];\n`
    );

    const plugins = await loadConfigPlugins(root);

    assert.deepEqual(
      plugins.map((plugin) => plugin.name),
      ["alpha", "beta"]
    );
  });

  it("attaches a plugin named by several elements only once", async () => {
    const root = projectWithConfig(
      `export default [\n` +
        `  { build: { outfile: "a.js" }, plugins: [{ name: "banner" }] },\n` +
        `  { build: { outfile: "b.js" }, plugins: [{ name: "banner" }] },\n` +
        `];\n`
    );

    const plugins = await loadConfigPlugins(root);

    assert.equal(plugins.length, 1);
    assert.equal(plugins[0].name, "banner");
  });

  it("returns nothing when no configuration names a plugin", async () => {
    const root = projectWithConfig(`export default { build: { entry: "src/main.js" } };\n`);

    assert.deepEqual(await loadConfigPlugins(root), []);
  });
});
