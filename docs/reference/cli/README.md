---
title: CLI reference
description: Every Guchho command line command, its arguments and its options.
---

# CLI reference

```text
Usage:
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
  unpack <archive>    Extract an archive

Run 'guchho <command> --help' for command-specific help.
Run 'guchho --version' for version information.
```

## Commands

| Command | Page | Purpose |
| --- | --- | --- |
| `build` | [build.md](build.md) | Production-oriented build of HTML, JS, CSS and assets. |
| `dev` | [dev.md](dev.md) | Development server with file watching and rebuilds. |
| `serve` | [serve.md](serve.md) | Serve already-built output, without rebuilding. |
| `watch` | [watch.md](watch.md) | Watch and rebuild without starting a server. |
| `init` | [init.md](init.md) | Scaffold a new project in the working directory. |
| `clean` | [clean.md](clean.md) | Remove generated build output. |
| `info` | [info.md](info.md) | Report environment and configuration. |
| `transform` | [transform.md](transform.md) | Transform source read from stdin. |
| `pack` | [pack.md](pack.md) | Create a zip, tar or tar.gz archive. |
| `unpack` | [unpack.md](unpack.md) | Extract a zip, tar or tar.gz archive. |

## Global flags

These are read by the dispatcher before any command runs, or are the long
spelling of an option every command shares:

| Flag | Meaning |
| --- | --- |
| `-h`, `--help` | Print help. On its own it prints the usage text above; after a command it prints that command's help. |
| `-v`, `--version` | Print `guchho v<version>` and nothing else. |
| `--quiet` | Minimal output. |
| `--verbose` | Detailed output; the long form of `--log-level=verbose`. |
| `--log-level=<level>` | `verbose`, `debug`, `info`, `warning`, `error`, `silent`. |
| `--color` / `--no-color` | Force colour on or off. |

## Lint flags

Commands that build or transform JavaScript (`build`, `dev`, `watch` and
`transform`) accept `--lint` and `--lint-level` to enable the optional lint
checks. They are off by default; see
[the build options reference](../config/build.md#optional-lint-checks) for the
accepted values.

## Precedence

An option set on the command line wins over the same option in a configuration
file. A configuration file wins over the built-in defaults. See the
[configuration reference](../config/README.md).
