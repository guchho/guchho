---
title: Configuration
description: Configure Guchho with guchho.config.js, guchho.config.json or guchho.json.
---

# Configuration

Guchho walks up from the working directory and stops at the first directory that
holds a config file. The nearest one wins; directories above it are never
consulted. Within one directory, the first of these is used:

1. `guchho.config.js`
2. `guchho.config.json`
3. `guchho.json`

`guchho build` prints the config file it is using, so a build that landed
somewhere unexpected says which file decided it. `guchho dev`, `guchho watch`
and `guchho serve` print the same `Using …` line, and all of them stop with an
error if the nearest config exists but cannot be used.

## Precedence

**Command-line flag → config file → built-in default.** A flag you pass always
wins.

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
}
```

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

Any option a flag accepts may be set here, using the flag's name in camelCase
(`outdir`, `minifyHtml`, `treeShaking`, `logLevel`, …). Unknown fields are
reported as warnings rather than ignored, so a typo is visible.

A config file read by the native CLI parser understands this subset of `build`
fields: `entry`, `outdir`, `outfile`, `format`, `platform`, `target`, `minify`,
`sourcemap`, `splitting`, `clean`, `treeShaking`, `pretty`, `minifyHtml`,
`bundle`, `name`, `globalName`, `exports`, `banner`, `define` and `lint`. It
also reads the top-level sections `resolve`, `css`, `assets`, `external`,
`output`, `logLevel`, `plugins` and `entry`. Other options are accepted by the
JavaScript API and by command-line flags, not by a config file; set one in a
config and the unknown-field warning says so. The `server` and `watch` sections
are schema-compatible but the CLI warns that it ignores them - use their flags,
or the JavaScript API's `serve()`/`watch()`.

Optional JavaScript lint checks are off by default. Enable them with `lint`:

```js
export default {
  build: {
    entry: 'src/index.js',
    lint: {
      undefined: 'warning',
      unused: 'error',
    },
  },
}
```

`lint: true` enables every check at `warning`, and a string or array such as
`lint: ['undefined']` enables just those checks. See the
[build options reference](../reference/config/build.md#optional-lint-checks).

The standalone linter uses a separate file, not this `build.lint` section:

```js
// guchho.lint.config.mjs
export default {
  include: ['src/**/*.{js,ts}'],
  rules: {
    'no-console': 'error',
    'no-unused-vars': 'warn',
  },
}
```

It looks for `guchho.lint.config.js`, `.mjs`, `.cjs`, or `.json`, and `--config`
can name one explicitly. See [`guchho lint`](../reference/cli/lint.md).

`define` values are the JavaScript expression to substitute, written as a
string: bare `false`, `true` and numbers need no quoting of their own, while
string values carry their quotes inside the text (`__VERSION__: '"1.0.0"'`). A
`--define:` flag on the command line wins for the keys it names; every other key
the config set still applies.

## Multiple builds

A config file whose root is an object produces one build. A root as an array
produces one build per element, each resolved independently from the same
explicit options and defaults.

Plugins may sit beside `build` on each element. Since v1.0.6 the plugins on an
element run only for that element's build: each resolved build gets its own
group, and the same plugin named by two elements is instantiated once per
build. A single-configuration root and a flat top-level `plugins` list still
apply to every build in the run. From C++, the grouped form is
`cli::RunWithPluginGroups`; `api::BuildOptions::config_plugins` carries the
groups when a host resolves them itself.

## Full reference

See the [configuration reference](../reference/config/README.md) for every
field.
