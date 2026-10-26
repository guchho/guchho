---
title: Dev server & watch
description: Development server, static server and watch mode.
---

# Dev server & watch

Three related commands share the same build engine but differ in what they do
with it.

## `guchho dev`

A development server with file watching and automatic rebuilds.

```bash
guchho dev --port=8080 --open
guchho dev --host=0.0.0.0
```

Options: `--host` (default `localhost`), `--port` (default `3000`), `--open`,
plus the build flags below them.

## `guchho serve`

Serve already-built output, without rebuilding.

```bash
guchho serve
guchho serve dist --port=8080
```

Options: `--host`, `--port`, `--open`. Nothing rebuilds; the directory
defaults to the build's output directory.

## `guchho watch`

Watch and rebuild without a server. Use this when whatever reads the output is
not a browser.

```bash
guchho watch src/index.html
guchho watch --outdir=dist
```

Options are the same as `guchho build`.

## Watch behaviour

- **`watch` in a config** - `true` means "build and keep watching". The
  JavaScript API's `watch()` reads it; the CLI's `guchho watch` takes the same
  setting as `--watch-delay` and warns that a config `watch` section is
  ignored.
- **`server` in a config** - the JavaScript API's `serve()` reads it. The CLI's
  `guchho dev` and `guchho serve` do not: pass `--host`, `--port`, `--servedir`
  and `--open` instead.

The [JavaScript API](../api/README.md#watch) exposes the same watcher and dev server as
`watch()` and `serve()`, returning a `Watcher` / `DevServer` you can rebuild and
stop programmatically.
