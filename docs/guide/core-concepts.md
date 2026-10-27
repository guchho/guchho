---
title: Core concepts
description: The vocabulary used throughout the Guchho documentation.
---

# Core concepts

Guchho is a build engine, not a task runner. It reads source files, follows the
references it finds, and writes the result in a form a browser or runtime can
consume. The terms below are the vocabulary the rest of the documentation uses.

## Entry points

An **entry point** is a file the build starts from. It may be an HTML document,
a JavaScript module, or a stylesheet. Everything reachable from an entry point
is part of the build.

```js
build: {
  entry: 'src/index.html',
}
```

An entry point is not automatically the only output. A bundle may produce one
file, several files, or a directory of assets, depending on what the graph
contains and which options you set.

Several entries produce several outputs:

```js
build: {
  entry: ['src/home.js', 'src/dashboard.js'],
  outdir: 'dist',
}
```

Each entry is named in the output by the `entryNames` pattern; chunks and assets
have their own patterns. An entry point must be a real file and is never marked
external.

## The graph

As Guchho reads a file it discovers imports, URLs, `@import` rules and asset
references. The result is a **module graph**: files are nodes, references are
edges.

```js
// src/index.html
// <script type="module" src="./app.js"></script>
// <link rel="stylesheet" href="./style.css">
```

```js
// src/app.js
import { greet } from './greet.js';
import logo from './logo.png';
```

```js
// src/greet.js
export function greet(name) {
  console.log(`Hello, ${name}`);
}
```

The graph for that project has edges from `index.html` to `app.js` and
`style.css`, from `app.js` to `greet.js` and `logo.png`, and an edge from each
asset to the loader that decides what it becomes.

Whether the graph is inlined or left as separate files depends on the `bundle`
option:

- **Bundled** - imports are resolved and combined; the output can be one file.
- **Not bundled** - each file is converted on its own and imports are left as
  written.

```bash
guchho build src/app.js --bundle --outfile=dist/app.js
guchho build src/app.js --outfile=dist/app.js          # converted, not bundled
```

See [Bundling & splitting](bundling-and-splitting.md) for what the graph makes
possible: splitting, external packages and aliases.

## Loaders

A **loader** decides how a file's bytes become a module. It answers three
questions at once: how the bytes are parsed, what the module exports, and
whether the original file is emitted.

| File | Loader | Result in the graph |
| --- | --- | --- |
| `.json` | `json` | An object whose keys may be named imports |
| `.png` | `copy` | An opaque asset emitted verbatim and referenced by URL |
| `.png` | `dataurl` | A data URL string inlined into JavaScript |
| `.png` | `binary` | A `Uint8Array` of the raw bytes |
| `.txt` | `text` | An inert string |
| `.wasm` | `binary` | Bytes ready for `WebAssembly.compile` |

The same bytes under a different loader are a different module. Choosing a
loader is therefore not a formatting detail; it decides what the rest of the
build sees. See [Assets & loaders](assets-and-loaders.md).

## Bundle vs. transform

These are the two shapes of work Guchho does:

- A **build** has a file system, a graph and package resolution. Use it to
  produce a directory of output.
- A **transform** works on a single string in memory. Use it for editor
  integrations and pipelines that already did their own bundling.

A build:

```bash
guchho build src/index.html --outdir=dist
```

```js
import { build } from 'guchho';

const result = await build({
  entrypoints: ['src/index.html'],
  outdir: 'dist',
  bundle: true,
});
```

A transform:

```bash
echo 'const x: number = 1' | guchho transform --loader=ts
```

```js
import { transform } from 'guchho';

const { code } = await transform('const x: number = 1', {
  loader: 'ts',
  target: 'es2020',
});
```

A transform has no graph, so options that need one - `external`, `alias`,
`splitting`, `entryNames` - do not apply. A build with `bundle` off is closer to
a transform: it converts each file, but still has a file system and a config.

## Options are input only

`BuildOptions` and `TransformOptions` are plain structs whose members all have
defaults. You name the few fields you care about and the rest fall into place.

```js
const result = await build({
  entrypoints: ['src/app.js'],
});
```

That is a complete request. `outdir` defaults to `dist`, `platform` to
`browser`, `bundle` to off (except for an HTML entry), source maps to `none`,
and minification to off. Reading a result never changes the options; running
two builds with the same options produces the same output.

The JavaScript API spells the entry option `entrypoints`; a config file uses
`entry`. Both reach the same setting.

## Failure is data

A build hands back a result carrying `errors` and `warnings`. A build with only
warnings is still a successful build. On failure the `errors` list is non-empty
and the output fields stay empty.

```js
const result = await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
});

for (const warning of result.warnings) {
  console.warn(warning.text);
}

console.log(`Wrote ${result.outputs.length} files in ${result.duration} ms`);
```

A failed build rejects rather than resolving:

```js
import { BuildFailure, build } from 'guchho';

try {
  await build({ entrypoints: ['src/app.js'], outdir: 'dist' });
} catch (error) {
  if (!(error instanceof BuildFailure)) throw error;

  for (const message of error.errors) {
    const where = message.location
      ? `${message.location.file}:${message.location.line}:${message.location.column}`
      : '<build>';
    console.error(`${where}: ${message.text}`);
  }

  process.exitCode = 1;
}
```

The CLI follows the same rule in exit codes: a failed build exits `1`, a usage
error exits `2`, and a lone `--help` or `--version` exits `0`. Warnings do not
fail a build.

## Effective options

Guchho ranks settings in a fixed order:

1. **Explicit** - a flag on the command line, or a field assigned in C++.
2. **Config file** - `guchho.config.js`, `guchho.config.json`, then
   `guchho.json`.
3. **Built-in defaults.**
4. **Derived values** - an output file implies its directory; a bundle implies a
   format.

Every command and embedding goes through the same resolution, so a project sees
the same settings however it is driven.

The first three are a precedence list. The fourth is not a lower rank; it is how
the engine fills in a value that follows from one you did set:

```js
build: {
  entry: 'src/index.html',   // implies bundle: true when bundle was not set
  outfile: 'dist/app.js',    // implies the output directory
}
```

Likewise, `format` is derived when it is not named: `iife` for a browser
bundle, `cjs` for a Node bundle, `esm` for a neutral bundle. A non-bundled
transform keeps the input's module syntax instead.

A worked example for one option:

| Source | `sourcemap` value |
| --- | --- |
| `--sourcemap=inline` on the command line | `inline` |
| Config file says `"sourcemap": "linked"`, no flag | `linked` |
| Neither names it | `none` |

## Platform and format

Two options are easy to confuse:

- **`platform`** - whose conventions the output follows: `browser`, `node` or
  `neutral`. It decides which package fields are preferred, which globals are
  assumed, and which default format is derived.
- **`format`** - the module syntax the output uses: `iife`, `cjs`, `esm`,
  `umd`, `amd` or `system`.

```js
build: {
  platform: 'node',   // Node conventions
  format: 'cjs',      // CommonJS output
  bundle: true,
}
```

Choosing a platform does not choose a format for you when you name one; the
derivation only fills in a format that was left unset. See
[Formats](../reference/formats.md) and [Platforms](../reference/platforms.md).

## Resolution

Resolution is how an import string becomes a file. Guchho's resolver, in broad
strokes:

1. Applies `alias` prefix replacements.
2. Checks `external` and returns a match as an unresolved import.
3. Resolves relative and absolute paths against the importing file.
4. Resolves bare specifiers through `node_modules`, package `exports` and
   `conditions`, then `mainFields`.
5. Applies `resolveExtensions` when a path has no extension.

```js
await build({
  entrypoints: ['src/index.js'],
  outdir: 'dist',
  bundle: true,
  alias: { '@': './src' },
  external: ['react'],
  conditions: ['import', 'browser'],
  resolveExtensions: ['.tsx', '.ts', '.jsx', '.js', '.json'],
});
```

Resolution only happens for a build; a transform receives one string and does
not follow imports. See [Bundling & splitting](bundling-and-splitting.md#aliasing)
and the [build options reference](../reference/config/build.md#resolution).

## Outputs and naming

A build's output is a set of files described by three naming patterns:

| Pattern | Applies to |
| --- | --- |
| `entryNames` | Entry points |
| `chunkNames` | Shared chunks produced by splitting |
| `assetNames` | Files emitted by the `file` and `copy` loaders |

```js
build: {
  entryNames: 'js/[name]-[hash]',
  chunkNames: 'js/chunks/[name]-[hash]',
  assetNames: 'assets/[name]-[hash]',
}
```

`[name]`, `[hash]`, `[dir]` and `[ext]` are substituted into each pattern, and
every URL that points at a renamed file is rewritten to match. `publicPath`
prefixes the URLs the output emits, which is a separate question from what the
files are called. See [Deployment](deployment.md).

## See also

- [Configuration](configuration.md) - where options come from and how they rank.
- [Assets & loaders](assets-and-loaders.md) - how non-JS files enter the graph.
- [Bundling & splitting](bundling-and-splitting.md) - what a graph enables.
- [JavaScript & TypeScript](javascript-and-typescript.md) - language support.
- [CSS pipeline](css-pipeline.md) - stylesheets in the graph.
