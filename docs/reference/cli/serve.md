---
title: guchho serve
description: Reference for the static server command.
---

# `guchho serve`

```text
Usage: guchho serve [dir] [options]
```

Serve built output without rebuilding. The one positional argument is a
directory of already-built output; it defaults to the usual output directory.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--host=<host>` | `localhost` | Bind host. |
| `--port=<port>` | `3000` | Bind port. |
| `--open` | off | Open a browser on start. |

`serve` has no build flags. Use [`guchho dev`](dev.md) when you want a rebuild.

## Examples

```bash
guchho serve
guchho serve dist --port=8080
```
