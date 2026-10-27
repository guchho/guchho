---
title: WebAssembly
description: Running the Guchho engine in Node and the browser via @guchho/wasm.
---

# WebAssembly

`@guchho/wasm` ships the same engine as the native binary, compiled to
WebAssembly with [Emscripten](https://emscripten.org). It provides a drop-in,
portable build engine for environments where installing native platform
packages is impractical or impossible.

## When to use WebAssembly

- **CI environments**: avoid platform-specific binary downloads in restrictive or ephemeral CI runners
- **Online sandboxes**: works in StackBlitz, CodeSandbox, Gitpod, and similar browser-based environments
- **Browser applications**: run the bundler entirely in-memory for playgrounds, documentation sites, or web-based build tools
- **Cross-platform consistency**: ensure identical behavior across all platforms without architecture-specific binaries
- **Security-restricted environments**: run in sandboxes without native executable execution permissions

Flag grammar, diagnostics and exit codes are identical to the native CLI - the wrapper is the only layer on top.

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

All build flags work identically to the native CLI. The following commands are
unavailable because the engine is single-threaded with no network sockets:

- `serve` - no HTTP server support
- `watch` - no filesystem watching in the WASM runtime
- `dev` - combines serve and watch, also unavailable

Attempting to use them will produce an error naming the native packages as the
alternative.

## Node API

On Node the host file system is mounted through Emscripten's NODEFS at `/host`
and the working directory is mapped in, so entry points, `--outdir` and
resolved imports are ordinary host paths and outputs land on the real disk with
no copy-back.

### Using `createGuchho()` (instance-based)

The instance-based API gives you direct control over the engine and its
filesystem.

```js
import { createGuchho } from "@guchho/wasm";

const guchho = await createGuchho();       // mounts the host FS at process.cwd()
console.log(guchho.version());             // e.g. "1.0.8"

// Build with options
const exitCode = guchho.build("src/index.html", { 
  outdir: "dist", 
  minify: true,
  sourcemap: true 
});

if (exitCode !== 0) {
  throw new Error(`Build failed with exit code ${exitCode}`);
}

// Or run the CLI with arbitrary arguments
guchho.callMain(["build", "src/index.html", "--outdir=dist", "--splitting"]);
```

#### `createGuchho(options)`

| Option | Type | Description |
| --- | --- | --- |
| `cwd` | `string` | Working directory to mount; defaults to `process.cwd()` |
| `files` | `Record<string, string \| Uint8Array>` | In-memory files to seed the FS (bypasses host mounting if provided) |
| `print` | `(line: string) => void` | Custom stdout handler for engine output |
| `printErr` | `(line: string) => void` | Custom stderr handler for diagnostics |
| `locateFile` | `(path: string, scriptDir: string) => string` | Override WASM file location (advanced use) |

#### Instance methods

| Method | Description |
| --- | --- |
| `version()` | Returns the engine version string |
| `build(entry, options)` | Direct build via C ABI; returns exit code |
| `callMain(argv)` | Run CLI with argument array (argv[0] is typically the program name) |
| `request(payload)` | Low-level service protocol call; returns raw engine response |
| `requestRaw(payload)` | Same as `request()` but without error normalization |
| `FS` | Emscripten filesystem API for reading/writing files directly |

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
creating the instance and read the outputs back from `engine.FS`. No
`SharedArrayBuffer`, COOP, or COEP headers are required since the engine is
single-threaded.

### Complete browser example

```html
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Guchho WASM Build</title>
</head>
<body>
  <script type="module">
    import { createGuchho } from "https://unpkg.com/@guchho/wasm/index.mjs";

    // Initialize engine with virtual files
    const guchho = await createGuchho({
      files: {
        "src/index.html": `<!doctype html>
<html>
<head><title>App</title></head>
<body>
  <h1>Hello Guchho</h1>
  <script type="module" src="./main.js"></script>
</body>
</html>`,
        "src/main.js": `import { greet } from "./utils.js";
greet("World");`,
        "src/utils.js": `export function greet(name) {
  console.log("Hello " + name);
  document.body.innerHTML += "<p>Hello " + name + "</p>";
}`,
      },
      print: (line) => console.log("[guchho]", line),
      printErr: (line) => console.error("[guchho err]", line),
    });

    try {
      // Build the project
      const code = guchho.callMain(["build", "src/index.html", "--outdir=dist", "--minify"]);
      if (code !== 0) {
        throw new Error(`Build failed with exit code ${code}`);
      }

      // Read outputs from in-memory filesystem
      const html = guchho.FS.readFile("dist/index.html", { encoding: "utf8" });
      const jsFiles = guchho.FS.readdir("dist");
      
      console.log("Build successful!");
      console.log("HTML:", html);
      console.log("Output files:", jsFiles);
      
      // Create a blob and display in iframe
      const blob = new Blob([html], { type: "text/html" });
      const url = URL.createObjectURL(blob);
      document.body.innerHTML += `<iframe src="${url}" width="100%" height="300"></iframe>`;
    } catch (e) {
      console.error("Error:", e);
    }
  </script>
</body>
</html>
```

### Key browser notes

- **In-memory only**: All files live in MEMFS. Use `guchho.FS` to read outputs or write additional files.
- **No host disk access**: You must provide all input files via the `files` map or write them to the FS.
- **ESM module**: The glue is compiled as `-sMODULARIZE -sEXPORT_ES6` for native ES module support.
- **CDN friendly**: The binary is resolved relative to the package via `locateFile`, never the working directory, so it works from any URL path.
- **Dynamic file updates**: Write files directly with `guchho.FS.writeFile()` and rebuild, or use `loadFiles()` with the top-level API.

### Top-level functions

`@guchho/wasm` also exposes a shared-engine API as top-level named exports, so
you do not need to manage an instance for one-off calls. All top-level functions
share a single lazily-created engine instance.

```js
import {
  configure,
  loadFiles,
  lexer,
  parse,
  print,
  build,
  request,
  runCli,
  toVirtual,
  mountHost,
  pack,
  unpack,
  packBytes,
  unpackBytes,
  version,
} from "@guchho/wasm";

// On Node, you can skip configure() and use host FS directly
// On browser, you MUST configure() first with files
configure({
  files: { 
    "src/main.js": 'console.log("hello");',
    "src/index.html": '<!doctype html><script src="./main.js"></script>',
  },
});

// Or incrementally add files
loadFiles({
  "src/utils.js": 'export const add = (a,b) => a+b;',
});

const source = 'console.log("hello");';

// Compiler API - work on source strings directly
const { tokens, count } = await lexer(source, { language: "js" });
console.log(`Tokenized into ${count} tokens`);

const parsed = await parse(source, { language: "js" });
console.log("AST handle:", parsed.ast);

// Print/format the AST back to source
const { code } = await print(parsed.ast, { language: "js" });
console.log("Formatted:", code);

// Build from the filesystem tree
const result = await build("src/index.html", { 
  outdir: "dist", 
  minify: true,
  sourcemap: "external"
});

// Archive operations
await pack({ inputs: ["dist"], outFile: "release.zip", level: 9 });
const { bytes } = await packBytes({ inputs: ["dist"], format: "tar.gz" });
const { files } = await unpackBytes(bytes, { format: "tar.gz" });
await unpack({ archive: "release.zip", outDir: "check" });

console.log(await version());
```

#### Compiler API details

- **`lexer(source, { language })`** - Tokenizes source text into tokens. `source` is the raw text/bytes, not a file path. Returns token stream.
- **`parse(source, { language })`** - Parses source into an AST. Returns `{ ast: number }` where `ast` is a numeric handle.
- **`print(handle, { language })`** - Prints/formats an AST back to source code. Takes the numeric handle from `parse()`.

Supported languages: `"html"`, `"css"`, `"js"`. The virtual file tree set by
`configure()` is used by `build()`, not by the compiler stages which operate on
the source you pass directly.

#### Filesystem control

- **`configure({ files, print, printErr })`** — Must be called before the first
  engine use in a browser or sandbox, where there is no host filesystem. On Node
  it is optional: without it the top-level API mounts the host filesystem. It
  throws if the engine already exists. Sets the virtual file tree and the
  diagnostic sinks.
- **`loadFiles(files)`** — Writes more files into the already-created in-memory
  tree. Useful for adding files after initialization.
- **`toVirtual(absPath)`** *(Node only)* — Maps an absolute host path to its
  `/host/...` counterpart inside the engine's filesystem.
- **`mountHost(module, cwd?)`** *(Node only)* — Manually mounts the host file
  system at `/host` and changes directory. `createGuchho()` does this
  automatically unless a `files` map is provided.

#### Advanced

- **`request(payload)`** — Sends a raw service protocol request. Rejects on
  engine errors (returns `error` field) with engine's exact wording.
- **`runCli(argv)`** *(Node only)* — Mounts host FS and runs CLI in one call.
  Arguments exclude program name. Returns exit code.
- **`version()`** — Returns the engine's version string (different from package version).

All of these are available from both ESM (`import`) and CommonJS (`require`)
entry points.

## Using `guchho` in the browser

The `guchho` package itself has a `browser` export condition. When a bundler
resolves `import { build } from "guchho"` for a browser target, it picks up
`lib/browser/index.mjs` (or `.js` for `require`), which wires the same
orchestrators to the WASM transport instead of the native pipe.

### Installation

```sh
npm i guchho @guchho/wasm
```

`@guchho/wasm` is an optional peer dependency of `guchho`. The browser entry
will throw a helpful error if it's missing, instructing you to install it.

### Example

```js
import { configure, build, transform, lint, lexer, parse, print, pack } from "guchho";

// Supply the project's files before the first call.
configure({
  files: {
    "src/index.html": '<!doctype html><script type="module" src="./main.js"></script>',
    "src/main.js": 'import "./styles.css"; console.log("hello from browser!");',
    "src/styles.css": "body { background: #f0f0f0; }",
  },
  print: (msg) => console.log(msg),
  printErr: (msg) => console.error(msg),
});

// Build
const result = await build("src/index.html", { 
  outdir: "dist",
  minify: true,
  sourcemap: true 
});

console.log("Build complete:", result);

// Use compiler APIs
const tokens = await lexer('const x = 42;', { language: "js" });
const parsed = await parse('const x = 42;', { language: "js" });
const formatted = await print(parsed.ast, { language: "js" });

// Lint
await lint({ files: ["src/main.js"] });

// Pack outputs (works in browser)
await pack({ inputs: ["dist"], outFile: "bundle.zip" });
```

### Browser-specific API differences

| Feature | Node | Browser | Notes |
| --- | --- | --- | --- |
| `configure({ files, print, printErr })` | Optional if using host FS | **Required** before first use | Sets virtual file tree and sinks |
| `loadFiles(files)` | Optional | Available | Add files after engine init |
| `build()`, `transform()`, `lint()`, `analyze()`, `analyzeMetafile()`, `context()` | Work | Work | Routed through WASM transport |
| `pack()` | Works | Works | Stays in engine FS |
| `unpack()` | Works | **Throws** | No host filesystem to extract to; use `unpackBytes()` from `@guchho/wasm` directly |
| `lexer()`, `parse()`, `print()` | Work | Work | Compiler stages work normally |
| `watch()`, `serve()`, `dev()` | Work (native) | **Throws** | No host process/sockets |
| `stop()` | Stops native process | **No-op** | Engine lives with page lifecycle |
| Plugins | Supported | **Not supported** | Callbacks can't cross C ABI boundary; requests with plugins throw errors |
| JS/TS lint configs (`.js`, `.mjs`, `.cjs`) | Supported | **Not supported** | Evaluated by Node child process; JSON configs work fine |
| JSON lint configs | Supported | Supported | Can be passed as strings/objects |

### Workarounds for browser limitations

- **Need to extract archives?** Use `@guchho/wasm`'s `unpackBytes()` which returns files as in-memory objects instead of writing to disk.
- **Need plugins?** Plugins aren't possible in browser; refactor plugin logic into pure transforms or handle post-processing in JavaScript.
- **Need lint config in JS?** Convert to JSON or inline configuration.
- **Need watch/serve?** Use your bundler's dev server and rebuild on demand, or use a service worker pattern.

## Choosing an engine

The `guchho` wrapper resolves its engine in this order:

1. `GUCHHO_BINARY=/path/to/guchho` - spawn a specific native binary. It wins
   over everything, `GUCHHO_BACKEND` included: a path that is set but wrong is
   an error, not a cue to run a different engine.
2. `GUCHHO_BACKEND=wasm|native` - force one, erroring if it is unavailable.
3. `@guchho/wasm`, if it is installed - **wasm-first** (on CLI/path).
4. The native platform package for the current `os`/`cpu`.
5. Otherwise, an error naming both install options.

### Resolution details by API

| API | Resolution | Notes |
| --- | --- | --- |
| **CLI (`guchho build`)** | Follows the order above (wasm-first when `@guchho/wasm` installed) | Can use WASM if native packages unavailable |
| **`build()`, `transform()`, `lint()`, `pack()`, `analyze()`, `analyzeMetafile()`, `context()`, `watch()`, `serve()`** | **Native-only on Node** | These speak the service protocol to a child process. No WASM fallback — if only `@guchho/wasm` is installed, `require("guchho").build()` fails while `npx guchho build` succeeds. In browser, use via `guchho` package's browser export (routed to WASM). |
| **Compiler stages (`lexer()`, `parse()`, `print()`)** | **Native-first** (opposite of CLI) | Reaches same lexer either way. Falls back to WASM only if no native binary installed or `GUCHHO_BACKEND=wasm` is set. No plugins involved so native is preferred for speed. |
| **`@guchho/wasm` direct API** | Always uses WASM engine | Bypasses wrapper resolution entirely |

### Environment variables

| Variable | Values | Effect |
| --- | --- | --- |
| `GUCHHO_BINARY` | Absolute/relative path | Force specific native binary; errors if missing |
| `GUCHHO_BACKEND` | `native` \| `wasm` | Force engine type; errors if unavailable |

`GUCHHO_WASM_SERIAL` is a compile-time define used when building the WASM
artifact, not a runtime switch; it is mentioned under
[Architecture](#architecture) for that reason.

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

## Troubleshooting

### Common issues

| Issue | Cause | Solution |
| --- | --- | --- |
| "Cannot find module @guchho/wasm" | Package not installed | Run `npm i @guchho/wasm` or `npm i @guchho/wasm guchho` |
| "configure() must be called before..." | Using top-level API in browser without configure | Call `configure({ files: {...} })` before any build/lexer/parse calls |
| "configure() throws engine already exists" | Called configure twice | Only call once; use `loadFiles()` to add more files later |
| Build fails with plugins error | Using plugins in browser/WASM | Remove plugins or use native build; plugins aren't supported in WASM |
| JS lint config not working | Using `.js`/`.mjs` config in browser | Convert to JSON or use inline config; Node child process needed for JS configs |
| Slow performance | Expected for WASM | Use native packages for performance-critical builds; WASM is for portability |
| WASM file fails to load | Incorrect bundler/CDN setup | Ensure WASM file is served with correct MIME type (`application/wasm`) and path resolution works via `locateFile` |

### Debugging tips

```js
// Enable verbose output
const guchho = await createGuchho({
  print: console.log,
  printErr: console.error,
});

// Check version
console.log("Engine:", await version());

// Inspect filesystem (browser/Node)
const files = guchho.FS.readdir("/"); // or specific paths
console.log(files);
```

## Limitations

- **Single-threaded**: no `SharedArrayBuffer`, no pthreads, no COOP/COEP headers needed but also no parallel processing.
- **No dev server/watch**: `serve`, `watch`, `dev` unavailable - no sockets or filesystem watchers.
- **No child processes**: features that would shell out (e.g. Yarn PnP config evaluation) degrade to warnings.
- **No plugins**: plugin hooks can't cross the C ABI boundary in WASM builds.
- **Lint configs**: JavaScript/TypeScript lint configs require Node child process; use JSON configs.
- **Slower performance**: ~5x slower than native, higher memory usage; treat as portable fallback.
- **Cold start**: ~140ms vs ~22ms for native binary.

## Best practices

### For Node.js
- Use `npx guchho build` for automatic engine resolution if both packages installed
- Use direct `@guchho/wasm` API only when you need explicit control or in restricted environments
- Prefer native packages for production builds; use WASM as fallback

### For browsers
- Bundle all input files upfront in `configure({ files })` or use `loadFiles()`
- Read outputs from `guchho.FS` or the engine's virtual filesystem
- Use `@guchho/wasm` top-level API for simpler one-off operations
- Handle large projects carefully due to memory constraints of in-memory FS
- Serve WASM with proper headers if using custom hosting

### For CI
- Install `@guchho/wasm` as devDependency as fallback
- Use `GUCHHO_BACKEND=wasm` to force WASM if native binaries cause issues
- Allow extra time for slower WASM builds

## See also

- [`@guchho/wasm` README](https://github.com/guchho/guchho/blob/main/package/npm/@guchho/wasm/README.md) -
  the package's own quick start and API reference.
- [Release notes v1.0.7](release-notes/v1.0.7.md) - where the package was introduced.
- [Release notes v1.0.8](release-notes/v1.0.8.md) - where the compiler API (`lexer`, `parse`, `print`) came to WebAssembly.
- [Guchho Documentation](https://guchho.com/docs) - full documentation.
