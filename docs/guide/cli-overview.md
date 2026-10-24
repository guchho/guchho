---
title: CLI overview
description: The ten Guchho commands at a glance.
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
  unpack <archive>    Extract an archive
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
guchho unpack release.zip -o check  # extract a copy
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
- [unpack](../reference/cli/unpack.md)
