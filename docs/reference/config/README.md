---
title: Configuration reference
description: Every Guchho configuration option, grouped by section.
---

# Configuration reference

Guchho reads configuration from the nearest of these files, searching upward
from the working directory:

1. `guchho.config.js`
2. `guchho.config.json`
3. `guchho.json`

The first one found wins. A flag on the command line overrides the same setting
in the file, and the file overrides the built-in defaults.

A JavaScript config is evaluated in a Node process and its result is handed to
the same native parser, so all three names describe one schema. The native
parser reads the `build` fields `entry`, `outdir`, `outfile`, `format`,
`platform`, `target`, `minify`, `sourcemap`, `splitting`, `clean`,
`treeShaking`, `pretty`, `minifyHtml`, `bundle`, `name`, `globalName`,
`exports`, `banner`, `define` and `lint`, plus the top-level sections
`resolve`, `css`, `assets`, `external`, `output`, `logLevel`, `plugins` and
`entry`. Options outside that set are available through the JavaScript API or
as command-line flags; a config file that sets one gets the unknown-field
warning rather than a silent no-op.

## Precedence

1. An explicit command-line flag.
2. The nearest configuration file's value.
3. The built-in default.

An option set twice in one file - at the top level and under `build` - is
refused rather than resolved.

## Sections

A configuration nests its build options under `build`, with `server` and
`watch` as sibling sections. A top-level `entry`, `define`, `external`,
`output`, `resolve`, `css`, `assets`, `logLevel` and `plugins` are accepted as
aliases for the corresponding nested fields; other build options are read only
from inside `build`.

```js
export default {
  build: {
    entrypoints: ['src/index.html'],
    outdir: 'dist',
  },
  server: {
    port: 3000,
    host: 'localhost',
  },
  watch: {
    delay: 100,
  },
}
```

Setting the same option at both the top level and under `build` is refused
rather than resolved.

| Section | Page | Covers |
| --- | --- | --- |
| `build` | [build.md](build.md) | Entries, output, format, minification, targets, resolution, plugins. |
| `server` | [server.md](server.md) | Host, port, served directory, SPA fallback. |
| `watch` | [watch.md](watch.md) | Rebuild delay and interval. |

The standalone linter uses a separate `guchho.lint.config.*` file, documented in
[`guchho lint`](../cli/lint.md). The `build.lint` section only controls the
optional parser checks run during a build.

## See also

- [Configuration guide](../../guide/configuration.md)
- [JavaScript API](../../api/README.md)
