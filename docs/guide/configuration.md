---
title: Configuration
description: Configure Guchho with guchho.config.js, guchho.config.json or guchho.json.
---

# Configuration

Guchho reads one configuration file per run. It walks up from the working
directory and stops at the first directory that holds a config file. The nearest
one wins; directories above it are never consulted, and a config in a parent
directory cannot merge with one below it.

## Discovery

Within one directory, the first of these is used:

1. `guchho.config.js`
2. `guchho.config.json`
3. `guchho.json`

The two JSON names describe the same schema and differ only in spelling.

Consider this tree:

```text
repo/
  guchho.config.js          ← used when you run from repo/
  packages/
    web/
      guchho.config.json    ← used when you run from packages/web/
```

Running `guchho build` in `packages/web` reads the JSON file, not the one at the
repository root. Running it in `repo/` reads the JavaScript file. When two files
of different names sit in the same directory, the higher entry in the list wins.

`guchho build` prints the config file it is using, so a build that landed
somewhere unexpected says which file decided it. `guchho dev`, `guchho watch`
and `guchho serve` print the same `Using …` line, and all of them stop with an
error if the nearest config exists but cannot be used.

## Precedence

**Command-line flag → config file → built-in default.** A flag you pass always
wins over the same option in the file, and the file wins over the default.

```js
// guchho.config.js
export default {
  build: { sourcemap: 'linked' },
};
```

```bash
guchho build --sourcemap=inline   # inline wins
guchho build                      # linked, from the file
```

With neither a flag nor a config value, the built-in default applies - here
`none`. An option set twice in one file - at the top level and under `build` -
is refused rather than resolved.

## A minimal config

```js
// guchho.config.js
export default {
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
  },
};
```

That is usually enough to start: an HTML entry turns bundling on by itself, the
output directory defaults to `dist`, and minification stays off until you ask
for it.

## JavaScript config

```js
// guchho.config.js
export default {
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
    format: 'esm',
    target: 'esnext',
    minify: true,
    pretty: true,
    minifyHtml: false,
    define: { __DEV__: 'false', __VERSION__: '"1.0.0"' },
  },
};
```

A JavaScript config is evaluated in a Node process and its result is handed to
the same native parser the JSON files use, so both spellings describe one
schema. That also means a JavaScript config can compute values:

```js
// guchho.config.js
const production = process.env.NODE_ENV === 'production';

export default {
  build: {
    entry: 'src/index.html',
    outdir: production ? 'dist' : 'build',
    bundle: true,
    minify: production,
    sourcemap: production ? 'linked' : 'inline',
    define: {
      __DEV__: production ? 'false' : 'true',
      'process.env.NODE_ENV': production ? '"production"' : '"development"',
    },
  },
};
```

The CLI never evaluates the config in-process; it asks Node for the result. A
JavaScript config therefore needs a Node binary, which is why `guchho.json`
exists for environments without one.

## JSON config

```json
{
  "build": {
    "entry": "src/index.html",
    "outdir": "dist",
    "format": "esm",
    "target": "esnext",
    "minify": true,
    "define": { "__DEV__": "false", "__VERSION__": "\"1.0.0\"" }
  }
}
```

`guchho.json` uses the same shape. The two differ only in what surrounds the
object, except that the `.config.json` name sorts above `guchho.json` when both
exist in one directory.

## What a config file can set

Flag names become camelCase field names (`--minify-html` → `minifyHtml`,
`--tree-shaking` → `treeShaking`, `--log-level` → `logLevel`). The native
parser reads this subset of `build` fields:

`entry`, `outdir`, `outfile`, `format`, `platform`, `target`, `minify`,
`sourcemap`, `splitting`, `clean`, `treeShaking`, `pretty`, `minifyHtml`,
`bundle`, `name`, `globalName`, `exports`, `banner`, `define`, `lint`.

It also reads these top-level sections and aliases:

| Top-level name | Meaning |
| --- | --- |
| `entry` | Alias for `build.entry` |
| `define` | Alias for `build.define` |
| `external` | Alias for `build.external` |
| `output` | Output naming and path settings |
| `resolve` | Resolution settings |
| `css` | CSS settings |
| `assets` | Asset settings |
| `logLevel` | Log verbosity |
| `plugins` | Plugins for every build in the run |
| `server` | Server settings, read by the JavaScript API |
| `watch` | Watch settings, read by the JavaScript API |

An option outside that set is available through the JavaScript API or as a
command-line flag, not through a config file. Setting one in a config produces
the unknown-field warning rather than a silent no-op, so a typo is visible:

```js
export default {
  build: {
    minifyWhitespace: true,   // understood
    mnifySyntax: true,        // warning: unknown field
  },
};
```

The `server` and `watch` sections are schema-compatible but the CLI warns that
it ignores them. Use their flags with `guchho dev` / `guchho serve`, or the
JavaScript API's `serve()` / `watch()` when the server or watcher must be
configured from the file.

## `define` values

`define` maps an identifier or dotted path to the JavaScript expression that
should replace it. Values are written as strings, so a string replacement carries
its own quotes inside the text:

```js
build: {
  define: {
    __DEV__: 'false',                  // replaces __DEV__ with false
    __VERSION__: '"1.0.0"',            // replaces __VERSION__ with "1.0.0"
    'process.env.NODE_ENV': '"production"',
  },
}
```

Bare `false`, `true` and numbers need no extra quoting. A `--define:` flag on the
command line wins for the keys it names; every other key the config set still
applies.

```bash
guchho build --define:__DEV__=true
```

## Lint settings

Optional parser checks during a build are configured under `build.lint`. They
are off by default:

```js
export default {
  build: {
    entry: 'src/index.js',
    lint: {
      undefined: 'warning',
      unused: 'error',
    },
  },
};
```

`lint: true` enables every check at `warning`, and a string or array such as
`lint: ['undefined']` enables just those checks. See the
[build options reference](../reference/config/build.md#optional-lint-checks).

The standalone linter is separate. It uses its own file, not `build.lint`:

```js
// guchho.lint.config.mjs
export default {
  include: ['src/**/*.{js,ts}'],
  rules: {
    'no-console': 'error',
    'no-unused-vars': 'warn',
  },
};
```

It looks for `guchho.lint.config.js`, `.mjs`, `.cjs`, or `.json`, and `--config`
can name one explicitly. See [`guchho lint`](../reference/cli/lint.md).

## Multiple builds

A config file whose root is an object produces one build. A root as an array
produces one build per element, each resolved independently from the same
explicit options and defaults.

```js
// guchho.config.js
export default [
  {
    build: {
      entry: 'src/app.js',
      outdir: 'dist/app',
      format: 'esm',
      bundle: true,
      minify: true,
    },
  },
  {
    build: {
      entry: 'src/library.js',
      outdir: 'dist/library',
      format: 'esm',
      bundle: true,
      external: ['react'],
    },
  },
];
```

Each element is a complete build. In this example the application is minified
while the library keeps React external and stays readable enough to debug.

A few consequences of the array form:

- Each element has its own `entry` and `outdir`; two elements writing the same
  output file are an error.
- Settings that describe the whole run rather than one build - `--metafile`,
  `--mangle-cache` and `--watch` - cannot be combined with multiple builds.
- All elements share the discovery result; there is no per-element config file.

### Plugins per build

Plugins may sit beside `build` on each element. Since v1.0.6 the plugins on an
element run only for that element's build: each resolved build gets its own
group, and the same plugin named by two elements is instantiated once per build.
A single-configuration root and a flat top-level `plugins` list still apply to
every build in the run.

```js
export default [
  {
    build: { entry: 'src/app.js', outdir: 'dist/app' },
    plugins: [appOnlyPlugin()],
  },
  {
    build: { entry: 'src/lib.js', outdir: 'dist/lib' },
    plugins: [libOnlyPlugin()],
  },
];
```

From C++, the grouped form is `cli::RunWithPluginGroups`;
`api::BuildOptions::config_plugins` carries the groups when a host resolves them
itself.

## Server and watch sections

These sections are read by the JavaScript API, not by the CLI:

```js
export default {
  build: { entry: 'src/index.html', outdir: 'dist' },
  server: {
    port: 3000,
    host: 'localhost',
    servedir: 'dist',
    fallback: 'index.html',
  },
  watch: {
    delay: 100,
  },
};
```

| Section | Option | Meaning |
| --- | --- | --- |
| `server` | `port` | Port to bind; zero or absent tries 8000 upward |
| `server` | `host` | Interface to bind; absent binds every interface |
| `server` | `servedir` | Directory to serve; defaults to the build's output |
| `server` | `fallback` | Served when nothing matches, for SPA routes |
| `server` | `open` | Accepted by the schema; the engine does not open a browser |
| `watch` | `delay` | Quiet milliseconds before a change is final |
| `watch` | `interval` | How often the handle rebuilds; zero means never |

On the command line, `guchho dev` and `guchho serve` take `--host`, `--port` and
`--open` instead, and warn that a config's `server` section is ignored. See
[Server options](../reference/config/server.md) and
[Watch options](../reference/config/watch.md).

## Choosing between the three files

| Situation | Use |
| --- | --- |
| You want `process.env`, computed values or helper functions | `guchho.config.js` |
| You want a config that works without Node | `guchho.config.json` |
| You want a short project-level file | `guchho.json` |
| You have several packages in one repository | one config per package |

## Common mistakes

- **Expecting a parent config to merge.** The nearest file wins whole; nothing
  is inherited from a config above it.
- **Setting a flag-only option in the config.** It warns instead of applying.
  Use the flag or the JavaScript API.
- **Forgetting the extra quotes in `define`.** `__VERSION__: '1.0.0'` replaces
  it with an identifier; `'"1.0.0"'` replaces it with a string.
- **Assuming a JavaScript config works without Node.** The CLI evaluates it in
  a Node process; use JSON where Node is unavailable.
- **Setting `server` or `watch` and expecting the CLI to use it.** The CLI
  ignores those sections; use flags or the JavaScript API.
- **Using `build.lint` for the standalone linter.** That section only controls
  build-time parser checks; `guchho lint` has its own config file.

## See also

- [Configuration reference](../reference/config/README.md) - every field and
  section.
- [Build options](../reference/config/build.md) - the complete `build` table.
- [CLI overview](cli-overview.md#configuration-in-the-cli) - config discovery
  from the command line.
- [Core concepts](core-concepts.md#effective-options) - how settings rank.
