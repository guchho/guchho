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

`define` values are the JavaScript expression to substitute, written as a
string: bare `false`, `true` and numbers need no quoting of their own, while
string values carry their quotes inside the text (`__VERSION__: '"1.0.0"'`). A
`--define:` flag on the command line wins for the keys it names; every other key
the config set still applies.

## Multiple builds

A config file whose root is an object produces one build. A root as an array
produces one build per element, each resolved independently from the same
explicit options and defaults.

## Full reference

See the [configuration reference](../reference/config/README.md) for every
field.
