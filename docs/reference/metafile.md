---
title: Metafile
description: The shape of the build graph Guchho reports.
---

# Metafile

A build asked for `metafile` produces a graph of what it read and wrote.

```js
export default {
  build: {
    entrypoints: ['src/index.js'],
    metafile: true,
  },
}
```

```ts
interface Metafile {
  inputs: {
    [path: string]: {
      bytes: number
      imports: { path: string; kind: string; external?: boolean }[]
      format?: string
    }
  }
  outputs: {
    [path: string]: {
      bytes: number
      inputs: { path: string; bytesInOutput: number }[]
      imports: { path: string; kind: string; external?: boolean }[]
      exports: string[]
      entryPoint?: string
    }
  }
}
```

## Reading it

The top-level `inputs` map names the **entry points and only the entry points**.
The transitive set is the contribution map on each output - the `inputs` under
`outputs`. Reading the top-level map as a record of everything the build read
reports a single file for a build that read fifty.

Each output's `imports` lists what it still imports at runtime - anything not
inlined. Because `bundle` defaults to `false`, a relative sibling import is
listed here and left alone on purpose.

## Analysis

- `analyze(options)` runs a build with `write: false` and answers with an
  `AnalysisReport`.
- `analyzeMetafile(metafile)` pretty-prints a metafile as a text report.

## See also

- [JavaScript API](../api/README.md)
- [`--analyze`](cli/build.md)
