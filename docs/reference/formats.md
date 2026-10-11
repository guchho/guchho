---
title: Formats
description: The output formats Guchho can produce.
---

# Formats

Set with `format` in configuration or `--format=<format>` on the command line.

| Format | Name | Notes |
| --- | --- | --- |
| `iife` | Immediately-invoked function expression | Browser global; uses `name` / `globalName`. |
| `cjs` | CommonJS | `require` / `module.exports`. |
| `esm` | ECMAScript module | `import` / `export`. |
| `umd` | Universal module definition | Works as CJS, AMD or global; uses `name`. |
| `amd` | Asynchronous module definition | `define`. |
| `system` | SystemJS | `System.register`. |

## Entry exports

`exports` decides what the entry publishes:

| Value | Meaning |
| --- | --- |
| `named` | A namespace object of the export names (the default). |
| `default` | The entry's default export is the value. |
| `none` | Publish nothing; valid only when the entry exports nothing. |
| `auto` | Infer from the entry's exports. |

## See also

- [Bundling & splitting](../guide/bundling-and-splitting.md)
- [Configuration: build](config/build.md)
