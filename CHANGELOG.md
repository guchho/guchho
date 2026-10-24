# Changelog

All notable changes to Guchho. Newest first. Full release notes live in
[docs/release-notes](docs/release-notes/README.md); tags are on
[GitHub](https://github.com/guchho/guchho/tags).

## [Unreleased]

### Added

- `guchho unpack` extracts zip, tar and tar.gz archives, with `--outdir`,
  `--format`, `--overwrite` and `--strip-components`. Absolute paths and `..`
  components are refused, and symlinks are skipped with a warning. Tar
  headers are checksum-verified and PAX records are honoured; gzip streams
  are validated against their CRC-32 and length trailer.
- `unpack()` in the `guchho` package extracts an archive from JavaScript,
  with the same default destination, format detection, overwrite rule and
  component stripping as `guchho unpack`. It is native-only; the browser entry
  exports a refusing stub because the WASM engine has no host filesystem.

## [1.0.8] - 2026-10-19

### Added

- The compiler API - `lexer`, `parse` and `print` - now runs on the
  WebAssembly engine (`@guchho/wasm`), in Node and in the browser, for `html`,
  `css` and `js`. The `guchho` package falls back to it native-first when no
  native binary is installed.
- Typed browser entry for the `guchho` package: a `browser.d.ts` and explicit
  named exports, so `import { build, lexer } from "guchho"` is a real named
  binding with types.

### Fixed

- `stop()` no longer abandons its exit wait on an idle service.
- The handle-to-language map behind `parse()`/`print()` is cleared when the
  native service ends, so a restarted service cannot attribute a reused handle
  id to the wrong language.

[1.0.8]: docs/release-notes/v1.0.8.md

## [1.0.7] - 2026-10-15

### Added

- `@guchho/wasm`: the engine compiled to WebAssembly, with the CLI's own flag
  grammar, diagnostics and exit codes. On Node the host file system is mounted
  through NODEFS; in the browser a `files` map feeds an in-memory file system.
  Opt-in - install it explicitly and it is used automatically.
- Engine selection overrides: `GUCHHO_BACKEND=wasm|native` forces an engine,
  `GUCHHO_BINARY=/path/to/guchho` spawns a specific native binary.

### Fixed

- Linux and macOS npm packages shipped a binary without the execute bit,
  failing `npx guchho` and global installs with `Permission denied` (exit 126).
  The bit is now restored on download, enforced at publish, and repaired at
  runtime and after install as a safety net.

[1.0.7]: docs/release-notes/v1.0.7.md

## [1.0.6] - 2026-10-14

### Changed

- When a config file's root is an array of configurations, each element's
  `plugins` now run for that element's build only. A single-configuration root
  and a flat `plugins` list behave as before. From C++, the same behaviour is
  available through the new `RunWithPluginGroups`.

[1.0.6]: docs/release-notes/v1.0.6.md

## [1.0.5] - 2026-10-13

### Added

- A JavaScript plugin system in the Rollup shape: plain objects with `name` and
  the hooks `buildStart`, `resolveId`, `load`, `buildEnd`, `generateBundle`,
  `writeBundle` and `closeBundle`, passed through `plugins` on `build()` and
  `transform()` or the config file. Hooks run across the service pipe, so the
  engine stays a separate native process.
- The whole documentation tree: guide, reference, JavaScript API, C++ embedding,
  recipes, plugins and resources sections, plus a blog and release notes. API
  and embedding pages are generated from `index.d.ts` and the public headers.

[1.0.5]: docs/release-notes/v1.0.5.md

## [1.0.4] - 2026-10-11

### Added

- `guchho pack` creates an archive from files and directories, with `--format`
  (`zip`, `tar`, `tar.gz`), `--level` (0-9) and `--allow-overwrite`.
- `pack()` does the same from JavaScript, returning the archive bytes.

### Distribution

- The npm publish pipeline gained an `android-x64` target with a prebuilt
  binary.

[1.0.4]: docs/release-notes/v1.0.4.md

## [1.0.3] - 2026-10-07

### Changed

- Engine refinements across the compiler, CSS, HTML and linker stages, with
  more consistent diagnostics and graph resolution aligned with the documented
  rules.

### Distribution

- Chocolatey packaging was added and verified.
- The npm publish workflow was corrected, including the publish cascade for the
  `guchho` package type.

[1.0.3]: docs/release-notes/v1.0.3.md

## 1.0.2 - 2026-10-06

See the tag [`v1.0.2`](https://github.com/guchho/guchho/releases/tag/v1.0.2).

## 1.0.1 - 2026-10-08

First published release. See the tag
[`v1.0.1`](https://github.com/guchho/guchho/releases/tag/v1.0.1).
