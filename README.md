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

| Distribution | Install from | Status |
| :-------------: | ------------ | ------ |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/npm_logo.svg" height="20" alt="npm"> | **npm**<br>`npm install -g guchho` | ✅ [Available](https://www.npmjs.com/package/guchho) |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/chocolatey_logo.png" height="45" alt="Chocolatey"> | **Chocolatey**<br>`choco install guchho` | ✅ [Available](https://community.chocolatey.org/packages/guchho/) |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/scoop_logo.png" height="40" alt="Scoop"> | **Scoop**<br>`scoop install guchho` | 🚧 Coming Soon |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/winget_logo.png" height="35" alt="WinGet"> | **WinGet**<br>`winget install Guchho.Guchho` | 🚧 Coming Soon |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/homebrew_logo.svg" height="40" alt="Homebrew"> | **Homebrew**<br>`brew install guchho` | 📅 Planned |
| <img src="https://raw.githubusercontent.com/guchho/.github/refs/heads/main/profile/image/cmake_logo.svg" height="35" alt="cmake"> | **CMake**<br>`bash scripts/build.sh` | 🛠️ Build from source. Clone the repository and build Guchho locally. |

## Install

The `guchho` package is the command line tool. On install it pulls the native
binary for your platform from a matching `@guchho/<os>-<cpu>` package.

```sh
npm install -g guchho      # CLI, global
npm install -D guchho      # CLI, per-project
```

## WebAssembly engine (optional)

Where a native binary cannot be installed - CI fallbacks, sandboxes,
StackBlitz/CodeSandbox, the browser - the same engine is available compiled to
WebAssembly in the opt-in [`@guchho/wasm`](package/npm/@guchho/wasm/README.md)
package:

```sh
npm install -D @guchho/wasm
npx guchho-wasm build src/index.html --outdir=dist
```

Once installed, the `guchho` wrapper uses it automatically (wasm-first). Two
environment variables override the choice:

- `GUCHHO_BACKEND=wasm|native` - force one engine, erroring if unavailable.
- `GUCHHO_BINARY=/path/to/guchho` - spawn a specific native binary.

Limitations of the WebAssembly engine: single-threaded, no `serve` / `watch` /
`dev`, no child processes, and a slower cold start than the native binary. See
[the WebAssembly guide](docs/wasm.md) for details.