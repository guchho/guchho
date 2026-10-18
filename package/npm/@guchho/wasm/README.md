# @guchho/wasm

The [Guchho](https://github.com/guchho/guchho) bundler compiled to WebAssembly.
Same engine, same flag grammar, same diagnostics — no native binary required.
Useful where a native binary cannot be installed: CI fallbacks, sandboxes,
StackBlitz/CodeSandbox, and browsers.

```sh
npm i -D @guchho/wasm
```

## CLI

```sh
npx guchho-wasm build src/index.html --outdir=dist
```

Every flag the native `guchho` CLI accepts works here, except `serve`, `watch`
and `dev` — the engine is single-threaded and has no sockets, so those refuse
with an error naming the native packages.

## Node API

```js
import { createGuchho } from "@guchho/wasm";

const guchho = await createGuchho(); // mounts the host FS at the cwd
console.log(guchho.version());       // e.g. "1.0.7"

guchho.build("src/index.html", { outdir: "dist", minify: true });
// → dist/ written to the real disk, no copy-back needed
```

Entry and outdir are ordinary host paths (relative paths resolve against
`process.cwd()`, or pass `createGuchho({ cwd })`).

## Top-level API

For one-off calls you do not need to manage an instance. The package exposes a
shared engine as top-level named exports, available from both ESM and
CommonJS:

```js
import { configure, loadFiles, lexer, parse, print, build, request, version } from "@guchho/wasm";

// Configure the in-memory file tree before the first call (browser)…
configure({ files: { "main.js": 'console.log("hi");' } });
// …or skip configure() entirely on Node, where the host FS is mounted.

const source = 'console.log("hi");';

const tokens = await lexer(source, { language: "js" });
const parsed = await parse(source, { language: "js" });
const { code } = await print(parsed.ast, { language: "js" });

const result = await build("main.js", { outdir: "dist" });
console.log(await version());
```

- **`configure({ files, print, printErr })`** — sets the virtual file tree and
  diagnostic sinks. Must be called before the first engine use; throws if the
  engine already exists.
- **`loadFiles(files)`** — writes more files into the in-memory tree after the
  engine has been created.
- **`lexer(source, { language })`**, **`parse(source, { language })`**,
  **`print(handle, { language })`** — token-level access, mirroring the engine's
  own stages. `source` is the text (or bytes) to work on, not a path; `print`
  takes the numeric handle `parse` returned as `ast`. The `files` tree is what
  `build()` reads, not what these stages read.
- **`build(entry, options)`** — one-shot build through the C ABI, same options
  as `createGuchho(...).build(...)`.
- **`request(payload)`** — one service request, same protocol as the native
  pipe. It **rejects** when the engine refused (an `error` field rather than a
  populated `errors` list), carrying the engine's wording and the parsed
  errors. The untouched response, `error` field and all, is the engine
  instance's `requestRaw()` — the half a host layer with its own failure
  shape reads.
- **`version()`** — the engine version string.

All of these share one lazily-created engine. Call `configure()` before any of
them; after that, everything routes to the same instance.

## Browser

In the browser the engine runs in memory. Pass a `files` map when creating the
instance and read the outputs back out of the engine's `FS`:

```js
import { createGuchho } from "@guchho/wasm";

const guchho = await createGuchho({
  files: {
    "src/index.html": '<!doctype html><script type="module" src="./main.js"></script>',
    "src/main.js": 'console.log("hello from the wasm engine");',
  },
  printErr: (line) => console.error(line),
});

const code = guchho.callMain(["build", "src/index.html", "--outdir=dist"]);
if (code !== 0) throw new Error(`build failed with exit code ${code}`);

const html = guchho.FS.readFile("dist/index.html", { encoding: "utf8" });
const assets = guchho.FS.readdir("dist/assets");
// → the outputs live in the in-memory FS; copy them to your own store
```

No COOP/COEP headers are required: the engine is single-threaded, so there are
no `SharedArrayBuffer` requirements to satisfy.

## CommonJS

```js
const { createGuchho, build, lexer, version } = require("@guchho/wasm");

// The factory…
const guchho = await createGuchho();

// …or the top-level names, which share one engine:
const result = await build("src/index.html", { outdir: "dist" });
```

## Limitations

- Single-threaded (no pthreads, no SharedArrayBuffer, no COOP/COEP needed).
- No `serve` / `watch` / `dev`.
- No child processes — features that would shell out (e.g. Yarn PnP config
  evaluation) degrade to warnings.
- Slower cold start than the native binary (~140 ms to boot the engine);
  builds themselves run at roughly 1/5 native speed. Best treated as a
  portable fallback, not a replacement for the native package in CI.

## License

Guchho is licensed under **MIT**. Copyright © [CodeHemu](https://github.com/code-hemu).
