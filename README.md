<div align="center">
<p>
  <a href="https://github.com/guchho">
      <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/guchho_logo.png" alt="guchho logo" width="200px" />
  </a>
</p>

<p>
  <a href="https://www.npmjs.com/package/guchho"><img src="https://img.shields.io/npm/v/guchho" alt="Version"></a>
  <a href="https://nodejs.org/en/about/previous-releases"><img src="https://img.shields.io/node/v/guchho.svg" alt="node compatibility"></a>
  <a href="https://github.com/guchho/guchho/blob/master/LICENSE"><img src="https://img.shields.io/github/license/guchho/guchho" alt="License"></a>
  <a href="https://github.com/guchho/guchho/issues"><img src="https://img.shields.io/github/issues/guchho/guchho" alt="Issues"></a>
  <a href="https://github.com/guchho/guchho/discussions"><img src="https://img.shields.io/github/discussions/guchho/guchho" alt="GitHub Discussions"></a>
  <a href="https://github.com/guchho/guchho/graphs/contributors"><img src="https://img.shields.io/github/contributors/guchho/guchho" alt="Contributors"></a>
  <a href="https://github.com/sponsors/code-hemu"><img src="https://img.shields.io/badge/Sponsor-GitHub-red" alt="Sponsor"></a>
</p>

<h1 align="center">Guchho</h1>
 <p>An all-in-one bundler, compiler and build tool. Parse HTML, JavaScript, TypeScript, JSX, CSS and assets, and produce a deployable output directory with a single command.
</p>

| Distribution | Install from | Status |
| :-------------: | ------------ | ------ |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/npm_logo.svg" height="20" alt="npm"> | **npm**<br>`npm install -g guchho` | ✅ Official Package Available |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/chocolatey_logo.png" height="45" alt="Chocolatey"> | **Chocolatey**<br>`choco install guchho` | ⏳ Pending review |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/scoop_logo.png" height="40" alt="Scoop"> | **Scoop**<br>`scoop install guchho` | 🚧 Coming Soon |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/winget_logo.png" height="35" alt="WinGet"> | **WinGet**<br>`winget install Guchho.Guchho` | 🚧 Coming Soon |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/homebrew_logo.svg" height="40" alt="Homebrew"> | **Homebrew**<br>`brew install guchho` | 📅 Planned |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/cmake_logo.svg" height="35" alt="cmake"> | **CMake**<br>`bash scripts/build.sh` | 🛠️ Build from source. Clone the repository and build Guchho locally. |

</div>

## Why Guchho

- **Native, not transpiled.** The engine is a single C++20 binary. Parsing, linking, minification and tree shaking happen in native code, not in a JavaScript runtime.
- **HTML-first.** Start at your HTML entry; Guchho follows every `<script>`, `<link>`, `import` and asset reference and produces a complete output directory - hashed filenames, rewritten URLs, minified, ready to upload.
- **One tool, not five.** Lexer, parser, transformer, printer, minifier, linker, resolver, watcher and dev server are all in the same binary - no plugin process, no worker pool, no separate CSS toolchain.
- **A real API.** The CLI is a thin shim over a long-lived service protocol. Everything `guchho build` does is callable from JavaScript, including the compiler stages (`lexer`, `parse`, `print`).
- **Failure is data.** Anything that can go wrong arrives as a diagnostic in the result, not an exception, so messages can be filtered, reworded or forwarded to an editor.

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

# 5. Package the built output into one archive
npx guchho pack dist -o release.zip
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

## CLI

```text
guchho <command> [options]

Commands:
  build [entry...]    Production build
  dev                 Development server with watch
  serve [dir]         Serve built output
  watch [entry...]    Watch and rebuild without server
  init                Initialize a new project
  clean               Remove build output
  info                Show environment info
  transform           Transform source from stdin
  pack <input...>     Create an archive
```

Run `guchho <command> --help` for command-specific help. The full list lives in the [CLI reference](docs/reference/cli/README.md).

## Configuration

Guchho walks up from the working directory and stops at the first directory that holds a config file - `guchho.config.js`, `guchho.config.json` or `guchho.json`, first match wins. Precedence is **command-line flag → config file → built-in default**.

```js
// guchho.config.js
export default {
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
    format: 'esm',
    target: 'esnext',
    minify: true,
    define: { __DEV__: 'false', __VERSION__: '"1.0.0"' },
  },
}
```

Any option a flag accepts may be set here, using the flag's name in camelCase. Unknown fields are reported as warnings rather than ignored, so a typo is visible. See the [configuration guide](docs/guide/configuration.md) and [reference](docs/reference/config/README.md).

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
  metafile: true,
  splitting: true,
  treeShaking: true,
})

console.log(`${result.outputFiles?.length ?? 0} files written`)
```

Beyond `build()`, the package exports `transform`, `context`, `watch`, `serve`, `pack`, `analyze`, `analyzeMetafile`, `stop`, and the compiler stages `lexer`, `parse` and `print` - all with full TypeScript declarations. The [API docs](docs/api/README.md) cover every function.

Plugins are plain objects in the Rollup shape - `name` plus hooks such as `resolveId`, `load`, `generateBundle` and `writeBundle` - passed through `plugins` on `build()` or the config file. See [writing plugins](docs/plugins/README.md).

## Documentation

The whole documentation tree lives in [docs/](docs/README.md) as plain Markdown, readable directly on GitHub:

- [Guide](docs/guide/README.md) - introduction, getting started, core concepts, HTML/CSS/JS pipelines, bundling, minification, source maps.
- [CLI reference](docs/reference/cli/README.md) and [configuration reference](docs/reference/config/README.md).
- [JavaScript API](docs/api/README.md) - generated from `index.d.ts`.
- [Embedding (C++)](docs/embedding/README.md) and [WebAssembly](docs/wasm.md).
- [Recipes](docs/recipes/README.md) - single-page apps, libraries, bundle analysis, and more.
- [Release notes](docs/release-notes/README.md) and [changelog](CHANGELOG.md).

The website is at [guchho.js.org](https://guchho.js.org).

## Contributing

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) for the workflow, and use `bash scripts/build.sh` plus `bash scripts/test.sh` to build and test locally. Please open an [issue](https://github.com/guchho/guchho/issues) or [discussion](https://github.com/guchho/guchho/discussions) before large changes.

## License

Guchho is licensed under **MIT**. Copyright © [CodeHemu](https://github.com/code-hemu).
