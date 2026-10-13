---
title: Scaffold a project
description: Start from a working tree with guchho init.
---

# Scaffold a project

`guchho init` writes a small but complete project - a config file, an entry
point, and a package manifest - into the current directory. It takes no
directory argument: make the directory first.

```bash
mkdir my-app
cd my-app
guchho init
guchho build
```

Without `-y`/`--yes` the command asks which shape you want; with it, the
defaults are used and nothing is prompted.

## Choose the shape

Two independent choices decide the files: the project **type** and the
**template** (which sets the source extension).

```bash
guchho init --type=lib --template=ts      # a TypeScript library
guchho init --type=plugin                 # a Guchho plugin skeleton
guchho init -y                            # defaults: app, basic
```

Types are `app` (default), `lib` and `plugin`; templates are `basic` (default),
`ts`, `jsx` and `tsx`.

## What it will and will not touch

- A directory that already holds files this scaffold would not have written is
  refused, unless `--force` is given.
- An individual file that is already there is left alone, unless `--force` is
  given.
- Nothing outside the generated set is ever removed.

## Then what

The generated `guchho.config.json` is where the build is described; every field
is listed in the [configuration reference](../reference/config/README.md). From
there:

- [Build a single-page app](build-a-single-page-app.md) adds an HTML entry.
- [Serve and watch](serve-and-watch.md) gets an edit-refresh loop going.

## See also

- [`guchho init`](../reference/cli/init.md)
- [Getting started](../guide/getting-started.md)
