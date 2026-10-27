---
title: CLI overview
description: The eleven Guchho commands at a glance.
---

# CLI overview

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
  lint [files...]     Lint JavaScript and TypeScript
  pack <input...>     Create an archive
  unpack <archive>    Extract an archive
```

Run `guchho <command> --help` for command-specific help, and `guchho --version`
for the version. With no command, `guchho` prints the usage text and exits `0`.

## Commands at a glance

| Command | What it does | Reads config | Writes files |
| --- | --- | --- | --- |
| `build` | Build HTML, JS, CSS and assets for production | yes | yes |
| `dev` | Build, serve, watch and live-reload | yes | yes |
| `serve` | Serve already-built output, no rebuild | yes (for messages) | no |
| `watch` | Rebuild on change, no server | yes | yes |
| `init` | Scaffold a new project in the working directory | no | yes |
| `clean` | Remove generated output | yes | deletes |
| `info` | Report environment and configuration | yes | no |
| `transform` | Read stdin, write transformed source to stdout | yes | no |
| `lint` | Standalone linter with its own config | its own | only with `--fix` |
| `pack` | Create a zip, tar or tar.gz | no | archive |
| `unpack` | Extract a zip, tar or tar.gz | no | extracted files |

## Global flags

These are read by the dispatcher before any command runs, or are the long
spelling of an option every command shares:

| Flag | Meaning |
| --- | --- |
| `-h`, `--help` | Print help. On its own it prints the usage text above; after a command it prints that command's help. |
| `-v`, `--version` | Print `guchho v<version>` and nothing else. |
| `--quiet` | Minimal output; errors still print. |
| `--verbose` | Detailed output; the long form of `--log-level=verbose`. |
| `--log-level=<level>` | `verbose`, `debug`, `info`, `warning`, `error`, `silent`. |
| `--color` / `--no-color` | Force colour on or off. Colour otherwise follows whether stdout is a terminal. |

## How the dispatcher reads a command line

1. The first non-flag argument is the command. A bare path with no command word
   means `build`, so `guchho src/index.js` builds that file.
2. Anything after the command belongs to it. `--help` before a command prints
   the usage text; `--help` after a command prints that command's help.
3. `--version` and `-v` are answered only when the whole argument list is that
   one word. `guchho build --version` is a flag the build grammar rejects, not
   the version command; use `guchho --version` instead.
4. A malformed command line exits `2` before any work begins.

## A typical workflow

```bash
guchho init -y                      # scaffold
guchho dev                          # develop with live rebuilds
guchho build --minify --sourcemap   # production build
guchho serve                        # preview the output
guchho lint src                     # check source without building
guchho pack dist -o release.zip     # archive it
guchho unpack release.zip -o check  # extract a copy
```

### Starting a project

```bash
# In an empty directory
guchho init -y

# TypeScript library
guchho init --type=lib --template=ts -y

# A Guchho plugin
guchho init --type=plugin --template=ts -y
```

`init` writes a config, a source file and the usual project files. It refuses a
directory that already holds conflicting files unless `--force` is given, and it
never removes anything outside the generated set.

### Developing

```bash
guchho dev                            # localhost:3000, live reload
guchho dev --port=8080 --open         # different port, open a browser
guchho dev --host=0.0.0.0             # reachable from other machines
```

`dev` builds, serves and watches in one process. Every HTML response gets a
`/guchho.js` script tag, and the page reloads when a rebuild changes an output.
Use it while you are iterating.

### Building for production

```bash
guchho build                                    # entry from config
guchho build src/index.html --outdir=dist       # explicit entry
guchho build src/app.js --bundle --minify       # bundle and minify
guchho build src/app.js --sourcemap=linked      # external source maps
guchho build src/lib.js --format=esm --external:react
```

A build writes to `outdir` (`dist` by default) and prints the config file it
used. On failure it exits `1` and prints diagnostics with locations.

### Watching without a server

```bash
guchho watch src/index.html
guchho watch --outdir=dist
```

This is the command for when something other than a browser consumes the
output - a test runner, an Electron shell, a static server you run yourself.
Unlike `dev`, it writes files and leaves serving alone.

### Serving output

```bash
guchho serve                     # serve the configured output directory
guchho serve dist --port=8080
guchho serve dist --open
```

`serve` does not rebuild. Build first, then serve, when you want to inspect
exactly what a deploy would ship.

### Transforming a single input

```bash
echo 'const x: number = 1' | guchho transform --loader=ts
cat src/app.tsx | guchho transform --loader=tsx --minify > app.js
```

`transform` reads stdin and writes stdout. It is the command-line form of the
`transform` API: one input, one loader, configured options. Use it in a shell
pipeline, not for a project.

### Linting

```bash
guchho lint                       # every supported file under the cwd
guchho lint src                   # a directory
guchho lint src/app.js            # one file
guchho lint --fix                 # apply safe fixes
guchho lint --format=json         # machine-readable output
guchho lint --max-warnings=0      # fail on any warning
guchho lint --rule no-console:error
```

`guchho lint` is separate from the optional parser checks used during a build
(`build.lint`). It runs its own rule set, applies safe fixes, and has its own
config file (`guchho.lint.config.*`). It exits `1` when there is a lint error or
the warning threshold is exceeded, and `2` for usage/config failures.

### Cleaning

```bash
guchho clean --dry-run    # show what would be removed
guchho clean              # remove build output
```

`clean` removes generated output, not source. Use `--dry-run` first on a project
whose layout you are unsure of.

### Archiving

```bash
guchho pack dist                       # -> dist.zip
guchho pack dist -o release.zip
guchho pack dist src -o release.zip    # both trees
guchho pack dist --format=tar          # uncompressed tar
guchho pack dist --format=tar.gz --level=9

guchho unpack release.zip              # -> release/
guchho unpack release.zip -o check
guchho unpack bundle.zip --strip-components=1
guchho unpack release.zip --overwrite
```

`pack` stores each input under its last path component (`dist` -> `dist/`).
`unpack` detects the format from the extension, then from magic bytes, and
refuses absolute paths and `..` components so an archive cannot escape the output
directory.

## Configuration in the CLI

Guchho walks up from the working directory and stops at the first directory that
holds a config file. Within one directory, the first of these wins:

1. `guchho.config.js`
2. `guchho.config.json`
3. `guchho.json`

`build`, `dev`, `watch` and `serve` print a `Using <file>` line so a build that
landed somewhere unexpected says which file decided it. They stop with an error
if the nearest config exists but cannot be used.

## Precedence

**Command-line flag → config file → built-in default.** A flag you pass always
wins over the same option in the config file, and the config file wins over the
built-in defaults. See the [configuration reference](../reference/config/README.md).

```bash
# Config says outdir: "dist"; the flag wins
guchho build --outdir=build
```

## Output and diagnostics

- Errors and warnings go to stderr, each with a file, line and column where
  there is one, plus a caret under the offending source.
- `transform` writes only the transformed source to stdout, so it is safe to
  pipe. Build banners, the `Using <config>` line and summaries may go to stdout
  instead, so pipe `build` only when you mean to capture its chatter.
- `--quiet` suppresses progress lines but still prints errors.
- `--log-level=silent` suppresses everything except a fatal error.
- `--color` / `--no-color` override the terminal detection; on Windows colour is
  rendered as console attributes rather than raw ANSI.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success. Also returned when nothing was asked (empty command line, lone `--help` / `--version`). |
| `1` | Build failed. |
| `2` | CLI usage error, or something the binary cannot do. |
| `130` | Interrupted (SIGINT). |

`guchho lint` refines this: `1` also means a lint error or too many warnings, and
`2` covers config and operational failures. See
[Exit codes](../reference/exit-codes.md).

```bash
guchho build --minify || echo "build failed: $?"
```

## Choosing the engine

`guchho` is a wrapper around an engine. On a machine without native binaries,
`@guchho/wasm` can run every command except `serve`, `watch` and `dev`. Set
`GUCHHO_BACKEND=wasm` to force it, or `GUCHHO_BINARY=/path/to/guchho` to pin a
native binary. See [WebAssembly](../wasm.md#choosing-an-engine).

```bash
GUCHHO_BACKEND=wasm npx guchho build src/index.html --outdir=dist
GUCHHO_BINARY=./build/guchho npx guchho build
```

## Common pitfalls

- **Flags after a command belong to that command.** `guchho build --version`
  does not print the version.
- **`serve` never rebuilds.** Run `guchho build` (or use `dev`) first.
- **`watch` has no server.** Use `dev` if you want a browser-facing server.
- **`transform` needs a loader.** It has no extension to infer from; pass
  `--loader=ts`, `--loader=tsx`, and so on.
- **`external` and `alias` need `--bundle`.** Without a graph they are errors.
- **The nearest config wins.** A nested `guchho.config.js` shadows the one at
  the repository root; the `Using …` line names the winner.
- **`pack` names inputs by their last component.** `guchho pack dist/` stores
  `dist/...`, not the files at the archive root.

## Command reference

Each command has a full page:

- [build](../reference/cli/build.md)
- [dev](../reference/cli/dev.md)
- [serve](../reference/cli/serve.md)
- [watch](../reference/cli/watch.md)
- [init](../reference/cli/init.md)
- [clean](../reference/cli/clean.md)
- [info](../reference/cli/info.md)
- [transform](../reference/cli/transform.md)
- [lint](../reference/cli/lint.md)
- [pack](../reference/cli/pack.md)
- [unpack](../reference/cli/unpack.md)

## See also

- [Getting started](getting-started.md) - a first build end to end.
- [Configuration](configuration.md) - config files and precedence.
- [Bundling & splitting](bundling-and-splitting.md) - what build flags change.
- [Exit codes](../reference/exit-codes.md) - every code a run can return.
