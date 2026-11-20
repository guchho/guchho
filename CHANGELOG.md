# Changelog

All notable changes to Guchho. Newest first. Full release notes live in
[docs/release-notes](docs/release-notes/README.md); tags are on
[GitHub](https://github.com/guchho/guchho/tags).

## [1.1.0] - 2026-11-20

### Added

- JavaScript plugins can rewrite module source with `transform(code, id, info)`.
  It runs after `load` and before parsing, every matching transform sees the
  code left by the previous one, and returning `null` declines. A result may
  carry `code` as a string or `Uint8Array`, a JSON `map`, `meta` for later hooks,
  and `watchFiles`/`watchDirs`. A map is used as the module's input source map
  when source maps are enabled, in preference to a `sourceMappingURL` comment;
  transforms are skipped for `copy` and `binary` modules. The hook's `info`
  carries `namespace`, `suffix`, `loader`, `with` and `meta`. From C++, the same
  hook is available as `PluginBuild::on_transform`, with `OnTransformOptions`,
  `OnTransformArgs` and `OnTransformResult`.
- `buildStart` can register virtual modules through `this.addVirtualModule()` or
  a `virtualModules` array in its result. A module needs an `id` and `code`;
  `namespace`, `suffix`, `loader`, `resolveDir`, `meta`, `watchFiles` and
  `watchDirs` are optional. Registered modules resolve like a normal import and
  are loaded from memory; an empty id, a suffix not starting with `?` or `#`,
  and two modules with the same namespace, id and suffix are reported as
  errors. From C++, `OnStartResult` gains `virtual_modules` and the matching
  `VirtualModule` struct.
- `this.resolve()` now answers from inside a hook using the build's own resolver
  and plugin chain. A nested service request carries the calling build's token,
  and the calling plugin is skipped by default (`custom: true`) so a plugin that
  rewrites imports cannot answer its own question; pass `custom: false` to
  allow that. `load` and `transform` hooks also receive the import attributes
  in `info.with`.
- The JavaScript `Loader` type now declares the internal spellings
  `default`, `global-css`, `local-css` and `none`.

- `guchho test` delegates to the optional `@guchho/test` runner, passing every
  argument after `test` through unchanged so `run`, `watch`, file arguments and
  options behave like the matching `guchho-test` command. It is handled by the
  npm entry point before any engine starts, prints an installation message when
  the runner is missing, and requires Node.js 20.19 or newer.
- `pack()`, `unpack()`, `packBytes()` and `unpackBytes()` in `@guchho/wasm`,
  with the same options as the native package but paths inside the engine's
  filesystem. `packBytes()` and `unpackBytes()` keep the archive and extracted
  files in memory, so both directions work in the browser.
- The browser entry of the `guchho` package now supports `pack()`.
  `unpack()` still refuses there; use `unpackBytes()` from `@guchho/wasm`.
- JavaScript plugins can shape a build from `buildStart`. A start hook may
  return `entries`, `watchFiles` and `watchDirs`, and `this.addWatchFile()` now
  adds a dependency instead of doing nothing. Contributed entries are merged
  with the configured ones, so a plugin can supply a virtual module as an entry
  without writing it to disk. `this.meta.watchMode` reports whether the current
  pass is a watch rebuild, and an emitted asset with a relative name is written
  under the output directory. From C++, `OnStartResult` gains the matching
  `entries`, `watch_files` and `watch_dirs` fields.

### Fixed
- Plugins attached to a long-lived context no longer read freed options. The
  context owns the options handed to plugin setup, so `initial_options` stays
  valid for the life of the session and `WatchMode` reaches hooks on rebuilds.

- Source map generation no longer reads out of bounds when a file has no line
  offset entries or a location precedes the first line. Those mappings are
  skipped instead of crashing the build.
- The syntax minifier no longer drops the declaration of an uninitialized local
  used by a lowered logical assignment. `let x; x ||= {}` now lowers to
  `let x; x || (x = {});` instead of assigning to an undeclared name.
- `lex-js` now tokenizes interpolated template literals and returns lexer
  failures as diagnostics. An interpolated template previously threw an
  unrecoverable lexer error, which the playground displayed as
  `Lex failed: [object Object]` instead of a useful answer.
- `parse-js` now accepts TypeScript, JSX and TSX source as well as plain
  JavaScript. The playground sends all four through `parse-js`, so the default
  TypeScript sample previously failed with a syntax error. Requests can also
  select a dialect with the `typescript` and `jsx` options, or with a
  `sourcefile` extension.
- `guchho watch`, `guchho build --watch` and `guchho serve` run an opening
  build as soon as they enable watch mode. Watch mode records paths only on
  passes that run after it is enabled, so without that pass the watcher started
  with no paths: `serve` did not notice a change until a browser requested a
  page, and the other commands waited for a rebuild before watching anything.

### Distribution

- AIX PPC64 is a build and publish target, using a self-hosted runner.
- Homebrew and WinGet packaging scripts were added.

[1.1.0]: docs/release-notes/v1.1.0.md

## [1.0.9] - 2026-10-26

### Added

- `guchho lint`, a standalone JavaScript and TypeScript linter with its own
  configuration file, fifteen rules, safe fixes, watch mode, human/stylish/JSON
  output and a warning threshold. `--fix`, `--fix-dry-run`, `--rule`,
  `--config` and `--max-warnings` mirror the documented command behaviour.
- `lint()` in the `guchho` package runs the same rule set and safe fixes and
  returns the findings as data. It works in the browser when the lint config is
  JSON; a JavaScript config is native-only.
- Optional build-time JavaScript lint checks, off by default, enabled through
  `build.lint`: `undefined`, `unused`, `unused-variable` and
  `unused-parameter`. Findings can be silenced or promoted through
  `log_override`; analysis is platform-aware and stays silent in `with` scopes
  and files containing direct `eval`.
- Context-aware JavaScript syntax diagnostics: misspelled keywords and missing
  closing delimiters now carry structured suggestions while keeping the
  existing fallback message text unchanged.
- `guchho unpack` extracts zip, tar and tar.gz archives, with `--outdir`,
  `--format`, `--overwrite` and `--strip-components`. Absolute paths and `..`
  components are refused, and symlinks are skipped with a warning. Tar
  headers are checksum-verified and PAX records are honoured; gzip streams
  are validated against their CRC-32 and length trailer.
- `unpack()` in the `guchho` package extracts an archive from JavaScript,
  with the same default destination, format detection, overwrite rule and
  component stripping as `guchho unpack`. It is native-only; the browser entry
  exports a refusing stub because the WASM engine has no host filesystem.

### Changed

- Android builds target API level 24 (Android 7.0) and link the C++ runtime
  statically, so packaged binaries start on a device without the NDK runtime
  library.

[1.0.9]: docs/release-notes/v1.0.9.md

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
