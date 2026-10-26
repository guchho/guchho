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

## Request handling

The server answers from the build, not from whatever happens to be on disk:

1. The current build output, rebuilt on demand for the request.
2. `servedir` on disk, for static files the build does not emit.
3. `fallback`, which is how an SPA route reaches `index.html`.

A failed rebuild is reported instead of hidden: the response is `503` with the
build's messages as plain text, so a broken edit is visible immediately rather
than leaving a stale bundle on screen. Requests whose `Host` header is not an
address the server bound are refused with `403`, and paths containing a
backslash are refused with `400`.

`guchho dev` turns on live reload. The server reserves two paths of its own:

- `/guchho.js` - the client script, injected before `</body>` (or `</html>`,
  or appended when neither exists) of every HTML response.
- `/guchho` - a `text/event-stream` connection. After a rebuild the server
  diffs output hashes and sends only the URLs that were added, removed or
  updated; an idle stream gets a keep-alive comment every 30 seconds.

The JavaScript API exposes the same behaviour through `ServeOptions.live_reload`
(off by default) and `ServeResult.notify()` for a watcher that rebuilds out of
band.

## Watch behaviour

- **`watch` in a config** - `true` means "build and keep watching". The
  JavaScript API's `watch()` reads it; the CLI's `guchho watch` takes the same
  setting as `--watch-delay` and warns that a config `watch` section is
  ignored.
- **`server` in a config** - the JavaScript API's `serve()` reads it. The CLI's
  `guchho dev` and `guchho serve` do not: pass `--host`, `--port` and `--open`
  instead, and give `guchho serve` the directory as its positional argument.

The [JavaScript API](../api/README.md#watch) exposes the same watcher and dev server as
`watch()` and `serve()`, returning a `Watcher` / `DevServer` you can rebuild and
stop programmatically.
