---
title: CSS pipeline
description: Building, scoping and minifying stylesheets.
---

# CSS pipeline

Guchho parses CSS with its own parser and can bundle stylesheets alongside
JavaScript and HTML.

## Loaders

| Loader | Meaning |
| --- | --- |
| `css` | A stylesheet |
| `global-css` | Rules land in the global scope |
| `local-css` | Rules stay local to the importing module |

`local-css` scopes rules so that unused ones can be dropped (the same idea as a
CSS module). `global-css` emits rules that cannot be tree-shaken.

## Imports and URLs

- `@import` rules are resolved and inlined like module imports.
- `composes: ... from ...` in a CSS module is resolved as a module reference.
- `url(...)` references are resolved and, when bundling, rewritten to point at
  the emitted asset.

## Minification

`--minify-whitespace`, `--minify-syntax` and the `minify` shorthand apply to CSS
as well as JavaScript. See [Minification](minification.md).

## Compiler stages

CSS can be parsed and printed through the
[JavaScript API](../api/compiler.md):

```js
import { parse, print } from 'guchho'

const css = await parse('.used { color: red } .dead { color: blue }', { language: 'css' })
const out = await print(css.ast, { language: 'css', minifyWhitespace: true })
```
