---
title: Minification
description: Whitespace, identifier and syntax minification, plus property mangling.
---

# Minification

Minification is a choice about the bytes a build leaves behind, and a person who
has not made it should get output they can still read. All minification is off
by default.

## The three switches

They are independent, so `minifyWhitespace` alone is a safe way to shrink a file
for debugging while keeping names intact.

| Option | What it does |
| --- | --- |
| `minifyWhitespace` | Remove insignificant whitespace |
| `minifyIdentifiers` | Shorten local identifiers |
| `minifySyntax` | Apply size-reducing rewrites |

`--minify` on the command line is shorthand for all three:

```bash
guchho build src/app.js --minify
guchho build src/app.js --minify-whitespace   # only one
```

## Dropping code

- `drop` - `drop: ['console', 'debugger']` removes `console.*` calls and
  `debugger` statements.
- `dropLabels` - remove a labelled statement outright, the classic way of
  fencing off development-only code:

  ```js
  build: { dropLabels: ['DEBUG'], define: { DEBUG: 'false' } }
  ```
  ```js
  while (DEBUG) { /* development-only */ }   // the whole loop is removed
  ```

## Keeping names

`keepNames` preserves function and class names while minifying, which matters
when stack traces or reflection read them.

## Property mangling

| Option | Meaning |
| --- | --- |
| `mangleProps` | Regex matching property names to shorten |
| `reserveProps` | Regex of names to protect |
| `mangleQuoted` | Also shorten `obj["name"]` string keys |
| `mangleCache` | Carry name decisions between builds |

`mangleCache` keeps a project's shortened property names stable across builds, so
a hot-reload session does not invalidate cached files. See the
[CLI reference](../reference/cli/build.md) for the file-backed spelling.
