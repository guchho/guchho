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
console.log(guchho.version());             // e.g. "1.0.8"

guchho.build("src/index.html", { outdir: "dist", minify: true });
```

A one-shot `build(entry, options)` drives the C ABI (`guchho_build`) directly,
and `callMain(argv)` runs the CLI with the engine's own flag grammar.

### Which `guchho` API runs where

This package is the engine; the `guchho` package is the orchestrator over it,
and on Node the two do not fall back the same way:

- **The CLI (`guchho build`)** runs this engine whenever it is installed —
  wasm-first — so a machine with only `@guchho/wasm` can still run every
  command except `serve`, `watch` and `dev`.
- **`lexer`, `parse` and `print`** fall back to this engine too, but
  native-first: a native binary, when one is installed, answers them without
  loading the WebAssembly module at all.
- **`build`, `transform`, `lint`, `pack`, `analyze`, `analyzeMetafile`,
  `context`, `watch` and `serve`** are native-only on Node. They speak the service
  protocol to a child process, and there is no WebAssembly fallback for that
  path: with only this package installed, `require("guchho").build()` fails
  while `npx guchho build` succeeds. In a browser the same functions run
  against this engine through the `guchho` package's `browser` export
  condition, where an in-process build is the only option.

`version()` on this package's engine is the **engine's** version, read from
the compiled binary. The `guchho` package's `version` is a string constant —
the **package's** version. The two agree in a lockstep release, which is what
this repository publishes, but they answer different questions: compare a
version against its own kind.

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
import {
  configure,
  loadFiles,
  lexer,
  parse,
  print,
  build,
  pack,
  unpack,
  packBytes,
  unpackBytes,
  version,
} from "@guchho/wasm";

configure({
  files: { "src/main.js": 'console.log("hello");' },
});

const source = 'console.log("hello");';

const { tokens, count } = await lexer(source, { language: "js" });
const parsed = await parse(source, { language: "js" });
const { code } = await print(parsed.ast, { language: "js" });

const output = await build("src/main.js", { outdir: "dist" });

await pack({ inputs: ["dist"], outFile: "release.zip" });
const { bytes } = await packBytes({ inputs: ["dist"], format: "tar.gz" });
const { files } = await unpackBytes(bytes, { format: "tar.gz" });
await unpack({ archive: "release.zip", outDir: "check" });

console.log(await version());
```

`lexer`, `parse` and `print` take the source text (or bytes) itself, not a
path, and an options object naming the language — one of `"html"`, `"css"` or
`"js"`. `print` takes the numeric handle `parse` returned as `ast`. The tree
`configure()` installs is what `build()` reads.

`pack` and `unpack` take the native package's options, with paths resolved
inside the engine's filesystem. `packBytes` and `unpackBytes` are the
memory-only halves: no archive path is named, and the bytes or extracted files
come back as data, which is what a browser caller needs. Both directions work
in the browser, unlike the `guchho` package's browser `unpack`, which has no
filesystem of its own to extract into.

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
import { configure, build, transform, lint, lexer } from "guchho";

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
- **`watch`, `serve` and `unpack` are present but throw.** They are kept as
  named exports (so a shared import does not break at load time) that throw a
  sentence explaining what the browser lacks: a host process for the first two,
  and a host filesystem for `unpack`.
- **`stop()` is a no-op.** The in-process engine frees itself with the page.
- **JSON lint configs work; JavaScript configs do not.** `lint()` reaches the
  engine through the same transport, but a `.js`, `.mjs` or `.cjs` lint config
  is evaluated by Node in a child process, which the browser does not have.

`lint`, `analyze`, `analyzeMetafile`, `context` and `pack` all work through the
same transport.

## Choosing an engine

The `guchho` wrapper resolves its engine in this order:

1. `GUCHHO_BINARY=/path/to/guchho` - spawn a specific native binary. It wins
   over everything, `GUCHHO_BACKEND` included: a path that is set but wrong is
   an error, not a cue to run a different engine.
2. `GUCHHO_BACKEND=wasm|native` - force one, erroring if it is unavailable.
3. `@guchho/wasm`, if it is installed - **wasm-first**.
4. The native platform package for the current `os`/`cpu`.
5. Otherwise, an error naming both install options.

This is the *command line's* order. The compiler stages (`lexer`, `parse`,
`print`) resolve the other way round on purpose: **native-first**, WebAssembly
only when no binary is installed or `GUCHHO_BACKEND=wasm` asks for it. A
compiler-stage request carries no plugins and reaches the same lexer either
way, so there is nothing to gain from the slower engine where the faster one
exists; the fallback is for the machine that has no binary. The high-level
`build()` API does not consult either order on Node — it is native-only (see
"Node API" above).

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
  introduced, and [1.0.8](release-notes/v1.0.8.md) - where the compiler API
  (`lexer`, `parse`, `print`) came to WebAssembly.
