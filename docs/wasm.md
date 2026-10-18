---
title: WebAssembly
description: Running the Guchho engine in Node and the browser via @guchho/wasm.
---

# WebAssembly

`@guchho/wasm` ships the same engine as the native binary, compiled to
WebAssembly with [Emscripten](https://emscripten.org). It exists for the places
a native binary is awkward or impossible to install: CI fallbacks, sandboxes,
StackBlitz/CodeSandbox, and the browser. Flag grammar, diagnostics and exit
codes are the CLI's own - the wrapper is the only layer on top.

```sh
npm i -D @guchho/wasm
```

The package is opt-in. Install it explicitly; it is deliberately *not* a
dependency of the `guchho` package.

## CLI

The package exposes a `guchho-wasm` bin that behaves like the native `guchho`:

```sh
npx guchho-wasm build src/index.html --outdir=dist
```

`serve`, `watch` and `dev` are refused with an error naming the native
packages: the engine has no sockets and no threads.

## Node API

On Node the host file system is mounted through Emscripten's NODEFS at `/host`
and the working directory is mapped in, so entry points, `--outdir` and
resolved imports are ordinary host paths and outputs land on the real disk with
no copy-back.

```js
import { createGuchho } from "@guchho/wasm";

const guchho = await createGuchho();       // mounts the host FS at process.cwd()
console.log(guchho.version());             // e.g. "1.0.7"

guchho.build("src/index.html", { outdir: "dist", minify: true });
```

A one-shot `build(entry, options)` drives the C ABI (`guchho_build`) directly,
and `callMain(argv)` runs the CLI with the engine's own flag grammar.

## Browser

In the browser the engine runs entirely in memory. Supply a `files` map when
creating the instance and read the outputs back from `engine.FS`:

```js
const guchho = await createGuchho({
  files: {
    "src/index.html": '<!doctype html><script type="module" src="./main.js"></script>',
    "src/main.js": 'console.log("hello");',
  },
  printErr: (line) => console.error(line),
});

if (guchho.callMain(["build", "src/index.html", "--outdir=dist"]) !== 0) {
  throw new Error("build failed");
}

const html = guchho.FS.readFile("dist/index.html", { encoding: "utf8" });
```

The glue is an ES module (`-sMODULARIZE -sEXPORT_ES6`), and the binary is
resolved relative to the package (`locateFile`), never the working directory,
so the package can be served from any path.

### Top-level functions

`@guchho/wasm` also exposes a shared-engine API as top-level named exports, so
you do not need to manage an instance for one-off calls:

```js
import { configure, loadFiles, lexer, parse, print, build, version } from "@guchho/wasm";

configure({
  files: { "src/main.js": 'console.log("hello");' },
});

const source = 'console.log("hello");';

const { tokens, count } = await lexer(source, { language: "js" });
const parsed = await parse(source, { language: "js" });
const { code } = await print(parsed.ast, { language: "js" });

const output = await build("src/main.js", { outdir: "dist" });
console.log(await version());
```

`lexer`, `parse` and `print` take the source text (or bytes) itself, not a
path, and an options object naming the language — one of `"html"`, `"css"` or
`"js"`. `print` takes the numeric handle `parse` returned as `ast`. The tree
`configure()` installs is what `build()` reads.

`configure()` must be called before the first engine use; it throws if the
engine already exists. `loadFiles(files)` writes more files into the
already-created in-memory tree. These names are available from both the ESM
(`import`) and CommonJS (`require`) entry points.

## Using `guchho` in the browser

The `guchho` package itself has a `browser` export condition. When a bundler
resolves `import { build } from "guchho"` for a browser target, it picks up
`lib/browser/index.mjs` (or `.js` for `require`), which wires the same
orchestrators to the WASM transport instead of the native pipe:

```js
import { configure, build, transform, lexer } from "guchho";

// Supply the project's files before the first call.
configure({
  files: {
    "src/index.html": '<!doctype html><script type="module" src="./main.js"></script>',
    "src/main.js": 'console.log("hello");',
  },
});

const result = await build("src/index.html", { outdir: "dist" });
```

Install `@guchho/wasm` alongside `guchho`; it is an optional peer dependency
and the browser entry will tell you to if it is missing.

Differences from the Node surface:

- **`configure({ files, print, printErr })`** — sets the virtual file tree the
  engine is created with and the diagnostic sinks. Call it before the first
  build, lex, parse or print.
- **`loadFiles(files)`** — writes more files into the in-memory tree after the
  engine exists.
- **No plugins.** Plugin hooks are callbacks across the C ABI, which cannot
  cross; a request that names a plugin is refused with an explanatory error
  rather than silently ignored.
- **`watch` and `serve` are present but throw.** They are kept as named exports
  (so a shared import does not break at load time) that throw a sentence
  explaining they need a host process.
- **`stop()` is a no-op.** The in-process engine frees itself with the page.

`analyze`, `analyzeMetafile`, `context` and `pack` all work through the same
transport.

## Choosing an engine

The `guchho` wrapper resolves its engine in this order:

1. `GUCHHO_BACKEND=wasm|native` - force one, erroring if it is unavailable.
2. `GUCHHO_BINARY=/path/to/guchho` - spawn a specific native binary.
3. `@guchho/wasm`, if it is installed - **wasm-first**.
4. The native platform package for the current `os`/`cpu`.
5. Otherwise, an error naming both install options.

## Architecture

- **Threads.** Every hot-path thread site is collapsed to an eager-inline
  serial shim under `GUCHHO_WASM_SERIAL`: `Spawn` runs the closure immediately
  and `Join` is a no-op. Nothing links pthreads, so there is no
  `SharedArrayBuffer`, and no COOP/COEP headers are needed.
- **Filesystem.** Node uses NODEFS (not `-sNODERAWFS`, which is broken on
  Windows); the browser and sandboxes use MEMFS.
- **Feature gates.** `GUCHHO_WASM` drops the dev server, the watcher, child
  processes and the `serve`/`watch`/`dev` subcommands at compile time.
- **Diagnostics.** The engine writes to Emscripten's `print`/`printErr`, which
  the host can replace with its own sinks.

## Performance and size

Measured on win32-x64 (Node, median of five runs; native = the release binary
on the same machine):

| | `guchho.wasm` | native |
| --- | --- | --- |
| Raw size | 6.82 MiB | - |
| `gzip -9` | 2.06 MiB | - |
| Cold start (`--version`) | ~140 ms | ~22 ms |
| Small sample (5 files) | ~600 ms | ~70 ms |
| 3000-export sample | ~1.3 s | ~250 ms |

The engine runs at roughly 1/5 native speed. Treat it as a portable fallback,
not a replacement for the native package in a performance-sensitive CI job.

## Limitations

- Single-threaded; no `serve` / `watch` / `dev`.
- No child processes: features that would shell out (for example Yarn PnP
  config evaluation) degrade to warnings.
- Slower cold start than the native binary.

## See also

- [`@guchho/wasm` README](https://github.com/guchho/guchho/blob/main/package/npm/@guchho/wasm/README.md) -
  the package's own quick start.
- [Release notes](release-notes/v1.0.7.md) - where the package was
  introduced.
