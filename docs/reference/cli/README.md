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
  lint [files...]     Lint JavaScript and TypeScript
  test [args...]      Run tests with @guchho/test
  pack <input...>     Create an archive
  unpack <archive>    Extract an archive

Run 'guchho <command> --help' for command-specific help.
Run 'guchho --version' for version information.
```

The native usage text printed by `guchho --help` does not list `test` yet; the
command is handled by the npm entry point. `guchho test --help` shows the test
runner's own help.

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
| `lint` | [lint.md](lint.md) | Lint JavaScript and TypeScript without building. |
| `test` | [test.md](test.md) | Run tests through the optional `@guchho/test` runner. |
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

## Linting

`guchho lint` is the standalone linter for JavaScript and TypeScript. It runs
the full rule set, supports safe autofixes and uses its own configuration file;
see [lint.md](lint.md).

The optional parser checks used during a build are configured through
`build.lint`, not command-line flags. See
[the build options reference](../config/build.md#optional-lint-checks).

## Testing

`guchho test` delegates to the optional [`@guchho/test`](https://github.com/guchho/test)
runner. Install it as a development dependency before using the command:

```bash
npm install --save-dev @guchho/test
guchho test
```

Every argument after `test` is passed to the runner, so its commands and options
work unchanged: `guchho test watch`, `guchho test run --bail`, and
`guchho test src/math.test.ts` all do what the matching `guchho-test` command
does. The runner requires Node.js 20.19 or newer; see [test.md](test.md).

## Precedence

An option set on the command line wins over the same option in a configuration
file. A configuration file wins over the built-in defaults. See the
[configuration reference](../config/README.md).
