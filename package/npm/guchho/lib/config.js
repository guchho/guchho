"use strict";

// Loading plugins out of a project's config file, for the JavaScript host.
//
// A plugin is a JavaScript function, and the engine is a separate process that
// cannot call one. The engine reads the config file too — that is how it learns
// a project's build options — but it reads it as JSON, so the "plugins" field
// arrives as data it can do nothing with. This module is the other half: it
// reads the same file in this process, where the functions are live, and hands
// them to the plugin bridge so the hooks can actually run.
//
// The discovery here mirrors the engine's own (src/resolver/guchho_json.cpp):
// walk up from the working directory, stop at the first directory holding a
// config file, and within one directory try "guchho.config.js", then
// "guchho.config.json", then "guchho.json". It has to match, because a config
// the engine found and this did not would run no hooks in a build that plainly
// names some.

const fs = require("fs");
const path = require("path");
const { createRequire } = require("module");
const { pathToFileURL } = require("url");

const CONFIG_NAMES = ["guchho.config.js", "guchho.config.json", "guchho.json"];

/**
 * The config file that governs "startDir", or null when there is none.
 *
 * @param {string} [startDir]
 * @returns {string|null} An absolute path.
 */
function findConfigFile(startDir) {
  let dir = path.resolve(startDir || process.cwd());
  for (;;) {
    for (const name of CONFIG_NAMES) {
      const candidate = path.join(dir, name);
      try {
        if (fs.statSync(candidate).isFile()) return candidate;
      } catch {
        // Not there, or not readable as a file; try the next name.
      }
    }
    const parent = path.dirname(dir);
    if (parent === dir) return null;
    dir = parent;
  }
}

// One "plugins" entry, as a live plugin object. A string is a path or module
// specifier resolved from the config's directory and imported; a function is a
// plugin factory and is called; an object is used as it is.
async function resolveEntry(entry, configDir) {
  if (typeof entry === "string") {
    const require = createRequire(path.join(configDir, "noop.js"));
    const resolved = require.resolve(entry);
    const mod = await import(pathToFileURL(resolved).href);
    const value = mod && "default" in mod ? mod.default : mod;
    return typeof value === "function" ? value() : value;
  }
  if (typeof entry === "function") return entry();
  return entry;
}

/**
 * The plugins a project's config file names, in the order it names them.
 *
 * The root may be one configuration object with a "plugins" key, or an array of
 * them, each with its own "plugins". Both are collected, in element order. A
 * plugin named more than once is kept only the first time, because the host
 * dispatches a hook by plugin name and so cannot tell two same-named plugins
 * apart anyway.
 *
 * @param {string} [startDir] Where discovery starts. Defaults to the process
 *   working directory, matching the engine's own starting point.
 * @returns {Promise<Array<object>>} Live plugin objects, or an empty list.
 */
async function loadConfigPlugins(startDir) {
  const configPath = findConfigFile(startDir);
  if (configPath === null) return [];

  const configDir = path.dirname(configPath);

  let root;
  if (configPath.endsWith(".json")) {
    root = JSON.parse(fs.readFileSync(configPath, "utf8"));
  } else {
    const mod = await import(pathToFileURL(configPath).href);
    root = mod && "default" in mod ? mod.default : mod;
  }

  // The root is either one configuration object, with a "plugins" key, or an
  // array of them, each with its own. The engine builds one configuration per
  // element (src/resolver/guchho_json.cpp), so the plugins are gathered from
  // every element in order. The host runs one flat list for the whole run, so a
  // plugin named by several elements is attached once.
  const roots = Array.isArray(root) ? root : [root];

  const plugins = [];
  const seen = new Set();
  for (const element of roots) {
    const listed =
      element === null || typeof element !== "object" ? undefined : element.plugins;
    if (listed === undefined || listed === null) continue;

    const entries = Array.isArray(listed) ? listed : [listed];
    for (const entry of entries) {
      const plugin = await resolveEntry(entry, configDir);
      if (plugin === null || plugin === undefined) continue;
      // Prefer the name as the key: the host answers a hook by name, so a
      // second plugin of the same name would never be reached. Fall back to
      // identity for an entry that carries no name, which the host rejects
      // later with a clear error.
      const key =
        typeof plugin === "object" && typeof plugin.name === "string" && plugin.name !== ""
          ? `name:${plugin.name}`
          : plugin;
      if (seen.has(key)) continue;
      seen.add(key);
      plugins.push(plugin);
    }
  }
  return plugins;
}

/**
 * The caller's plugins after the config file's.
 *
 * The config's plugins run first and the caller's after, so a caller can wrap
 * or override what the project set up. The result is null rather than an empty
 * list when there are none, because the plugin bridge reads null as "no host".
 *
 * @param {string} [startDir]
 * @param {Array|null} [callerPlugins]
 * @returns {Promise<Array<object>|null>}
 */
async function withConfigPlugins(startDir, callerPlugins) {
  const configPlugins = await loadConfigPlugins(startDir);
  const caller =
    callerPlugins === null || callerPlugins === undefined
      ? []
      : Array.isArray(callerPlugins)
        ? callerPlugins
        : [callerPlugins];
  const all = [...configPlugins, ...caller];
  return all.length === 0 ? null : all;
}

module.exports = { CONFIG_NAMES, findConfigFile, loadConfigPlugins, withConfigPlugins };
