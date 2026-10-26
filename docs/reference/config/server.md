---
title: Server options
description: Reference for the server configuration section.
---

# Server options

The options the JavaScript API's `serve()` reads from a configuration's
`server` section.

| Option | Type | Meaning |
| --- | --- | --- |
| `port` | `number` | The port to bind. Zero or absent asks the engine to try from 8000 upward and take the first free port. |
| `host` | `string` | The interface to bind. Absent, every interface (`0.0.0.0`) is bound. |
| `servedir` | `string` | The directory to serve. Defaults to the build's output directory. |
| `fallback` | `string` | Served when nothing else matches, which is how SPA routes are handled. |
| `open` | `boolean` | Accepted by the schema. The engine does not launch a browser; use the CLI's `--open` for that. |

The CLI is a separate surface: `guchho dev` and `guchho serve` take
`--host`, `--port` and `--open` as flags, plus `guchho serve [dir]` for the
directory, default to `localhost:3000`, and warn that a config file's `server`
section is ignored rather than applying it. Use the JavaScript API when the
server must be configured from the file.

## See also

- [Dev server & watch](../../guide/dev-server-and-watch.md)
- [`guchho serve`](../cli/serve.md), [`guchho dev`](../cli/dev.md)
