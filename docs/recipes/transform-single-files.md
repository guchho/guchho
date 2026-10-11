---
title: Transform single files
description: Turn one source through stdin into stdout.
---

# Transform single files

`guchho transform` reads one input from standard input and writes the result to
standard output. It is the command-line form of the programmatic
[`transform`](../api/README.md#transform) API: a loader, a target and the usual
options, with no entry points and no file system walking.

```bash
guchho transform --loader=ts < src/index.ts > dist/index.js
```

Tell it how to read the input with a loader and how far back to compile with a
target:

```bash
guchho transform --loader=jsx --jsx=automatic < src/view.jsx
guchho transform --loader=ts --target=es2020 --minify < src/index.ts
```

For a TypeScript file whose settings live in a project, point at the config so
paths and compiler options are read from it:

```bash
guchho transform --loader=ts --tsconfig=tsconfig.json < src/index.ts
```

## When to reach for it

- Compile one file in a pipeline of your own.
- Check what the compiler does to a snippet without setting up a build.
- Feed generated source in without writing a temporary file.

Because it reads stdin and writes stdout, it composes: pipe it, redirect it, or
embed it in a shell script.

## See also

- [`guchho transform`](../reference/cli/transform.md)
- [JavaScript and TypeScript](../guide/javascript-and-typescript.md)
- [JavaScript API](../api/README.md)
