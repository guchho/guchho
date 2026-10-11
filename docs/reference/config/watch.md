---
title: Watch options
description: Reference for the watch configuration section.
---

# Watch options

The options the watcher reads from a configuration's `watch` section. `watch`
may also simply be `true`, meaning "build and keep watching".

| Option | Type | Meaning |
| --- | --- | --- |
| `delay` | `number` | Milliseconds of quiet before a change is taken as final. |
| `interval` | `number` | How often the handle rebuilds on its own, in milliseconds. Zero means never. Absent, the handle drives itself. |

## See also

- [Dev server & watch](../../guide/dev-server-and-watch.md)
- [`guchho watch`](../cli/watch.md)
