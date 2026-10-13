---
title: guchho dev
description: Reference for the development server command.
---

# `guchho dev`

```text
Usage: guchho dev [options]
```

Start a development server with file watching and automatic rebuilds.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--host=<host>` | `localhost` | Bind host. |
| `--port=<port>` | `3000` | Bind port. |
| `--open` | off | Open a browser on start. |
| `--outdir=<dir>` | `dist` | Output directory. |
| `--minify` | off | Minify output. |
| `--minify-whitespace` | - | Minify whitespace only. |
| `--minify-identifiers` | - | Mangle identifiers only. |
| `--minify-syntax` | - | Optimize syntax only. |
| `--sourcemap` | - | Enable source maps. |
| `--target=<target>` | - | Target environment. |
| `--log-level=<level>` | `info` | Log level. |
| `--color` / `--no-color` | auto | Force colour on or off. |
| `--quiet` | off | Minimal output. |
| `--verbose` | off | Detailed output. |

The build flags are the same ones [`guchho build`](build.md) accepts.

## Examples

```bash
guchho dev
guchho dev --port=8080 --open
guchho dev --host=0.0.0.0
```

## See also

- [Dev server & watch](../../guide/dev-server-and-watch.md)
- [guchho serve](serve.md), [guchho watch](watch.md)
