---
title: Tree shaking
description: Removing unused exports and unreachable statements.
---

# Tree shaking

Tree shaking removes unused exports and unreachable statements.

## When it is on

`treeShaking` defaults to **enabled** when bundling or when the output is an
IIFE, because in both cases there is no way for a consumer to concatenate more
code onto the end of the file and rely on the parts that were dropped.

Otherwise it is **disabled**, so a file being rewritten still behaves like the
original.

| Value | Meaning |
| --- | --- |
| `default` | The rule above. |
| `false` | Never remove anything. |
| `true` | Always remove; safe even for a non-bundled file. |

```bash
guchho build src/app.js --bundle --tree-shaking
```

## Side effects

Tree shaking can only drop a module when loading it has no observable effect.
An `onResolve` hook can answer that question with a `sideEffects` value:

- `true` (default) - keep the module even if unused.
- `false` - it is side-effect free; drop it when nothing uses it.

Only mark a module side-effect free when loading it genuinely has no observable
effect, because getting it wrong deletes code that was doing work.

## Purity annotations

`/* @__PURE__ */` marks a call as removable when its result is unused. The
`pure` option lists functions that may be treated the same way, and
`ignoreAnnotations` makes the engine treat the comment as ordinary text.

See [Writing plugins](../plugins/README.md) for the hook that declares side
effects.
