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

## Sections

A configuration may write its options flat, or nested under `build`, `server`
and `watch`:

```js
export default {
  build: {
    entrypoints: ['src/index.html'],
    outdir: 'dist',
  },
  server: {
    port: 3000,
    open: true,
  },
  watch: {
    delay: 100,
  },
}
```

The nested spelling is an alias for the flat one, not a second API. Setting the
same option at both levels is refused rather than resolved.

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
