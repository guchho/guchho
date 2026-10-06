"use strict";

// One configuration model, two spellings.
//
// A caller writes either the flat form the API has always published:
//
//     await build({ entrypoints: ["src/index.js"], outdir: "dist", minify: true });
//
// or the nested form a config file uses:
//
//     await build({ build: { entry: "src/index.js", outdir: "dist" },
//                   server: { port: 3000 },
//                   plugins: [myPlugin] });
//
// and both have to mean the same thing. This module is what makes them mean the
// same thing, and it is the only place either spelling is read.
//
// The flat form is canonical. The nested form is an alias, accepted and then
// flattened onto the flat one, so there is one internal representation and one
// set of rules for what an option is called — which is the whole reason to have
// this file rather than spreading the difference across build.js and context.js.
//
// What this deliberately does NOT do is supply a default.
//
// Every value below is either spelled out by the caller or absent. The ranking
// of an explicit option against a config file against a built-in default is the
// engine's business, and it lives in one place —
// api::ResolveEffectiveBuildConfigs, reached through the flag grammar. A default
// written here would be a second copy of the same fact in a second language, and
// the two would disagree the first time either changed. So normalize() never
// invents an option; it only decides which of two spellings the caller used.
//
// That is also what keeps precedence intact. An option this module renames, such
// as "entry", becomes the canonical name and nothing more: it arrives at the
// engine with the same explicitness it would have had under either spelling, so
// it outranks a config file exactly as it did before.

/**
 * The keys that mean "this is the nested spelling".
 *
 * A reserved set rather than a heuristic. The alternative — deciding by looking
 * for option names nobody would put at the top level — breaks the first time
 * the config file grows a section, and breaks silently, which is the worst way
 * to break. Naming the sections makes the decision explicit and keeps the flat
 * form working for everything not named here.
 */
const SECTIONS = ["build", "server", "watch", "plugins"];

// Everything under a nested "build" section is a build option. The "server" and
// "plugins" sections are not build options at all — they configure the dev
// server and the plugin list — so they are peeled off rather than merged, or a
// caller would get "--port=3000" sent to the build parser as an invalid flag.
//
// Returned separately, because serve() and watch() need them and build() must
// not see them.

/**
 * The canonical name for each alias the API accepts.
 *
 * An alias maps onto a canonical option rather than being a second option, so
 * the engine only ever sees one spelling of a thing. Two aliases that named
 * different options would be a bug here rather than a feature there.
 *
 * "entrypoints" is the canonical spelling because it is the one that survives
 * the trip: it matches the plural noun the usage line already used, the JSON
 * field the protocol puts on the wire, and the lowercasing every other
 * multiword option in this table follows.
 *
 * "entryPoints" is here because the API published that camel-cased spelling for
 * years, and a caller who has it in a working script should not be broken by a
 * rename. It is accepted and reaches the same place; it is not a second option.
 *
 * "entry" is here because one entry reads as "entry" and several read as
 * "entries", and a caller writing the nested form writes the singular. Both
 * spellings have always been accepted; they now reach the same place.
 */
const ALIASES = {
  entry: "entrypoints",
  entryPoints: "entrypoints",
  // "name" is the spelling a command line and a Rollup config use for the
  // global a wrapper format publishes; "globalName" is the camelCase the
  // rest of this schema spells it with. They are one option, reached
  // through whichever name the caller wrote.
  name: "globalName",
};

// The section names a nested configuration may carry, and what each one is for.
// "watch" is accepted as a section because a caller reading the flat options
// list expects somewhere to put it, and because the watcher takes a delay that
// belongs to no build option.
const SERVER_KEYS = new Set(["host", "port", "open", "servedir", "fallback"]);
const WATCH_KEYS = new Set(["delay", "interval"]);

function isPlainObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

/**
 * The sections a nested configuration carries.
 *
 * Empty for a flat configuration, which is the common case and the one that
 * should cost nothing.
 */
function sectionsOf(options) {
  const found = {};
  let nested = false;

  for (const key of SECTIONS) {
    if (options[key] === undefined) continue;
    nested = true;
    found[key] = options[key];
  }

  return nested ? found : null;
}

/**
 * Rejects a configuration that sets the same option twice under both spellings.
 *
 * A refusal rather than a merge, because either answer would be a guess. If
 * "outdir" and "build.outdir" disagree, one of them is a mistake and nothing
 * here can tell which; silently preferring one is how a project ends up writing
 * its output where nobody expected. Naming both is the whole value.
 */
function rejectConflicts(flat, nestedBuild, sectionName) {
  for (const key of Object.keys(nestedBuild)) {
    if (flat[key] === undefined) continue;
    throw new TypeError(
      `"${key}" is set twice: once at the top level and once under "${sectionName}". ` +
        `Use one spelling or the other — guchho will not guess which one you meant.`
    );
  }
}

/**
 * Folds a nested configuration onto the flat one.
 *
 * The three sections are handled separately because only one of them is a bag of
 * build options. "server" and "watch" configure things that are not the build,
 * and are returned as their own objects for serve() and watch() to use; putting
 * them in the flat bag would send them to the build grammar, which would refuse
 * them with an error about a flag the caller never wrote.
 */
function flatten(options) {
  const sections = sectionsOf(options);

  const nestedBuild =
    sections !== null && sections.build !== undefined ? sections.build : {};
  if (!isPlainObject(nestedBuild)) {
    throw new TypeError('"build" must be an object of build options');
  }

  if (sections !== null) {
    rejectConflicts(options, nestedBuild, "build");
  }

  // Everything that is not a section is a flat build option, and it outranks
  // nothing: the sections were checked for conflicts above, so what is left here
  // is either the whole configuration or nothing at all.
  const flat = {};
  for (const key of Object.keys(options)) {
    if (SECTIONS.includes(key)) continue;
    flat[key] = options[key];
  }
  Object.assign(flat, nestedBuild);

  const server =
    sections !== null && sections.server !== undefined
      ? readSection(sections.server, "server", SERVER_KEYS)
      : null;
  const watch = readWatch(sections, flat);
  const plugins = sections !== null && sections.plugins !== undefined
    ? readPlugins(sections.plugins)
    : null;

  return { build: flat, server, watch, plugins };
}

/**
 * Reads one section, refusing a key that is not one of its own.
 *
 * "servedir" rather than "servedir" as a typo of "serveDir", or "prot" instead
 * of "port": both are silently wrong if the typo lands, and a server on port 0
 * that the caller believes is on port 3000 is a confusing hour rather than an
 * error message. A key the section does not have is refused by name.
 */
function readSection(value, name, allowed) {
  if (!isPlainObject(value)) {
    throw new TypeError(`"${name}" must be an object`);
  }

  for (const key of Object.keys(value)) {
    if (!allowed.has(key)) {
      throw new TypeError(
        `"${key}" is not a "${name}" option. ${name} takes: ${[...allowed].join(", ")}.`
      );
    }
  }

  return Object.assign({}, value);
}

// The watch section is a section of its own because a delay belongs to the
// watcher rather than to any build option — "--watch-delay" is a run extra, not
// a field of BuildOptions, so there is nowhere in the flat bag for it to go.
//
// And because a caller who writes { watch: true } means "build and keep
// watching" rather than an object.
function readWatch(sections, flat) {
  if (sections === null || sections.watch === undefined) {
    // The flat form carries the delay at the top level, next to the build
    // options, which is where it has always been read from.
    return typeof flat.delay === "number" ? { delay: flat.delay } : null;
  }

  const given = sections.watch;
  if (given === true) return {};
  if (given === false) return null;
  if (!isPlainObject(given)) {
    throw new TypeError('"watch" must be true, false, or an object of watch options');
  }

  for (const key of Object.keys(given)) {
    if (!WATCH_KEYS.has(key)) {
      throw new TypeError(
        `"${key}" is not a "watch" option. watch takes: ${[...WATCH_KEYS].join(", ")}.`
      );
    }
  }

  return Object.assign({}, given);
}

/**
 * Reads the plugin list.
 *
 * Accepted as an array of plugin objects, or as one plugin on its own, because
 * a project with exactly one plugin should not have to wrap it in an array to
 * read the way it means. The shape of a plugin is checked by lib/plugins.js,
 * which is where a plugin's own options are known; this only refuses the two
 * things that cannot be a list at all.
 */
function readPlugins(given) {
  if (given === undefined || given === null) return null;
  if (Array.isArray(given)) return given.slice();
  if (isPlainObject(given) || typeof given === "function") return [given];

  throw new TypeError('"plugins" must be a plugin object or an array of them');
}

/**
 * Turns either spelling into the canonical flat build options.
 *
 * The three things a caller can ask for beyond the build options — a server, a
 * watcher and a plugin list — come back alongside rather than inside, because
 * they are not build options and the flag grammar has no spelling for them.
 *
 * @param {object} [options] A flat or nested configuration.
 * @returns {{build: object, server: object|null, watch: object|null, plugins: Array|null}}
 */
function normalize(options) {
  if (options === null || options === undefined) {
    return { build: {}, server: null, watch: null, plugins: null };
  }
  if (!isPlainObject(options)) {
    throw new TypeError("build takes an options object");
  }

  const parts = flatten(options);
  return {
    build: rename(parts.build),
    server: parts.server,
    watch: parts.watch,
    plugins: parts.plugins,
  };
}

/**
 * Applies the alias table, in place.
 *
 * In place because the caller of this function is normalize(), which just built
 * the object, and a copy would be a second allocation for a dozen keys.
 */
function rename(build) {
  for (const [alias, canonical] of Object.entries(ALIASES)) {
    if (build[alias] === undefined) continue;
    if (build[canonical] !== undefined) {
      throw new TypeError(
        `"${alias}" and "${canonical}" are the same option. Set one of them.`
      );
    }
    build[canonical] = build[alias];
    delete build[alias];
  }
  return build;
}

module.exports = {
  normalize,
  SECTIONS,
  ALIASES,
};