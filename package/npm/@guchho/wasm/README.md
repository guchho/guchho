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
const createGuchho = require("@guchho/wasm");
const guchho = await createGuchho();
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

MIT
