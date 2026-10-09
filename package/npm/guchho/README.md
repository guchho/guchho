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

**Fast, HTML-first bundler, compiler and build tool for modern web applications. It parses HTML, JavaScript, TypeScript, JSX/TSX, CSS and assets.**

**Native, not transpiled.** The engine is a single C++20 binary. Parsing, linking, minification and tree shaking happen in native code, not in a JavaScript runtime, so a build does not spend its time in a garbage collector or in `node_modules`.

**HTML-first.** A web application is a document, not a module. Guchho starts at your HTML entry, follows every `<script>`, `<link>`, `import` and asset reference it finds, and produces a complete output directory - hashed filenames, rewritten URLs, minified, ready to upload.

**One tool, not five.** Lexer, parser, transformer, printer, minifier, linker, resolver, watcher and dev server are all in the same binary. There is no plugin process, no worker pool, and no separate CSS toolchain to configure.

**A real API.** The CLI is a thin shim over a long-lived service protocol. Everything `guchho build` does is also callable from JavaScript - including the compiler stages (`lexer`, `parse`, `print`), so the engine can be embedded in your own tools.

**A programmatic surface that stays out of the way.** `build()` is one call and one result. `context()` holds the expensive half of a build so the second one is cheap. Both ship with full TypeScript declarations.

## Quick start

```bash
# 1. Create a project (src/index.html, src/main.js, src/style.css, guchho.config.js)
npx guchho init -y

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


## Configuration file

Guchho walks up from the working directory and stops at the first directory
that holds a config file — the nearest one wins, and the directories above
it are never consulted. Within one directory, the first of these is used:

1. `guchho.config.js`
2. `guchho.config.json`
3. `guchho.json`

`guchho build` prints the config file it is using, so a build that landed
somewhere unexpected says which file decided it. `guchho dev`, `guchho watch`
and `guchho serve` print the same `Using …` line, and all of them stop with an
error if the nearest config exists but cannot be used.

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
    define: { __DEV__: 'false', __VERSION__: '"1.0.0"' },
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
    "minify": true,
    "define": { "__DEV__": "false", "__VERSION__": "\"1.0.0\"" }
  }
}
```

Any option a flag accepts may be set here, using the flag's name in camelCase (`outdir`, `minifyHtml`, `treeShaking`, `logLevel`, …). Unknown fields are reported as warnings rather than ignored, so a typo is visible.

`define` values are the JavaScript expression to substitute, written as a string: bare `false`, `true` and numbers need no quoting of their own, while string values carry their quotes inside the text (`__VERSION__: '"1.0.0"'`). A `--define:` flag on the command line wins for the keys it names; every other key the config set still applies.

## JavaScript API

```js
import { build } from 'guchho'

const result = await build({
  entrypoints: ['src/index.html'],
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
```

### UMD: exporting the default directly

`exports: 'default'` exposes the entry point's default export as the UMD global itself, rather than as a namespace object whose properties are the export names. `new Exprify()` then works on the global because the global *is* the class. Currently supported for UMD output only; an entry point without a default export is an error, and named exports alongside the default are not exposed in this mode.

```js
await build({
  entrypoints: ['src/exprify.js'],
  outfile: 'dist/exprify.js',
  format: 'umd',
  name: 'Exprify',
  exports: 'default',
})
```

This is a Guchho option in the Rollup tradition of export selection — not an esbuild option. The same value may be written in a config file under `build.exports` or passed on the command line as `--exports=default`.

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

### Compiler API

The engine's own lexer, parser and printer. One function per stage, told which language it is for - not one function per stage per language, which is the same three operations spelled twelve times.

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

A summary is the language's own: `nodes` for HTML, `rules` for CSS, `parts` and `symbols` for JS. `sourcefile` is worth setting on a parse - without it a syntax error reports a position and no file.

A handle is only valid for the language it came from, and passing one to the wrong printer throws rather than being sent somewhere it does not belong.

There is no handle-level transform. Dead-rule removal and the rest of the engine's rewrites are reached through [`transform(code, { loader })`](#transform), which takes source and answers with source; adding a fourth function taking a handle would be a second way to do the same work.

## License

Guchho is licensed under **MIT**. Copyright © [CodeHemu](https://github.com/code-hemu).
