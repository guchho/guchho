---
title: Bundling & splitting
description: Inlining the graph, extracting shared chunks, and keeping packages external.
---

# Bundling & splitting

## Bundle

`bundle` inlines the whole graph into the output. Without it, each input file is
merely converted.

```bash
guchho build src/app.js --bundle --outfile=dist/app.js
```

Two consequences worth knowing: `external` and `alias` only mean something when
bundling, and using either without it is reported as an error, because there is
no graph for them to act on.

## Code splitting

`splitting` extracts code shared by several entry points into chunks that those
entries import. It requires ES module output.

```js
build: {
  entry: ['src/a.js', 'src/b.js'],
  outdir: 'dist',
  format: 'esm',
  splitting: true,
}
```

## External packages

`external` keeps the named paths out of the graph and leaves the import
statement in the output:

```bash
guchho build src/app.js --bundle --external:react --external:react-dom
```

An entry matches either exactly or as a `prefix*` / `*suffix` / `*middle*`
wildcard, so `--external:'@scope/*'` keeps a whole scope out. Entry points are
never external: an entry has to be a real file.

`packages` is the package-wide switch: `bundle` (default) or `external`. It
applies to bare specifiers; relative and absolute paths still follow `external`
and the graph.

## Aliasing

`alias` redirects a specifier before resolution starts:

```js
build: {
  alias: { '@': './src', 'lodash': 'lodash-es' },
}
```

## Choosing an entry export

`exports` selects the value the bundle exposes:

| Value | Meaning |
| --- | --- |
| `named` | A namespace object (the default). |
| `default` | The entry's default export is the value. |
| `none` | Publish nothing; valid only when nothing is exported. |
| `auto` | Infer from the entry's actual exports. |

All four are read for every output format. See [Formats](../reference/formats.md).

## Output names

Output file names are built from the `[dir]`, `[name]`, `[hash]` and `[ext]`
placeholders:

```js
build: {
  entryNames: '[name]-[hash]',
  chunkNames: 'chunks/[name]-[hash]',
  assetNames: 'assets/[name]-[hash]',
}
```
