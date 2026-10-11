---
title: Server options
description: Reference for the server configuration section.
---

# Server options

The options the dev server reads from a configuration's `server` section.

| Option | Type | Meaning |
| --- | --- | --- |
| `port` | `number` | The port to bind. Zero or absent asks the engine to try from 8000 upward and take the first free port. |
| `host` | `string` | The interface to bind. Absent, every interface is bound. |
| `servedir` | `string` | The directory to serve. Defaults to the build's output directory. |
| `fallback` | `string` | Served when nothing else matches, which is how SPA routes are handled. |

`open` is accepted at runtime and refused with an explanation: a config written
for `guchho serve` may carry it, and a server that silently did not open a
browser is worse than one that says it cannot.

## See also

- [Dev server & watch](../../guide/dev-server-and-watch.md)
- [`guchho serve`](../cli/serve.md), [`guchho dev`](../cli/dev.md)
