---
title: Targets & browsers
description: Choosing the language level the output must stay within.
---

# Targets & browsers

The output must stay within a language level. Syntax or APIs above it are
lowered automatically; everything else passes through untouched, so the target
doubles as a promise about the emitted file.

## Language target

```bash
guchho build src/app.js --target=es2020
```

`target` names a single edition: `es5`, `es2015` … `es2025`, or `esnext` for
everything the parser understands. `default` means no lowering based on a
language level.

## Engines

Instead of naming an edition, name the engines you actually test on and let the
engine compute the level:

```bash
guchho build src/app.js --target=firefox115,safari16.4
```

Each entry pairs an engine with a version. The union of their capabilities
becomes the set of features the output is allowed to use. A target string may
mix language levels and engines in one comma-separated list.

Supported engine names: `chrome`, `deno`, `edge`, `firefox`, `hermes`, `ie`,
`ios`, `node`, `opera`, `rhino`, `safari`.

A version is read as `major[.minor[.patch]]` with an optional pre-release suffix
and compared component by component, so `100.0.0` is newer than `90.0.0`. A
version that does not parse — `latest`, for instance — becomes an error rather
than a silent guess.

## The escape hatch

`supported` turns one specific feature on or off regardless of `target` and
`engines`:

```js
build: { supported: { 'async-await': false } }
```

## Charset

`charset` decides how non-ASCII characters are escaped. `default` resolves to
`ascii`, which escapes every non-ASCII character as `\uXXXX` so the file
survives any encoding chain. `utf8` emits them as-is when the output charset
allows it.

See the [configuration reference](../reference/config/build.md) for the full
list.
