---
title: Build options
description: Reference for every build configuration option.
---

# Build options

## Input and output

| Option | Type | Meaning |
| --- | --- | --- |
| `entrypoints` | `EntryPoint[] \| EntryPoint` | What to build. |
| `entryPoints` | alias | The older spelling of `entrypoints`. Setting both is a `TypeError`. |
| `outdir` | `string` | Output directory. |
| `outfile` | `string` | Output file. |
| `outbase` | `string` | Base directory for computing output paths. |
| `loader` | `Record<string, Loader>` | Loader by file extension. |
| `stdin` | `{ contents, loader, resolveDir?, sourcefile? }` | Take input from memory rather than disk. |

An `EntryPoint` is one of:

- a path string,
- `{ out?: string; in: string }`, or
- `[out | null, in]`.

## Format and platform

| Option | Values | Meaning |
| --- | --- | --- |
| `format` | `iife`, `cjs`, `esm`, `umd`, `amd`, `system` | Output format. |
| `platform` | `browser`, `node`, `neutral` | Platform whose conventions the output follows. |
| `exports` | `named`, `default`, `none`, `auto` | Which entry export the bundle publishes. |
| `name` / `globalName` | `string` | Global for `umd` and `iife` output. Both spellings are aliases. |
| `bundle` | `boolean` | Bundle all imports (default `false`). |
| `splitting` | `boolean` | Enable code splitting. |
| `treeShaking` | `boolean` | Enable tree shaking. |
| `write` | `boolean` | Whether to write outputs (default `true`). |

See [Formats](../formats.md) and [Platforms](../platforms.md).

## Minification

| Option | Meaning |
| --- | --- |
| `minify` | Minify all output. |
| `minifyWhitespace` | Minify whitespace only. |
| `minifyIdentifiers` | Mangle identifiers only. |
| `minifySyntax` | Optimize syntax only. |
| `minifyHtml` | Minify the markup. |
| `pretty` | Indent the markup. |

## Source maps and legal comments

| Option | Meaning |
| --- | --- |
| `sourcemap` | `true`, `none`, `inline`, `external`, `linked` or `both`. |
| `sourcesContent` | Embed original sources in the map. |
| `legalComments` | `none`, `inline`, `eof`, `linked` or `external`. |
| `banner` | Text placed at the top of every generated JS file. |
| `footer` | Text placed at the bottom of every generated JS file. |
| `charset` | `ascii` or `utf8`. |

## Targets

| Option | Meaning |
| --- | --- |
| `target` | A target language version, or an array of them. |
| `supported` | Overrides for individual feature checks. |
| `tsconfig` | Path to a `tsconfig.json` to read paths and compiler options from. |
| `jsx` | `transform`, `preserve` or `automatic`. |
| `jsxFactory` | The JSX element factory. |
| `jsxFragment` | The JSX fragment factory. |
| `jsxImportSource` | The automatic-runtime import source. |
| `jsxDev` | Read jsx runtime in development mode. |
| `jsxSideEffects` | Treat JSX as having side effects. |

## Mangle and drop

| Option | Meaning |
| --- | --- |
| `mangleProps` | A property-name pattern to rename. |
| `reserveProps` | A property-name pattern to keep. |
| `mangleQuoted` | Also mangle quoted property accesses. |
| `mangleCache` | A record of renames, for stable output across builds. |
| `drop` | Statements to drop (`console`, `debugger`). |
| `dropLabels` | Labels whose statements to drop. |
| `keepNames` | Preserve `.name` on functions and classes. |
| `ignoreAnnotations` | Ignore `/* @__PURE__ */` and similar annotations. |

## Resolution

| Option | Meaning |
| --- | --- |
| `absWorkingDir` | Absolute path relative paths resolve against. |
| `nodePaths` | Extra module directories to search, ahead of `node_modules`. |
| `resolveExtensions` | Extensions tried for a path with none, in order. |
| `mainFields` | `package.json` fields to read for an entry point. |
| `conditions` | Conditions of an `exports` map to take. |
| `alias` | Replacement paths for modules, by prefix. |
| `external` | Packages to leave as imports rather than bundle. |
| `define` | Identifiers to replace, and their replacements. |
| `preserveSymlinks` | Follow symlinks rather than bundling their targets. |

## Optional lint checks

These checks are off by default. Enabling one adds diagnostics without changing
whether the build succeeds, unless the check is set to `error`.

| Option | Values | Meaning |
| --- | --- | --- |
| `lint` | `boolean`, check name, array, or object | Enable checks. `true` enables all at `warning`. |
| `lint.undefined` | `off`, `warning`, `error` | Report references with no visible binding and no known global. |
| `lint.unused` | `off`, `warning`, `error` | Report unused local variables and parameters. |
| `lint.unused-variable` | `off`, `warning`, `error` | Report unused local variables only. |
| `lint.unused-parameter` | `off`, `warning`, `error` | Report unused parameters only. |

A string or array enables the named checks at `warning` level:

```json
{ "build": { "lint": ["undefined", "unused"] } }
```

An object sets a level per check:

```json
{ "build": { "lint": { "undefined": "error", "unused": "warning" } } }
```

## Output and diagnostics

| Option | Meaning |
| --- | --- |
| `metafile` | Write a metafile describing the build. See [Metafile](../metafile.md). |
| `allowOverwrite` | Overwrite existing files (default `false`). |
| `publicPath` | The prefix on every generated URL. |
| `sourceRoot` | Where `file` in a diagnostic is resolved against. |
| `logLevel` | `verbose`, `debug`, `info`, `warning`, `error` or `silent`. |
| `logLimit` | How many log lines to keep. |
| `color` | Colour in diagnostics, or `auto`. |
| `sourcemapRaw` | A source map to embed rather than write beside the output. |

## Sections

| Option | Meaning |
| --- | --- |
| `server` | Server options. See [server.md](server.md). |
| `watch` | `true` or watch options. See [watch.md](watch.md). |
| `plugins` | Plugins to run the build with. See [Writing plugins](../../plugins/README.md). |
