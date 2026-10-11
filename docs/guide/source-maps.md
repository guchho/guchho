---
title: Source maps
description: Inline, linked and external source maps.
---

# Source maps

`sourcemap` controls how the map for each emitted file is produced.

| Value | Behaviour |
| --- | --- |
| `none` | No map (the default). |
| `inline` | Appended as a `//# sourceMappingURL=data:…` comment. |
| `linked` | A `.map` file plus a linking comment. The usual choice for a build uploaded to a web server. |
| `external` | A `.map` file with no linking comment, for pipelines that upload maps to a symbol server. |
| `inlineAndExternal` | Both, so the artefact works on its own and a symbol server still has a file. |

```bash
guchho build src/app.js --sourcemap --outdir=dist
```

## Sources content

`sourcesContent` decides whether the pre-transform source text is embedded:

- `include` (default) — write a `sourcesContent` array, making the map
  self-contained.
- `exclude` — omit it, for much smaller maps when the sources are already
  reachable from wherever the debugger runs.

## Source root

`sourceRoot` prepends a URL prefix to every source path in the map:

```js
build: { sourcemap: 'linked', sourceRoot: 'https://example.com/src/' }
```

## Constraints

External and linked maps need an output directory. A build with neither `outdir`
nor `outfile` and no directory is rejected if it asks for them, because there is
nowhere to put the map.
