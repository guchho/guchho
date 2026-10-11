---
title: guchho watch
description: Reference for the watch command.
---

# `guchho watch`

```text
Usage: guchho watch [entry...] [options]
```

Watch source files and rebuild on changes (no server). This is the command for
when whatever reads the output is not a browser; unlike [`dev`](dev.md), it
writes files and leaves the rest alone.

## Options

The options are the same as [`guchho build`](build.md).

## Examples

```bash
guchho watch src/index.html
guchho watch --outdir=dist
```
