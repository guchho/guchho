---
title: CLI overview
description: The nine Guchho commands at a glance.
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
  pack <input...>     Create an archive
```

Run `guchho <command> --help` for command-specific help, and `guchho --version`
for the version.

## A typical workflow

```bash
guchho init -y                      # scaffold
guchho dev                          # develop with live rebuilds
guchho build --minify --sourcemap   # production build
guchho serve                        # preview the output
guchho pack dist -o release.zip     # archive it
```

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
- [pack](../reference/cli/pack.md)
