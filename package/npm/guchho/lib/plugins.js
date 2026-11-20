"use strict";

const { version } = require("./info");

function isAbsolute(file) {
  return file.startsWith("/") || file.startsWith("\\") || /^[A-Za-z]:[\\/]/.test(file);
}

function separatorOf(value) {
  return value.includes("\\") && !value.includes("/") ? "\\" : "/";
}

function dirname(file) {
  const match = /^(.*)[\\/][^\\/]*$/.exec(file);
  if (match === null) return ".";
  if (match[1] === "" && /^[\\/]/.test(file)) return file[0];
  return match[1];
}

function join(dir, name) {
  if (dir === "" || dir === ".") return name;
  if (/[\\/]$/.test(dir)) return dir + name;
  return dir + separatorOf(dir) + name;
}

// The plugin host: the JavaScript side of the plugin bridge.
//
// A plugin is written in the host's process, but the build runs in a service
// child, so a hook is not a function call — it is the child stopping mid-build,
// sending a request up the pipe, and waiting. lib/service.js reads those
// requests and hands them here; this module is what turns one back into an
// ordinary hook call and turns the hook's answer back into a response.
//
// The shape is Rollup's on purpose. A plugin author writes resolveId(source,
// importer, options) and load(id), and everything in this file exists to make
// those signatures work against an engine that has its own, flatter, internal
// ones. The translation is in one direction only: nothing here knows what the
// engine's structs are called, and nothing in the C++ bridge knows what Rollup
// calls a hook.

// The hooks this host can answer, in the order a plugin's own list is filtered
// against them. A hook absent here is never advertised to the engine, so a plugin
// that has one is not asked to run it and pays no round trip for it.
const SUPPORTED_HOOKS = [
  "buildStart",
  "resolveId",
  "load",
  "transform",
  "buildEnd",
  "generateBundle",
  "writeBundle",
  "closeBundle",
];

// The order a plugin asked for. Rollup's three names, with anything else —
// including nothing — reading as the normal order.
const ORDER_PRE = "pre";
const ORDER_POST = "post";

function orderRank(plugin) {
  if (plugin.order === ORDER_PRE) return 0;
  if (plugin.order === ORDER_POST) return 2;
  return 1;
}

// A stable sort by the plugin's order, so that two plugins with the same order
// stay in the order the caller wrote them. The engine registers the descriptors
// in the order it receives them and runs matching hooks in that order, so this
// sort is what makes "pre" mean "before the others" rather than "before the ones
// that happened to be listed first".
function inPluginOrder(plugins) {
  return plugins
    .map((plugin, index) => ({ plugin, index }))
    .sort((a, b) => orderRank(a.plugin) - orderRank(b.plugin) || a.index - b.index)
    .map((entry) => entry.plugin);
}

// Whether a plugin list is grouped by configuration element — one inner array
// per build — rather than one flat list for a single build. A flat list starts
// with a plugin object; a grouped one starts with an array.
function isGrouped(plugins) {
  return (
    Array.isArray(plugins) && plugins.length > 0 && Array.isArray(plugins[0])
  );
}

// One byte buffer, however the value arrived. An output file's contents cross
// the wire as bytes, a hook may hand back a string, and both are the same thing
// by the time they are written.
function toBuffer(value) {
  if (Buffer.isBuffer(value)) return value;
  if (value instanceof Uint8Array) {
    return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
  }
  if (typeof value === "string") return Buffer.from(value, "utf8");
  if (value === undefined || value === null) return Buffer.alloc(0);
  return Buffer.from(String(value), "utf8");
}

// Whether an output file is code, which decides whether the bundle entry a hook
// sees is a "chunk" with a "code" string or an "asset" with a "source" buffer.
// The engine emits one list of files and does not label them, so the label is
// recovered from the name — which is exactly the rule the write step itself
// could not use, because it writes whatever bytes it is handed.
const CODE_EXTENSION = /\.(m?js|cjs|jsx|ts|tsx)$/i;

function bundleEntry(file) {
  if (CODE_EXTENSION.test(file.path)) {
    return {
      type: "chunk",
      fileName: file.path,
      code: file.contents.toString("utf8"),
      isEntry: false,
      importedBindings: {},
      modules: {},
    };
  }
  return { type: "asset", fileName: file.path, source: file.contents };
}

function entryContents(entry) {
  if (entry.type === "asset") return toBuffer(entry.source);
  if (typeof entry.code === "string") return Buffer.from(entry.code, "utf8");
  return toBuffer(entry.code);
}

// The context a hook is called with, and the only place a hook can reach the
// engine from inside a call. It is deliberately small: a hook that never touches
// the context — which is most of them — pays nothing for it.
function makeContext(plugin, state, watchMode, resolve) {
  return {
    pluginName: plugin.name,

    meta: {
      // Reported rather than omitted, because a plugin that switches on it is
      // otherwise reading undefined and taking the wrong branch silently.
      guchhoVersion: version,
      watchMode: watchMode === true,
    },

    // A warning is collected and travels back with the hook's answer, which is
    // how it reaches the engine's log and the caller's result.
    warn(message) {
      state.warnings.push(normalizeMessage(plugin.name, message, "warning"));
    },

    // Rollup's this.error throws, and a plugin relies on that: the statement
    // after it is unreachable. It is thrown here as an ordinary Error and caught
    // by handle(), which turns it into an error the build stops on.
    error(message) {
      throw new Error(typeof message === "string" ? message : messageOf(message));
    },

    // Files the plugin wants watched. A path added here travels back with the
    // hook's answer and is merged into the build's dependencies, so a module
    // whose contents came from somewhere other than its own file still triggers
    // a rebuild when that other file changes.
    addWatchFile(file) {
      if (typeof file === "string" && file !== "") state.watchFiles.push(file);
    },

    // Emitting a file is only meaningful while the bundle is being assembled,
    // and the only moment the engine hands it back is generateBundle. Emitted
    // assets are appended to the bundle that hook returns.
    emitFile(emitted) {
      if (state.emitted === null) {
        throw new Error(
          "this.emitFile is only supported during generateBundle in this version of guchho"
        );
      }
      if (emitted === null || typeof emitted !== "object" || emitted.type !== "asset") {
        throw new Error("this.emitFile currently supports assets only");
      }
      const name = emitted.fileName || emitted.name;
      if (typeof name !== "string" || name === "") {
        throw new Error("an emitted asset needs a fileName or a name");
      }
      const target =
        state.outDir !== null && !isAbsolute(name) ? join(state.outDir, name) : name;
      state.emitted.push({ path: target, contents: toBuffer(emitted.source) });
    },

    // Asks the service to resolve a specifier with the same plugin chain and
    // resolver the build is using. The question is nested inside the callback
    // that is currently running, so it carries the token that identifies that
    // hook's build; the service answers it without ending the callback.
    resolve,

    addVirtualModule(module) {
      state.virtualModules.push(normalizeVirtualModule(module));
    },
  };
}

function messageOf(value) {
  if (value instanceof Error) return value.message;
  if (value !== null && typeof value === "object" && typeof value.message === "string") {
    return value.message;
  }
  return String(value);
}

// Entry points a start hook returns, normalized to the one shape the service
// reads: { input, output }. The public spelling accepts the same three forms as
// the "entrypoints" option — a path, { in, out }, or [out, in] — because a
// plugin author already knows that vocabulary and should not have to learn a
// second one for this hook.
function normalizeStartEntries(entries) {
  if (!Array.isArray(entries)) return [];
  const normalized = [];
  for (const entry of entries) {
    if (typeof entry === "string") {
      if (entry !== "") normalized.push({ input: entry, output: "" });
      continue;
    }
    if (Array.isArray(entry) && entry.length === 2) {
      const [out, input] = entry;
      if (typeof input === "string" && input !== "") {
        normalized.push({ input, output: typeof out === "string" ? out : "" });
      }
      continue;
    }
    if (entry !== null && typeof entry === "object") {
      const input = entry.in !== undefined ? entry.in : entry.input;
      if (typeof input === "string" && input !== "") {
        const output = entry.out !== undefined ? entry.out : entry.output;
        normalized.push({ input, output: typeof output === "string" ? output : "" });
      }
    }
  }
  return normalized;
}

function normalizeVirtualModule(module) {
  if (module === null || typeof module !== "object" || Array.isArray(module)) {
    throw new TypeError("a virtual module must be an object");
  }
  if (typeof module.id !== "string" || module.id === "") {
    throw new TypeError("a virtual module needs a non-empty id");
  }
  const contents = module.code !== undefined ? module.code : module.contents;
  if (typeof contents !== "string" && !(contents instanceof Uint8Array)) {
    throw new TypeError(`virtual module ${module.id} needs string or Uint8Array code`);
  }
  const normalized = {
    id: module.id,
    code: toBuffer(contents),
    namespace:
      typeof module.namespace === "string" && module.namespace !== ""
        ? module.namespace
        : "virtual",
  };
  if (typeof module.suffix === "string" && module.suffix !== "") {
    normalized.suffix = module.suffix;
  }
  if (typeof module.loader === "string" && module.loader !== "") {
    normalized.loader = module.loader;
  }
  if (typeof module.resolveDir === "string" && module.resolveDir !== "") {
    normalized.resolveDir = module.resolveDir;
  }
  if (module.meta !== undefined) normalized.meta = module.meta;
  if (Array.isArray(module.watchFiles)) {
    normalized.watchFiles = module.watchFiles.filter((file) => typeof file === "string");
  }
  if (Array.isArray(module.watchDirs)) {
    normalized.watchDirs = module.watchDirs.filter((dir) => typeof dir === "string");
  }
  return normalized;
}

// The directory the bundle is being written to, inferred from the output paths
// the engine produced. Emitted file names are relative to this directory, the
// same way Rollup treats them, and without it a name like "template.xml" would
// be written beside the project instead of inside the output folder.
function commonOutputDir(files) {
  let common = null;
  for (const file of files) {
    const dir = dirname(file);
    if (common === null) {
      common = dir;
      continue;
    }
    while (dir !== common && !dir.startsWith(common + separatorOf(common))) {
      const parent = dirname(common);
      if (parent === common) return common;
      common = parent;
    }
  }
  return common;
}

// The watch paths a hook collected through this.addWatchFile, plus the ones it
// returned by name. Returned arrays stay arrays and absent ones stay absent, so
// the service's request for a build with no extra paths is unchanged.
function watchFields(state, result) {
  const files = state.watchFiles.slice();
  const dirs = state.watchDirs.slice();
  if (result !== null && typeof result === "object") {
    if (Array.isArray(result.watchFiles)) files.push(...result.watchFiles.filter((file) => typeof file === "string"));
    if (Array.isArray(result.watchDirs)) dirs.push(...result.watchDirs.filter((dir) => typeof dir === "string"));
  }
  const fields = {};
  if (files.length > 0) fields.watchFiles = files;
  if (dirs.length > 0) fields.watchDirs = dirs;
  return fields;
}

function normalizeMessage(pluginName, message, kind) {
  const text = messageOf(message);
  const id =
    message !== null && typeof message === "object" && typeof message.id === "string"
      ? message.id
      : `${pluginName}-${kind}`;
  return { id, text };
}

// An id that is a number in the protocol's sense: a whole number that fits a
// signed 32-bit integer. Anything else the engine reads as a string and names
// the file by, so numbers are left alone only when they are safe to send.
function pairList(value) {
  if (Array.isArray(value)) {
    const pairs = [];
    for (const item of value) {
      if (Array.isArray(item) && item.length >= 2) {
        pairs.push([String(item[0]), String(item[1])]);
      } else if (item !== null && typeof item === "object") {
        for (const [key, entry] of Object.entries(item)) pairs.push([key, String(entry)]);
      }
    }
    return pairs;
  }
  if (value === null || typeof value !== "object") return [];
  return Object.entries(value).map(([key, item]) => [key, String(item)]);
}

// The `this.resolve` function handed to one hook call.
//
// The host is installed on the service for the duration of the outer request,
// so there is exactly one place to send this nested question. The token came
// with the callback that is still in flight and names the build whose resolver
// should answer; without it the service would have to guess from a plugin name
// that may appear in several configuration elements.
function makeResolve(host, plugin, request) {
  return async function resolve(source, importer, options) {
    if (typeof source !== "string" || source === "") {
      throw new TypeError("this.resolve needs a non-empty source");
    }
    if (host.service === null || host.service === undefined) {
      throw new Error("this.resolve is not available in this transport");
    }
    const token = request !== null && typeof request === "object" ? request.resolveToken : undefined;
    if (typeof token !== "string" || token === "") {
      throw new Error("this.resolve is not available for this callback");
    }

    const question = options !== null && typeof options === "object" ? options : {};
    const kind =
      typeof question.kind === "string" && question.kind !== ""
        ? question.kind
        : question.isEntry === true
          ? "entry"
          : "import-statement";
    const from = typeof importer === "string" && importer !== "" ? importer : null;
    const resolveDir =
      typeof question.resolveDir === "string" && question.resolveDir !== ""
        ? question.resolveDir
        : from !== null
      ? dirname(from)
      : process.cwd();

    const response = await host.service.call({
      command: "pluginResolve",
      nested: true,
      token,
      plugin: plugin.name,
      source,
      importer: from,
      namespace:
        typeof question.namespace === "string" && question.namespace !== ""
          ? question.namespace
          : "file",
      resolveDir,
      kind,
      custom: question.custom !== false,
      options: {
        with: pairList(question.with),
      },
    });

    if (response === null || typeof response !== "object") return null;
    if (typeof response.id !== "string" || response.id === "") return null;

    const result = {
      id: response.id,
      moduleSideEffects: response.sideEffects !== false,
    };
    if (response.external === true) result.external = true;
    if (typeof response.namespace === "string" && response.namespace !== "") {
      result.namespace = response.namespace;
    }
    if (typeof response.suffix === "string" && response.suffix !== "") {
      result.suffix = response.suffix;
    }
    if (response.meta !== null && response.meta !== undefined) result.meta = response.meta;
    return result;
  };
}

/** Runs the plugins a caller attached to a build. */
class PluginHost {
  /**
   * @param {Array<object>|Array<Array<object>>} plugins The caller's plugin
   *   objects. A flat list runs one configuration; a list of lists is one group
   *   per configuration element, positionally aligned with the engine's builds.
   */
  constructor(plugins) {
    const list = Array.isArray(plugins) ? plugins : [plugins];
    this.grouped = isGrouped(list);
    const groups = this.grouped ? list : [list];
    for (const group of groups) {
      for (const plugin of group) {
        if (plugin === null || typeof plugin !== "object") {
          throw new TypeError("each plugin must be an object");
        }
        if (typeof plugin.name !== "string" || plugin.name === "") {
          throw new TypeError("each plugin must have a non-empty string name");
        }
      }
    }
    this.groups = groups.map((group) => inPluginOrder(group));
    // A flat host is one anonymous configuration, for callers that think in a
    // single list and for the requests that still carry one.
    this.plugins = this.grouped ? [] : this.groups[0];
  }

  /**
   * The plugins as the service is told about them: a name and the hooks it
   * should call. A hook that is not a function is not listed, and the service
   * does not register it — which is what keeps a plugin without a resolveId from
   * costing a round trip per import.
   *
   * A grouped host returns one array per configuration element, so that each
   * build receives only the plugins its element named; the engine keeps them
   * apart by the build's index.
   */
  descriptors() {
    const describe = (plugin) => ({
      name: plugin.name,
      hooks: SUPPORTED_HOOKS.filter((hook) => typeof plugin[hook] === "function"),
    });
    if (this.grouped) return this.groups.map((group) => group.map(describe));
    return this.plugins.map(describe);
  }

  // The plugin a callback is meant for. A grouped host is told which build is
  // calling, so a name that appears in two elements resolves to the right one;
  // a name with no group — or an ungrouped host — is looked up across all of
  // them, and a duplicate is a plugin wrote twice rather than two builds.
  _find(request) {
    const name =
      request !== null && typeof request === "object" ? request.plugin : undefined;
    if (this.grouped) {
      const index = request === null ? undefined : request.configIndex;
      if (Number.isInteger(index) && index >= 0) {
        const group = this.groups[index];
        return group === undefined
          ? undefined
          : group.find((candidate) => candidate.name === name);
      }
    }
    return this.groups
      .flat()
      .find((candidate) => candidate.name === name);
  }

  /**
   * Answers one callback request from the service.
   *
   * The request names a plugin and a hook, and the answer is the hook's result
   * translated back. A hook that throws becomes an error the build stops on; a
   * hook that returns nothing declines. Neither throws out of here: the service
   * is waiting on a response, and a rejection with no response would leave it
   * waiting forever.
   *
   * @param {{plugin: string, hook: string, args: any, configIndex?: number}} request
   * @returns {Promise<object>}
   */
  async handle(request) {
    const plugin = this._find(request);
    const hook = request !== null && typeof request === "object" ? request.hook : undefined;
    if (plugin === undefined || typeof plugin[hook] !== "function") return {};

    const args = request.args;
    const state = {
      warnings: [],
      emitted: null,
      outDir: null,
      watchFiles: [],
      watchDirs: [],
      virtualModules: [],
    };
    const watchMode =
      request !== null && typeof request === "object" ? request.watchMode : false;
    const context = makeContext(plugin, state, watchMode, makeResolve(this, plugin, request));

    try {
      return await this._run(plugin, hook, context, state, args);
    } catch (error) {
      return {
        errors: [{ id: `${plugin.name}-${hook}`, text: messageOf(error) }],
        warnings: state.warnings,
      };
    }
  }

  async _run(plugin, hook, context, state, args) {
    switch (hook) {
      case "buildStart": {
        const result = await plugin.buildStart.call(
          context,
          args !== null && typeof args === "object" ? args : {}
        );
        const response = { warnings: state.warnings };
        const resultObject = result !== null && typeof result === "object" ? result : {};
        const entries = normalizeStartEntries(resultObject.entries);
        if (entries.length > 0) response.entries = entries;
        if (resultObject.virtualModules != null) {
          if (!Array.isArray(resultObject.virtualModules)) {
            throw new TypeError("virtualModules must be an array");
          }
          for (const module of resultObject.virtualModules) {
            state.virtualModules.push(normalizeVirtualModule(module));
          }
        }
        if (state.virtualModules.length > 0) {
          response.virtualModules = state.virtualModules;
        }
        Object.assign(response, watchFields(state, result));
        return response;
      }

      case "resolveId": {
        const options = {
          kind: args.kind,
          isEntry: args.kind === "entry",
          with: Object.fromEntries(pairList(args.with)),
        };
        if (typeof args.custom === "boolean") options.custom = args.custom;
        if (typeof args.namespace === "string") options.namespace = args.namespace;
        if (args.meta !== null && args.meta !== undefined) options.meta = args.meta;

        const result = await plugin.resolveId.call(
          context,
          args.source,
          args.importer === null ? undefined : args.importer,
          options
        );

        const response = { warnings: state.warnings };
        Object.assign(response, watchFields(state, result));
        if (result === false) {
          response.external = true;
          return response;
        }
        if (typeof result === "string") {
          response.id = result;
          return response;
        }
        if (result !== null && typeof result === "object") {
          if (typeof result.id === "string") response.id = result.id;
          if (result.external === true) response.external = true;
          if (typeof result.namespace === "string") response.namespace = result.namespace;
          if (typeof result.suffix === "string") response.suffix = result.suffix;
          if (typeof result.moduleSideEffects === "boolean") {
            response.moduleSideEffects = result.moduleSideEffects;
          }
          if (result.meta !== undefined) response.meta = result.meta;
          return response;
        }
        return response; // declined
      }

      case "load": {
        const info = {
          namespace: args.namespace,
          suffix: args.suffix,
          with: Object.fromEntries(pairList(args.with)),
        };
        if (args.meta !== null && args.meta !== undefined) info.meta = args.meta;
        const result = await plugin.load.call(context, args.id, info);
        const response = { warnings: state.warnings };
        if (result === null || result === undefined) {
          Object.assign(response, watchFields(state, result));
          return response;
        }

        if (typeof result === "string" || result instanceof Uint8Array) {
          response.code = toBuffer(result);
          Object.assign(response, watchFields(state, result));
          return response;
        }
        if (typeof result !== "object") {
          Object.assign(response, watchFields(state, result));
          return response;
        }

        if (result.code === undefined) {
          Object.assign(response, watchFields(state, result));
          return response;
        }
        response.code = toBuffer(result.code);
        if (typeof result.resolveDir === "string") response.resolveDir = result.resolveDir;
        if (typeof result.loader === "string") response.loader = result.loader;
        if (result.meta !== undefined) response.meta = result.meta;
        Object.assign(response, watchFields(state, result));
        return response;
      }

      case "transform": {
        const info = {
          namespace: args.namespace,
          suffix: args.suffix,
          loader: args.loader,
          with: Object.fromEntries(pairList(args.with)),
        };
        if (args.meta !== null && args.meta !== undefined) info.meta = args.meta;

        const result = await plugin.transform.call(
          context,
          args.code,
          args.id,
          info
        );
        const response = { warnings: state.warnings };
        Object.assign(response, watchFields(state, result));
        if (result === null || result === undefined) return response;
        if (typeof result === "string" || result instanceof Uint8Array) {
          response.code = toBuffer(result);
          return response;
        }
        if (typeof result !== "object" || result.code === undefined) {
          return response;
        }
        response.code = toBuffer(result.code);
        if (typeof result.map === "string") response.map = result.map;
        if (result.meta !== undefined) response.meta = result.meta;
        return response;
      }

      case "buildEnd": {
        await plugin.buildEnd.call(context);
        return { warnings: state.warnings };
      }

      case "generateBundle":
      case "writeBundle": {
        // The engine hands back its files; they become the bundle object the
        // hook expects, keyed by file name. Whatever the hook leaves in the
        // object is what generateBundle returns, which is how a plugin rewrites
        // or drops an output.
        const bundle = {};
        for (const file of args.outputFiles) {
          bundle[file.path] = bundleEntry({ path: file.path, contents: toBuffer(file.contents) });
        }

        state.emitted = hook === "generateBundle" ? [] : null;
        state.outDir = commonOutputDir(Object.keys(bundle));

        const outputOptions = state.outDir === null ? {} : { dir: state.outDir };
        const isWrite = hook === "writeBundle";
        await plugin[hook].call(context, outputOptions, bundle, isWrite);

        const response = { warnings: state.warnings };
        if (hook === "generateBundle") {
          const files = Object.values(bundle).map((entry) => ({
            path: entry.fileName,
            contents: entryContents(entry),
          }));
          for (const emitted of state.emitted) {
            files.push({ path: emitted.path, contents: emitted.contents });
          }
          response.files = files;
        }
        return response;
      }

      case "closeBundle": {
        await plugin.closeBundle.call(context);
        return {};
      }

      default:
        return {};
    }
  }
}

/**
 * Makes a host for a plugin list, or null when there is nothing to host.
 *
 * Null rather than an empty host so that the absence of plugins is one thing to
 * check everywhere it matters — a build with no plugins sends no descriptor and
 * installs no host, and the service never sends a callback it does not expect.
 */
function makePluginHost(plugins) {
  if (plugins === null || plugins === undefined) return null;
  const list = Array.isArray(plugins) ? plugins : [plugins];
  if (list.length === 0) return null;
  // A grouped list whose every element is empty has nothing to host, the same
  // as a flat list would.
  if (isGrouped(list) && list.every((group) => group.length === 0)) return null;
  return new PluginHost(list);
}

/**
 * Runs one service call with a plugin host attached.
 *
 * The host is installed on the service for exactly the duration of the call and
 * put back afterwards, so a build that named plugins cannot leave them attached
 * to a later build that did not. The request carries the descriptors the service
 * turns into its own plugin registration — grouped, when the host keeps one
 * group per configuration element.
 */
async function callWithPlugins(service, host, request, raw = false) {
  const invoke = () => (raw ? service.request(request) : service.call(request));
  if (host === null) return invoke();

  if (host.grouped) request.pluginGroups = host.descriptors();
  else request.plugins = host.descriptors();
  const previous = service.pluginHost;
  const previousService = host.service;
  service.pluginHost = host;
  host.service = service;
  try {
    return await invoke();
  } finally {
    service.pluginHost = previous;
    host.service = previousService;
  }
}

module.exports = {
  PluginHost,
  makePluginHost,
  callWithPlugins,
  SUPPORTED_HOOKS,
};
