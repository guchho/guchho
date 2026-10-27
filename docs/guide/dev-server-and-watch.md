---
title: Dev server & watch
description: Development server, static server and watch mode.
---

# Dev server & watch

A normal build is a snapshot: it starts, writes output and exits. The three
commands on this page keep taking snapshots. They share the same engine and the
same build options, but they differ in two decisions:

- does the process stay alive after the first build?
- does it answer HTTP requests?

Keeping those two questions separate is the easiest way to choose a command.

| Command | Rebuilds on change | Serves HTTP | Reloads the page | Use it when |
| --- | --- | --- | --- | --- |
| `guchho build` | no | no | no | producing output for deployment. |
| `guchho watch` | yes | no | no | a test runner, another compiler or a file watcher consumes the output. |
| `guchho dev` | yes | yes | yes | developing an application in a browser. |
| `guchho serve` | no | yes | no | previewing an output directory that is already built. |

`guchho dev` is not "build plus serve" in the sense of two unrelated programs.
It is one session: the watcher decides when to build, the build writes output,
and the HTTP server answers every request from that output.

## `guchho dev`

A development server with file watching and automatic rebuilds.

```bash
guchho dev
guchho dev --port=8080 --open
guchho dev --host=0.0.0.0
guchho dev --outdir=build
```

The defaults are `localhost` and port `3000`:

```text
http://localhost:3000
```

`--port` chooses the port, `--open` asks the operating system to open the URL in
a browser, and `--host` chooses the interface to bind. The default,
`localhost`, accepts connections only from this machine. Binding
`0.0.0.0` accepts connections from other machines on the network, which is what
you want when testing from a phone or another computer:

```bash
guchho dev --host=0.0.0.0 --port=8080
```

That also exposes the server beyond the local machine, so use it on a network
you trust. If the address you need is not obvious, the server prints the
addresses it bound when it starts.

`dev` accepts the build flags too. The common ones change what is built rather
than where the server listens:

```bash
guchho dev --outdir=build
guchho dev --sourcemap=inline
guchho dev --target=es2020
```

For example, `--sourcemap=inline` keeps the map in the generated file, which is
convenient when the browser's developer tools are the only consumer. A flag
that the build accepts, `dev` accepts as well; see
[`guchho build`](../reference/cli/build.md) for the full list.

### What happens after a save

1. The watcher notices that a file read by the last build changed.
2. It waits for the quiet period, so a burst of editor writes becomes one
   build.
3. The engine rebuilds the graph and writes the output.
4. The server compares output hashes with the previous successful build.
5. If anything was added, removed or changed, the page reloads.

If the rebuild fails, the watcher stays alive and keeps watching. The next save
may be the one that fixes the error, so stopping would be the least useful
response. The server reports the failure instead of serving the old bundle as
though nothing happened.

## `guchho serve`

Serve already-built output, without rebuilding.

```bash
guchho serve
guchho serve dist
guchho serve dist --port=8080 --open
guchho serve build --host=0.0.0.0
```

The positional argument is the directory to serve. With no argument, the
directory is the build's configured output directory (normally `dist`).

Options are limited on purpose: `--host`, `--port` and `--open`. There are no
build flags, because `serve` does not build. Changing a source file after the
server starts has no effect on what is served; run `guchho build` again or use
`guchho dev`.

`serve` is the right command when you want to see the exact files a browser
would receive, including the output of a production build:

```bash
guchho build --minify --sourcemap=linked
guchho serve dist
```

It is also useful for checking a directory that was produced somewhere else,
such as a CI artifact downloaded to a local machine.

## `guchho watch`

Watch and rebuild without a server. Use this when whatever reads the output is
not a browser.

```bash
guchho watch src/index.html
guchho watch --outdir=dist
guchho watch src/app.js --outdir=build --minify
```

The options are the same as [`guchho build`](../reference/cli/build.md). The
command writes output exactly as a build would, then keeps rebuilding. It does
not open a port, inject a script or reload anything.

Typical consumers are:

- a test runner that imports the built files;
- a second compiler or a documentation generator downstream of Guchho;
- a file-sync process that uploads the output directory;
- an editor integration that only needs fresh files on disk.

The watch set is not a hand-written list of globs. It comes from the files the
last build actually read. That has two useful consequences:

- a new `import` that reaches a directory nobody had looked at before starts
  being watched, because the next build reports it;
- a file that leaves the graph stops being watched, because the build no longer
  reports it.

`--watch-delay` is accepted by `guchho watch` and `guchho dev`. It is the quiet
period, in milliseconds, between noticing a change and starting the rebuild.
The default is `0`, which rebuilds as soon as a change is seen. A short delay is
useful when an editor writes a file several times for one save:

```bash
guchho watch --watch-delay=250
```

## Request handling

A development server answers from the build first, then from disk:

1. The current build output, rebuilt on demand for the request.
2. `servedir` on disk, for static files the build does not emit.
3. A configured `fallback`, which is how an SPA route reaches `index.html`.

A directory is served under its trailing slash. If the directory contains an
`index.html`, that file wins; otherwise the server can produce a directory
listing. A request that matches none of the three sources is a `404`.

This is the build-aware server's order. `guchho serve` skips the build layer
entirely: it serves the directory it was given and has no in-memory build to
prefer over it.

A failed rebuild is reported instead of hidden: the response is `503` with the
build's messages as plain text, so a broken edit is visible immediately rather
than leaving a stale bundle on screen. Requests whose `Host` header is not an
address the server bound are refused with `403`, and paths containing a
backslash are refused with `400`. Both checks keep a page on another origin
from using the development server as a proxy, and keep a Windows path from
escaping the served tree.

The order matters. A generated `index.html` wins over an `index.html` sitting in
`servedir`, so a rebuild is visible without deleting the old file. Only when the
build has no matching output does the server look at the static directory.

## Live reload

`guchho dev` turns on live reload. The server reserves two paths of its own:

- `/guchho.js` - the client script, injected before `</body>` (or `</html>`,
  or appended when neither exists) of every HTML response.
- `/guchho` - a `text/event-stream` connection. After a rebuild the server
  diffs output hashes and sends only the URLs that were added, removed or
  updated; an idle stream gets a keep-alive comment every 30 seconds.

Only HTML responses get the script tag. A JavaScript file, a stylesheet, an
image or a JSON response is sent as it is. The reserved paths exist only while
live reload is on, so a project is free to emit its own `/guchho.js` when it
uses `guchho serve` or a production build.

`guchho serve` has none of this behaviour. It is a static server over a
finished directory: no watcher, no rebuild, no injected client and no event
stream.

### Live reload from C++

The live-reload client is part of the engine, not of the JavaScript package.
Embedders using `guchho::api` can set `ServeOptions.live_reload` and call
`ServeResult.notify()` after an out-of-band rebuild. See the
[embedding reference](../embedding/generated/options.md#serveoptions) for those
declarations.

The JavaScript API's `serve()` uses the same HTTP server, but does not turn the
client injection on. If you need browser reloads, use `guchho dev`, or rebuild
and reload from your own script.

## Watch and server settings

There are two places a setting can come from, and they are easy to mix up:

- CLI flags, which apply to the command you typed;
- an options object passed to the JavaScript API.

A `watch` or `server` section inside a discovered config file is not applied by
the native engine, and is reported as ignored. The CLI and the JavaScript API
both take these settings at the call site.

For the CLI, use flags:

```bash
guchho dev --host=localhost --port=3000
guchho serve dist --host=localhost --port=8080
guchho watch --watch-delay=200
```

For the JavaScript API, pass the section to the function:

```js
import { watch } from 'guchho';

const watcher = await watch({
  entrypoints: ['src/index.html'],
  outdir: 'dist',
  watch: {
    delay: 200,
    interval: 300,
  },
});

watcher.on('buildEnd', (result) => {
  console.log(`built ${result.outputs.length} files in ${result.duration} ms`);
});

process.on('SIGINT', async () => {
  await watcher.close();
  process.exit(0);
});
```

`watch.delay` is the same quiet period as `--watch-delay`. `watch.interval` is
different: it is how often the JavaScript handle asks for a rebuild, in
milliseconds, and it defaults to `200`. The JavaScript watcher is a polling
wrapper, not a push notification. Every interval tick runs a build even when
nothing changed. Set `interval: 0` to drive rebuilds yourself with
`watcher.rebuild()`.

For a server with a warm build behind it:

```js
import { serve } from 'guchho';

const server = await serve({
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
  },
  server: {
    host: 'localhost',
    port: 8080,
  },
});

console.log(`Listening on ${server.url}`);

// Rebuild the same warm context without restarting the server.
await server.rebuild();

process.on('SIGINT', async () => {
  await server.close();
  process.exit(0);
});
```

`serve()` resolves after the opening build has finished and the server is
accepting requests, so `server.url` is ready to use. A port of `0` (or no port)
asks the engine to choose one, which is why the bound port is reported on the
handle. A wildcard bind address is reported as bound, but `server.url` uses a
reachable address such as `localhost`; `server.hosts` keeps every address the
engine bound.

`server.close()` stops the server and releases the context behind it. For
incremental rebuilds without a server, [`context()`](../api/README.md#context)
returns a `BuildContext` whose `rebuild()` reuses what the first build learned.
For change-driven rebuilds in Node, use [`watch()`](../api/README.md#watch).

## Common setups

### Develop with production-like options

```bash
guchho dev --minify --sourcemap=inline
```

This is useful when a bug only appears after minification. It is slower than a
plain development build, so keep it as a check rather than the default.

### Serve a build from another machine

```bash
guchho build --outdir=public
guchho serve public --host=0.0.0.0 --port=8080
```

The same warning applies: binding every interface makes the directory reachable
from the local network.

### Watch for a test runner

```bash
guchho watch src/index.ts --outdir=tmp/test-build --sourcemap=inline
```

The test runner can import the output while Guchho keeps it current. There is no
HTTP server to stop, which makes this easier to run in CI-like environments.

### Preview a single-page app

```bash
guchho build --outdir=dist
guchho serve dist
```

A static server can only return a file that exists. If your router uses paths
such as `/settings/profile`, configure the host to rewrite unknown paths to
`index.html`, or use a server that supports a fallback page. Building the app
does not create those paths on disk.

## Troubleshooting

- **The server starts but every page is `404`.** Check the directory. `serve`
  uses the positional argument or the configured `outdir`; a typo produces a
  valid server over an empty tree.
- **The page does not change after an edit.** Check which command is running.
  `serve` never rebuilds, and `watch` does not serve. Use `dev` for both.
- **The page still shows the previous build after a failed rebuild.** A failed
  rebuild returns `503`; fixing the error and saving again is enough. If the
  page is still stale after a successful rebuild, check that you are not
  looking at `serve` output from before the rebuild.
- **Another device cannot connect.** The default host is `localhost`. Bind
  `0.0.0.0` and use the network address the server prints, not `localhost` from
  the other device.
- **`403` from a request that works in the browser.** The `Host` header must be
  one of the addresses the server bound. A proxy that rewrites `Host`, or a
  request to an IP the server did not bind, is refused on purpose.
- **Config fields do not take effect.** A `server` or `watch` section in a
  config file is ignored with a warning. Pass the values to the command or the
  API call instead.

## See also

- [`guchho dev`](../reference/cli/dev.md), [`guchho serve`](../reference/cli/serve.md)
  and [`guchho watch`](../reference/cli/watch.md)
- [JavaScript API](../api/README.md#serve)
- [Embedding: server options](../embedding/generated/options.md#serveoptions)
- [Deployment](deployment.md) for serving the final output
