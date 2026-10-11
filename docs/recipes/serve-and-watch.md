---
title: Serve and watch
description: Pick the right one of dev, serve and watch.
---

# Serve and watch

There are three commands that keep running, and the difference between them is
what each one does when a file changes.

| Command | Serves HTTP | Rebuilds on change | Use it when |
| --- | --- | --- | --- |
| `guchho dev` | yes | yes, and reloads the page | developing an app in a browser. |
| `guchho watch` | no | yes | the thing reading the output is not a browser. |
| `guchho serve` | yes | no | you want to look at an already-built directory. |

## The development loop

```bash
guchho dev
guchho dev --port=8080 --open
guchho dev --host=0.0.0.0        # reachable from another machine
```

`dev` is build plus watch plus server: it builds, serves the output, watches
every file the build read, and rebuilds when one changes. The server also
rebuilds before answering a request, so a stale page is not possible — and
rebuilds that finish push a reload to any open browser.

## Watch without a server

```bash
guchho watch src/index.html
guchho watch --outdir=dist
```

`watch` accepts the same flags as [`build`](../reference/cli/build.md) and writes
files; it just does it again every time something changes. Reach for it when the
consumer is a test runner or another process rather than a page.

## Serve without rebuilding

```bash
guchho serve
guchho serve dist --port=8080
```

`serve` is a static file server over an output directory (default `dist`). It
has no build flags at all: if you want a rebuild, you want `dev`.

## Doing it from a program

The three map onto the API — [`context()`](../api/README.md#context) for the
incremental build, [`watch()`](../api/README.md#watch) and
[`serve()`](../api/README.md#serve) to drive it, and
[`stop()`](../api/README.md#stop) to shut it down. See the
[dev server guide](../guide/dev-server-and-watch.md).

## See also

- [`guchho dev`](../reference/cli/dev.md), [`guchho watch`](../reference/cli/watch.md), [`guchho serve`](../reference/cli/serve.md)
- [Server options](../reference/config/server.md) and [watch options](../reference/config/watch.md)
