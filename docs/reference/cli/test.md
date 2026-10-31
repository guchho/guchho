---
title: guchho test
description: Run tests through the optional @guchho/test runner.
---

# `guchho test`

```text
Usage: guchho test [command] [files...] [options]
```

`guchho test` delegates to the `@guchho/test` runner. Every argument after
`test` is passed through unchanged, so the runner's `run` and `watch` commands,
file arguments, and options work as they do with `guchho-test`:

```bash
guchho test
guchho test watch
guchho test src/math.test.ts
guchho test run --filter math --reporter=verbose
```

Run `guchho test --help` for the full runner help.

## Installation

`@guchho/test` is an optional peer dependency. It is not installed with guchho,
and `guchho test` prints an installation message when it is missing:

```bash
npm install --save-dev @guchho/test
```

The runner requires Node.js 20.19 or newer. Guchho itself continues to support
Node.js 18; only the `test` command needs the newer runtime. The command works
where Node can resolve `@guchho/test` from the project or from the guchho
installation, so it is intended for npm-managed projects. Binary-only
distributions such as zip, Chocolatey, Homebrew, and WinGet do not go through
the npm entry point, so they do not provide `guchho test`.

## Interaction with guchho

`guchho test` is handled by the npm entry point before guchho loads a build
engine. It does not read a guchho build configuration, start the native binary,
or require a build. Test configuration belongs to `@guchho/test`; see its
`--config` option and discovery rules.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | All selected tests passed. |
| `1` | A test failed, a coverage threshold was not met, or `@guchho/test` is not installed. |
| `2` | The runner rejected the command line or could not discover or execute tests. |

## See also

- [CLI reference](README.md)
- [`@guchho/test` CLI reference](https://github.com/guchho/test/blob/main/docs/cli.md)
