---
title: Build a library
description: Publish ESM and CommonJS with dependencies left external.
---

# Build a library

A library is the opposite of an app: the code you import is not yours to
bundle, and the consumer's module system decides the format.

```bash
guchho build src/index.js --format=esm --outdir=dist/esm \
  --external:react --external:react-dom
guchho build src/index.js --format=cjs --outdir=dist/cjs \
  --external:react --external:react-dom
```

`--external:` takes a value after a colon rather than an equals sign because it
may be repeated: one flag per package you do not want to bundle.

## Match the consumer

- `--format=esm` for bundlers and modern `import`.
- `--format=cjs` for `require` and older Node.
- `--platform=neutral` when the library runs both in browsers and in Node and
  must not assume either's globals.
- `--target=es2020` (or similar) to keep from emitting syntax an old runtime
  cannot parse — see [targets and browsers](../guide/targets-and-browsers.md).

## Keep the output tree-shakeable

Guchho does not tree-shake unless asked. For a library that is imported by name,
turn it on and let the consumer keep only what it uses:

```bash
guchho build src/index.js --format=esm --bundle --tree-shaking --outdir=dist
```

See [tree shaking](../guide/tree-shaking.md) for the rules that make an export
removable.

## Declare the entry points

Point `package.json` at what you built so tools find it:

```json
{
  "exports": {
    ".": {
      "import": "./dist/esm/index.js",
      "require": "./dist/cjs/index.js"
    }
  }
}
```

## See also

- [Formats](../reference/formats.md) and [platforms](../reference/platforms.md)
- [`guchho build`](../reference/cli/build.md)
