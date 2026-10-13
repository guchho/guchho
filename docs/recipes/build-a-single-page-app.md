---
title: Build a single-page app
description: Go from index.html to a deployable dist directory.
---

# Build a single-page app

Point the build at the document and it follows the scripts and stylesheets the
document refers to.

```bash
guchho build index.html --outdir=dist --minify
```

## Why you did not have to say "bundle"

An HTML entry point cannot be served as one copied file - it refers to its
scripts and styles - so when any entry point ends in `.html` and bundling has
not been asked for either way, it is switched on automatically. A build that
left it off would hand back a directory of untouched assets instead of the
single file a browser needs. An explicit `--bundle` or `--bundle=false` on the
command line always wins.

The same behaviour is available from a config file:

```json
{
  "entryPoints": ["index.html"],
  "outdir": "dist",
  "minify": true,
  "sourcemap": true
}
```

## The pieces it produces

- The HTML, rewritten to point at the hashed JS and CSS it needs.
- One or more JS files, bundled and minified.
- CSS, collected from the imports and `<link>`s.
- Every other referenced asset, copied and URL-rewritten.

For the details of each stage, see the
[HTML pipeline](../guide/html-pipeline.md),
[CSS pipeline](../guide/css-pipeline.md) and
[assets and loaders](../guide/assets-and-loaders.md) guides.

## Debugging in the browser

`--sourcemap` writes maps beside the output so the browser shows original
sources, and `--sources-content` embeds them so the map works without the
originals. See [source maps](../guide/source-maps.md).

## See also

- [`guchho build`](../reference/cli/build.md)
- [Build options](../reference/config/build.md)
- [Serve and watch](serve-and-watch.md)
