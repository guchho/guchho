<p align="center">
  <a href="https://github.com/guchho">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/guchho_banner_light.png" />
      <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/guchho_banner_dark.png" />
      <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/guchho_banner_dark.png" alt="guchho logo" width="500px" />
    </picture>
  </a>
</p>

<p align="center">
  <a href="https://www.npmjs.com/package/guchho"><img src="https://img.shields.io/npm/v/guchho" alt="Version"></a>
  <a href="https://github.com/guchho/guchho/blob/master/LICENSE"><img src="https://img.shields.io/github/license/guchho/guchho" alt="License"></a>
  <a href="https://github.com/guchho/guchho/issues"><img src="https://img.shields.io/github/issues/guchho/guchho" alt="Issues"></a>
  <a href="https://github.com/guchho/guchho/graphs/contributors"><img src="https://img.shields.io/github/contributors/guchho/guchho" alt="Contributors"></a>
  <a href="https://github.com/sponsors/code-hemu"><img src="https://img.shields.io/badge/Sponsor-GitHub-red" alt="Sponsor"></a>
</p>

**Fast, HTML-first bundler, compiler and build tool for modern web applications — written in C++20 and shipped as a prebuilt native binary.**

Guchho is a native build system. It parses HTML, JavaScript, TypeScript, JSX/TSX, CSS and assets with its own engine, links them into a single optimised output, and serves the result — with no Node.js runtime in the build path and no JavaScript bundle in your process. This npm package installs the correct prebuilt binary for your machine and gives you a `guchho` command and a first-class JavaScript/TypeScript API.

```bash
npm install guchho
```

## Table of contents

- [Why Guchho](#why-guchho)
- [Install](#install)
- [Quick start](#quick-start)
- [CLI reference](#cli-reference)
- [Configuration file](#configuration-file)
- [JavaScript API](#javascript-api)
  - [Build](#build)
  - [Transform](#transform)
  - [Context](#context-rebuild-watch-serve)
  - [Analyze](#analyze)
  - [Lifecycle](#stop)
  - [Compiler API](#compiler-api)
  - [Error handling](#error-handling)
- [TypeScript](#typescript)
- [Supported values](#supported-values)
- [Platform support](#platform-support)
- [Environment variables](#environment-variables)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)
- [License](#license)


## Why Guchho

**Native, not transpiled.** The engine is a single C++20 binary. Parsing, linking, minification and tree shaking happen in native code, not in a JavaScript runtime, so a build does not spend its time in a garbage collector or in `node_modules`.

**HTML-first.** A web application is a document, not a module. Guchho starts at your HTML entry, follows every `<script>`, `<link>`, `import` and asset reference it finds, and produces a complete output directory — hashed filenames, rewritten URLs, minified, ready to upload.

**One tool, not five.** Lexer, parser, transformer, printer, minifier, linker, resolver, watcher and dev server are all in the same binary. There is no plugin process, no worker pool, and no separate CSS toolchain to configure.

**A real API.** The CLI is a thin shim over a long-lived service protocol. Everything `guchho build` does is also callable from JavaScript — including the compiler stages (`lexer`, `parse`, `print`), so the engine can be embedded in your own tools.

**A programmatic surface that stays out of the way.** `build()` is one call and one result. `context()` holds the expensive half of a build so the second one is cheap. Both ship with full TypeScript declarations.

## Install

```bash
npm install guchho
```

```bash
# or, in a project that uses it as a build tool
npm install --save-dev guchho
```

### Requirements

| Requirement | Value |
| ----------- | ----- |
| Node.js     | `>= 18` (any runtime that can `require`/`import` and spawn a child process) |
| Compiler    | none — prebuilt binaries are published for every supported platform |
| Install step | a `postinstall` script that copies the right binary into `node_modules/guchho/bin/` |

### How the binary is chosen

`guchho` is a thin package around one native binary. The platform binary ships as a separate optional dependency, `@guchho/<platform>-<arch>`, and `install.js` copies it into `guchho/bin/` at install time. The lookup order is:

1. `GUCHHO_BINARY` — an explicit path to a binary. If set but wrong, it fails loudly rather than silently falling back.
2. The `@guchho/<platform>-<arch>` optional dependency for this machine.
3. The binary `install.js` copied into `guchho/bin/`.
4. A `build/<preset>-<platform>-<arch>/bin/` directory in a checkout (development mode), most recently written first.

If none of those resolves, the fix is a one-liner:

```bash
npm install @guchho/linux-x64   # or win32-x64, darwin-arm64, ...
```

> **Air-gapped, Alpine/musl, or an unsupported target?** Build from source with CMake 3.25+ and point `GUCHHO_BINARY` at the result:
> ```bash
> GUCHHO_BINARY=/path/to/guchho npx guchho build src/index.html
> ```

## Quick start

```bash
# 1. Create a project (src/index.html, src/main.js, src/style.css, guchho.config.js)
npx guchho init

# 2. Development server with file watching and automatic rebuilds
npx guchho dev

# 3. Production build into ./dist
npx guchho build

# 4. Preview the production output
npx guchho serve
```

Add scripts to `package.json`:

```json
{
  "scripts": {
    "dev": "guchho dev",
    "build": "guchho build",
    "preview": "guchho serve",
    "clean": "guchho clean"
  }
}
```

From a script or CI, without a config file:

```bash
guchho build src/index.html --outdir=dist --format=esm --minify --sourcemap
```

## CLI reference

```
guchho <command> [options]
```

| Command | What it does |
| ------- | ------------ |
| `build [entry...]` | Production build. Processes HTML, JS, CSS and assets. |
| `dev` | Development server with file watching and automatic rebuilds. |
| `serve [dir]` | Serve built output, without rebuilding. |
| `watch [entry...]` | Watch source files and rebuild on changes (no server). |
| `init` | Initialize a new project with starter files. |
| `clean` | Remove generated build output. |
| `info` | Show environment and configuration information. |
| `transform` | Transform source read from stdin. |

Global: `--quiet` (minimal output), `--verbose` (detailed output), `--help`/`-h` per command, `--version`/`-v`. A bare `guchho src/index.js` with no command word is a build of that path.

### `guchho build`

```
Usage: guchho build [entry...] [options]
```

Production-oriented build. Processes HTML, JS, CSS and assets.

| Option | Description |
| ------ | ----------- |
| `--outdir=<dir>` | Output directory (default: `dist`) |
| `--outfile=<file>` | Output file |
| `--minify` | Minify all output (default: off) |
| `--minify-whitespace` | Minify whitespace only |
| `--minify-identifiers` | Mangle identifiers only |
| `--minify-syntax` | Optimize syntax only |
| `--minify-html` | Minify the markup (default: off) |
| `--pretty` | Indent the markup (default: on) |
| `--sourcemap` | Enable source maps |
| `--target=<target>` | Target (`es2020`, `chrome80`, `node16`, …) |
| `--format=<format>` | `iife`, `cjs`, `esm`, `umd`, `amd`, `system` |
| `--platform=<p>` | `browser`, `node`, `neutral` |
| `--splitting` | Enable code splitting |
| `--tree-shaking` | Enable tree shaking |
| `--bundle` | Bundle all imports |
| `--external:<pkg>` | Mark a package as external (repeatable) |
| `--define:<k>=<v>` | Replace an identifier with a value (repeatable) |
| `--loader:<ext>=<l>` | Loader for a file extension (repeatable) |
| `--watch` | Watch for changes |
| `--metafile` | Write metafile JSON |
| `--analyze` | Analyze bundle size |
| `--jsx=<mode>` | `transform`, `preserve`, `automatic` |
| `--tsconfig=<file>` | Path to `tsconfig.json` |
| `--log-level=<level>` | `verbose`, `debug`, `info`, `warning`, `error`, `silent` |
| `--color` / `--no-color` | Force colour on/off |

> Repeated options use a **colon**, not an equals sign: `--external:react`, `--define:DEBUG=false`, `--loader:.svg=file`. Every other value option uses `--name=value`.

```bash
guchho build src/index.html --outdir=dist
guchho build src/app.js --minify --sourcemap
guchho build --watch
guchho build src/main.ts --target=es2020 --format=esm --external:react --define:__DEV__=false
```

### `guchho dev`

Development server: watches, rebuilds on change, serves over HTTP.

| Option | Description |
| ------ | ----------- |
| `--host=<host>` | Bind host (default: `localhost`) |
| `--port=<port>` | Bind port (default: `3000`) |
| `--open` | Open a browser on start |
| `--outdir=<dir>` | Output directory (default: `dist`) |
| `--minify` | Minify output (default: off) |
| `--minify-whitespace` | Minify whitespace only |
| `--minify-identifiers` | Mangle identifiers only |
| `--minify-syntax` | Optimize syntax only |
| `--sourcemap` | Enable source maps |
| `--target=<target>` | Target environment |
| `--log-level=<level>` | Log level |

```bash
guchho dev
guchho dev --port=8080 --open
guchho dev --host=0.0.0.0
```

### `guchho serve`

Serves already-built output. No rebuilding, no build flags.

| Option | Description |
| ------ | ----------- |
| `--host=<host>` | Bind host (default: `localhost`) |
| `--port=<port>` | Bind port (default: `3000`) |
| `--open` | Open a browser on start |

```bash
guchho serve
guchho serve dist --port=8080
```

### `guchho watch`

Watches and rebuilds, but serves nothing — for consumers that are not a browser (Electron, a native shell, a test runner).

Takes the same options as `guchho build`.

```bash
guchho watch src/index.html
guchho watch --outdir=dist
```

### `guchho init`

```
Usage: guchho init [options]
```

Create a new Guchho project in the current directory. The directory is the target — create it first, then run `init` inside it. Existing files are left alone unless `--force` is given, and nothing is written at all when the directory holds work that is not a Guchho starter.

| Option | Description |
| ------ | ----------- |
| `-t`, `--template=<name>` | Starter template (default: `basic`) |
| `--type=<type>` | Project type (default: `app`) |
| `-y`, `--yes` | Use default options without prompting |
| `-f`, `--force` | Overwrite existing scaffold files |
| `-h`, `--help` | Show help |

| Type | What you get |
| ---- | ------------ |
| `app` | Web application |
| `lib` | Reusable JavaScript/TypeScript library |
| `plugin` | Guchho build-system plugin |

| Template | Language |
| -------- | -------- |
| `basic` | JavaScript + CSS |
| `ts` | TypeScript + CSS |
| `jsx` | JavaScript + JSX + CSS |
| `tsx` | TypeScript + JSX + CSS |

Without `--type` and `--template` on the command line, and without `--yes`, the command asks for both in the terminal. `--yes` takes the defaults instead of asking; it changes nothing about what is written.

```bash
guchho init
guchho init --template=ts
guchho init --type=lib --template=ts
guchho init --type=plugin --template=ts
```

### `guchho clean`

Removes generated build output.

| Option | Description |
| ------ | ----------- |
| `--dry-run` | Show what would be removed, without deleting |

```bash
guchho clean --dry-run
guchho clean
```

### `guchho info`

Prints the platform, the build type, and the configuration file in use.

```
  Platform:      win32-x64
  Build:         Release
  Config:        guchho.config.js
```

## Configuration file

Guchho reads the first of these it finds next to the project:

1. `guchho.config.js`
2. `guchho.config.json`
3. `guchho.json`

**Precedence: command-line flag → config file → built-in default.** A flag you pass always wins.

```js
// guchho.config.js
export default {
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
    format: 'esm',
    target: 'esnext',
    minify: true,
    pretty: true,
    minifyHtml: false,
  },
}
```

The same document as JSON:

```json
{
  "build": {
    "entry": "src/index.html",
    "outdir": "dist",
    "format": "esm",
    "target": "esnext",
    "minify": true
  }
}
```

Any option a flag accepts may be set here, using the flag's name in camelCase (`outdir`, `minifyHtml`, `treeShaking`, `logLevel`, …). Unknown fields are reported as warnings rather than ignored, so a typo is visible.

## JavaScript API

```js
// ESM
import { build, transform, context, analyze, stop, version } from 'guchho'

// CommonJS
const { build, transform, context } = require('guchho')
```

| Export | Returns |
| ------ | ------- |
| `build(options)` | `Promise<BuildResult>` |
| `transform(code, options?)` | `Promise<TransformResult>` |
| `context(options)` | `Promise<BuildContext>` |
| `analyze(config)` | `Promise<AnalysisResult>` |
| `analyzeMetafile(metafile)` | `Promise<string>` |
| `stop()` | `Promise<void>` |
| `version` | `string` |
| `lexer(source, options)` | `Promise<LexResult>` |
| `parse(source, options)` | `Promise<ParseResult>` |
| `print(ast, options)` | `Promise<PrintResult>` |
| `BuildFailure` `ServiceError` `BuildContext` | error and class types |

Each compiler stage is one function told which language it is working on, rather than one function per stage per language. `language` is `"html"`, `"css"` or `"js"`, and it is required — inferring it from the source text is guessing.

Every call starts a long-lived native service the first time it is used and reuses it afterwards, so a process that builds a hundred times starts the engine once.

### Build

```js
import { build, BuildFailure } from 'guchho'

try {
  const result = await build({
    entryPoints: ['src/index.html'],
    outdir: 'dist',
    format: 'esm',
    platform: 'browser',
    minify: true,
    sourcemap: true,
    target: 'es2020',
    metafile: true,
    splitting: true,
    treeShaking: true,
    define: { __DEV__: 'false' },
    external: ['react', 'react-dom'],
    loader: { '.svg': 'dataurl' },
  })

  console.log(`${result.outputFiles?.length ?? 0} files written`)
} catch (err) {
  if (err instanceof BuildFailure) {
    for (const m of err.errors) {
      console.error(`${m.location?.file}:${m.location?.line} — ${m.text}`)
    }
  } else {
    throw err
  }
}
```

**Writing vs. collecting.** By default the build writes to disk and returns diagnostics. Pass `write: false` to get the outputs in memory instead — nothing touches the filesystem:

```js
const { outputFiles } = await build({
  entryPoints: ['src/main.ts'],
  write: false,
  format: 'esm',
  minify: true,
})

for (const file of outputFiles) {
  console.log(file.path, file.contents.byteLength, file.text.slice(0, 80))
}
```

**Named entry points.** An entry can be a path, an `{ out, in }` record, or a `[out, in]` pair:

```js
await build({
  entryPoints: [
    { in: 'src/index.html', out: 'app' },
    'src/admin.ts',
  ],
  outdir: 'dist',
})
```

**Working from memory.** `stdin` builds source that never touches disk:

```js
const { code } = await transform('export const x: number = 1', { loader: 'ts' })
```

### Transform

One piece of source in, one piece of code out. No project, no file system.

```js
import { transform } from 'guchho'

const { code, map, errors, warnings } = await transform('const add = (a, b) => a + b', {
  loader: 'js',
  format: 'cjs',
  minify: true,
  sourcemap: true,
  target: 'es2017',
})
```

Loaders: `js`, `jsx`, `ts`, `tsx`, `json`, `text`, `css`, `html`, `base64`, `dataurl`, `file`, `binary`, `copy`, `empty`. A `transform` takes a single loader name; a `build` takes a loader **map** keyed by extension.

### Context: rebuild, watch, serve

A context holds everything the engine learned reading your project, which is what makes the second build cheaper than the first. Dispose it when you are done — it holds a watcher and sometimes a listening socket.

```js
import { context, stop } from 'guchho'

const ctx = await context({
  entryPoints: ['src/index.html'],
  outdir: 'dist',
  minify: true,
})

const first = await ctx.rebuild()

// Watch: the engine rebuilds on change, and you ask for the result.
await ctx.watch({ delay: 100 })
const second = await ctx.rebuild()

// Or serve the outputs over HTTP.
const { host, port, hosts } = await ctx.serve({ port: 3000 })

await ctx.dispose()
await stop()
```

> **How rebuilds arrive.** Guchho's service is request-and-response only, so it never sends anything unasked: `watch()` starts watching, and `rebuild()` collects the result. Pass `onRebuild` to `watch()` if you want the callback shape for the rebuilds you drive yourself. A true push would need a notification packet on the protocol, which does not exist yet — so it is documented rather than faked.

### Analyze

`analyze()` takes a configuration, builds without writing, and answers with a structured account of what the bundle is made of: which inputs fed which outputs, how many bytes each contributed, and what was left external. It is data, not prose, so it can be checked in a test or fed to a size budget.

`analyzeMetafile()` answers the older question — it renders a metafile as a human-readable report.

```js
import { build, analyze, analyzeMetafile } from 'guchho'

// Structured: the same build, in a form code can assert on.
const analysis = await analyze({
  entryPoints: ['src/index.html'],
  bundle: true,
})
console.log(analysis.outputs, analysis.inputs, analysis.dependencies)

// Prose: a metafile rendered as a report.
const { metafile } = await build({
  entryPoints: ['src/index.html'],
  metafile: true,
  write: false,
})
console.log(await analyzeMetafile(metafile))
```

Note that `analyze` is the structured one and `analyzeMetafile` is the report. In 1.x `analyze()` *was* the report, so code passing a metafile to `analyze()` now gets a configuration error rather than a string.

```bash
guchho build src/index.html --metafile --analyze
```

### `stop()`

Ends the service process, once it has actually ended.

```js
import { stop } from 'guchho'

await stop()
```

Await this before deleting a build's output directory. While the process is alive it holds those files open, and removing them fails — `EPERM` on Windows.

### Compiler API

The engine's own lexer, parser and printer. One function per stage, told which language it is for — not one function per stage per language, which is the same three operations spelled twelve times.

Each stage sends one request to the same service a build uses, so the code you get is the engine's code.

The AST never crosses the wire: a parse answers with a numeric **handle** and a structural summary, and a print turns that handle back into text.

```js
import { lexer, parse, print } from 'guchho'

// Lex: every token, with byte offsets and line/column.
const { tokens, count } = await lexer('const a = 1', { language: 'js' })
// tokens[0] → { kind: '"const"', value: 'const', start: 0, end: 5, line: 1, column: 0, length: 5 }
// "kind" is the engine's own name for the token type, so a token kind this
// package has never heard of arrives as itself rather than as a number.

// Parse: a handle, plus a summary. A syntax error resolves with ok: false.
const parsed = await parse('const a = 1; a + 1', { language: 'js', sourcefile: 'demo.js' })
// parsed.ok, parsed.symbols → [{ name: 'a', useCount: 2 }], parsed.partCount

const printed = await print(parsed.ast, { language: 'js', minifySyntax: true })

// CSS: parse, then print back to source.
const css = await parse('.used { color: red } .dead { color: blue }', { language: 'css' })
const out = await print(css.ast, { language: 'css', minifyWhitespace: true })
```

| Stage | Call | Language-specific options |
| ----- | ---- | ------------------------- |
| Lex | `lexer(source, { language })` | `includeComments` (CSS) |
| Parse | `parse(source, { language, sourcefile })` | `fragment`, `collectImportRecords`, `collectInlineCode` (HTML); `minifyWhitespace`, `minifySyntax`, `minifyIdentifiers` (CSS, JS) |
| Print | `print(ast, { language })` | `pretty`, `minify`, `scriptingEnabled` (HTML); `minifyWhitespace`, `asciiOnly` (CSS); `minifyWhitespace`, `minifySyntax`, `minifyIdentifiers`, `asciiOnly` (JS) |

A summary is the language's own: `nodes` for HTML, `rules` for CSS, `parts` and `symbols` for JS. `sourcefile` is worth setting on a parse — without it a syntax error reports a position and no file.

A handle is only valid for the language it came from, and passing one to the wrong printer throws rather than being sent somewhere it does not belong.

There is no handle-level transform. Dead-rule removal and the rest of the engine's rewrites are reached through [`transform(code, { loader })`](#transform), which takes source and answers with source; adding a fourth function taking a handle would be a second way to do the same work.

### Error handling

Two failure shapes, with two different fixes.

| Error | Meaning | Fix |
| ----- | ------- | --- |
| `BuildFailure` | The build failed. Carries `errors` and `warnings`. | Fix the code. |
| `ServiceError` | The service could not be reached or could not answer. Carries an optional `code`. | Fix the installation. |

```js
import { build, BuildFailure, ServiceError } from 'guchho'

try {
  await build({ entryPoints: ['src/index.html'] })
} catch (err) {
  if (err instanceof BuildFailure) {
    // Your code has a problem. Structured diagnostics, with notes and locations.
    for (const message of err.errors) {
      console.error(message.pluginName, message.text)
      if (message.location) {
        console.error(`  at ${message.location.file}:${message.location.line}:${message.location.column}`)
        if (message.location.suggestion) console.error(`  did you mean: ${message.location.suggestion}`)
      }
      for (const note of message.notes) console.error(`  note: ${note.text}`)
    }
    process.exitCode = 1
  } else if (err instanceof ServiceError) {
    console.error('The guchho service could not start. Try reinstalling the package.')
    process.exitCode = 2
  } else {
    throw err
  }
}
```

Match on the class, not on the message text — message wording changes between releases; the class does not.

## TypeScript

Types ship with the package. Nothing to install, nothing to configure.

```ts
import { build, type BuildOptions, type BuildResult, BuildFailure } from 'guchho'

const options: BuildOptions = {
  entryPoints: ['src/index.html'],
  outdir: 'dist',
  format: 'esm',
  minify: true,
}

try {
  const result: BuildResult = await build(options)
  if (result.metafile) {
    for (const [path, out] of Object.entries(result.metafile.outputs)) {
      console.log(path, out.bytes)
    }
  }
} catch (err) {
  if (err instanceof BuildFailure) {
    const first = err.errors[0]
    console.error(first?.location?.line, first?.text)
  }
}
```

The `BuildResult` fields are typed against the options you passed: `outputFiles` is `OutputFile[]` only when `write: false`, and `metafile` only when `metafile: true`.

## Supported values

| Option | Values |
| ------ | ------ |
| `loader` | `js`, `jsx`, `ts`, `tsx`, `json`, `text`, `css`, `html`, `base64`, `dataurl`, `file`, `binary`, `copy`, `empty` |
| `format` | `iife`, `cjs`, `esm`, `umd`, `amd`, `system` |
| `platform` | `browser`, `node`, `neutral` |
| `jsx` | `transform`, `preserve`, `automatic` |
| `logLevel` | `verbose`, `debug`, `info`, `warning`, `error`, `silent` |
| `legalComments` | `none`, `inline`, `eof`, `linked`, `external` |
| `charset` | `ascii`, `utf8` |
| `sourcemap` | `true`, `false`, `inline`, `external`, `linked`, `both` |
| `target` | `es2015`…`esnext`, `chrome…`, `firefox…`, `safari…`, `node…`, `ie…` |

## Platform support

`guchho` installs the matching `@guchho/*` binary automatically. All 26 are published as optional dependencies, so npm picks the right one and skips the rest.

| OS | Architectures |
| -- | ------------- |
| Linux | `x64`, `ia32`, `arm64`, `arm`, `loong64`, `mips64el`, `ppc64`, `riscv64`, `s390x` |
| macOS | `x64`, `arm64` |
| Windows | `x64`, `ia32`, `arm64` |
| Android | `x64`, `arm64`, `arm` |
| FreeBSD | `x64`, `arm64` |
| NetBSD | `x64`, `arm64` |
| OpenBSD | `x64`, `arm64` |
| OpenHarmony | `arm64` |
| Solaris (SunOS) | `x64` |
| AIX | `ppc64` |

Missing yours? Build from source and set `GUCHHO_BINARY`, or open an issue — the engine is portable C++20 and builds with MSVC, GCC, Clang, Ninja or Visual Studio.

## Environment variables

| Variable | Effect |
| -------- | ------ |
| `GUCHHO_BINARY` | Absolute path to the binary to run. Highest priority; a bad path fails loudly instead of falling back. |
| `GUCHHO_NODE_BIN` | Path to the `node` executable used to evaluate a JavaScript `guchho.config.js`. Defaults to `node` on `PATH`. |
| `GUCHHO_PROFILE` | When present, enables internal timing traces on stderr. `GUCHHO_PROFILE=1` and `GUCHHO_PROFILE=` both enable it. |

## Troubleshooting

**`Could not find guchho binary for <platform>-<arch>`**
The optional platform package was skipped — usually by `--no-optional`, or a lockfile pinned for another machine. Install it directly:

```bash
npm install @guchho/<platform>-<arch>
```

**`guchho: No pre-built binary available for <platform>-<arch>`**
The same situation, printed at install time. The package still installs and the JavaScript API still loads; only running the binary needs a build from source. Point `GUCHHO_BINARY` at it.

**`guchho exited with code <n>`**
The build ran and failed. Run the command directly to see the diagnostics — the shim inherits stdio, so nothing is hidden:

```bash
npx guchho build src/index.html --log-level=verbose
```

**Builds are fine, but the output is not picking up my change**
Check which config file is in play (`guchho info`) and whether a stale binary is being used (`GUCHHO_BINARY`). In a checkout, a `build/` directory from another toolchain can win if it is newer.

**`Invalid build flag: --foo`**
The grammar does not know that flag. Flags are validated by the engine itself, so a typo is rejected rather than ignored. Run `guchho build --help` for the list.

**Deleting `dist/` gives `EPERM` on Windows**
The service still has the files open. `await stop()` before cleaning up.

## Contributing

Builds, tests and packaging instructions live in [`CONTRIBUTING.md`](https://github.com/guchho/guchho/blob/master/CONTRIBUTING.md). The short version:

```bash
# Build from source (CMake 3.25+, MSVC/GCC/Clang)
bash scripts/build.sh release-win32-x64
bash scripts/test.sh

# Run the npm package's own test suite
bash scripts/test-npm.sh
```

Issues and feature requests: [github.com/guchho/guchho/issues](https://github.com/guchho/guchho/issues).

## License

MIT — see [`LICENSE`](https://github.com/guchho/guchho/blob/master/LICENSE).

Author: [Hemanta Gayen](https://github.com/sponsors/code-hemu) · Repository: [github.com/guchho/guchho](https://github.com/guchho/guchho) · npm: [guchho](https://www.npmjs.com/package/guchho)
