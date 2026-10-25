---
title: JavaScript & TypeScript
description: Parsing, transforming and targeting JavaScript, TypeScript, JSX and TSX.
---

# JavaScript & TypeScript

Guchho parses JavaScript, TypeScript, JSX and TSX with its own native parser.
TypeScript annotations are erased; the runtime is plain JavaScript.

## Loaders

| Loader | Meaning |
| --- | --- |
| `js` | JavaScript |
| `jsx` | JavaScript containing markup |
| `ts` | Typed source; annotations erased |
| `tsx` | Typed source that also contains markup |
| `json` | JSON, parsed into an object with named imports |

See [Assets & loaders](assets-and-loaders.md) for the rest.

## Targets and engines

`target` names a language edition (`es2020`, `esnext`, …); `engines` names the
browsers and runtimes you support and lets the engine compute the level. Syntax
or APIs above the target are lowered automatically. See
[Targets & browsers](targets-and-browsers.md).

## JSX

The `jsx` option selects the runtime:

- `transform` (default) - classic calls through `jsxFactory` / `jsxFragment`.
- `automatic` - imports helpers from `jsxImportSource`.
- `preserve` - leave the markup untouched.

## Compile-time substitution

`define` replaces identifiers and dotted paths with literal values:

```js
build: {
  define: { __DEV__: 'false', 'process.env.NODE_ENV': '"production"' },
}
```

`pure` lists functions that may be dropped or reordered when unused (the
programmatic form of a `/* @__PURE__ */` annotation). `keepNames` preserves
function and class names while minifying.

## Optional lint checks

Guchho can report two suspicious-but-valid patterns: references to names that
are not defined (`undefined`) and declarations that are never read (`unused`).
Both are off by default.

```js
build: {
  lint: {
    undefined: 'warning',
    unused: 'error',
  },
}
```

These build-time checks are configured only through `build.lint`. For the full
rule set, autofixes, file discovery and a dedicated configuration file, use
[`guchho lint`](../reference/cli/lint.md). See the
[build options reference](../reference/config/build.md#optional-lint-checks)
for the build-time fields.

## Compiler stages

The lexer, parser and printer are exposed through the
[JavaScript API](../api/compiler.md), so the engine's own stages can be embedded
in other tools.
