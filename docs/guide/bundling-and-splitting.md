---
title: Bundling & splitting
description: Inlining the graph, extracting shared chunks, and keeping packages external.
---

# Bundling & splitting

Guchho can do two quite different things with a set of source files:

- **Convert** each file on its own (no graph, no imports resolved).
- **Bundle** the whole import graph into one or more outputs, resolving and
  inlining imports as it goes.

Everything in this page - `external`, `alias`, `packages`, code splitting -
only applies to the bundling mode, because each one needs a graph to act on.
With `bundle` off, a file is merely rewritten and imports are left as written.

## Bundle

`bundle` inlines the whole graph into the output. Without it, each input file is
merely converted.

```bash
guchho build src/app.js --bundle --outfile=dist/app.js
```

```js
// src/app.js
import { greet } from "./greet.js";
import pkg from "./package.json";

greet(pkg.name);
```

```js
// src/greet.js
export function greet(name) {
  console.log(`Hello, ${name}!`);
}
```

After `--bundle`, `dist/app.js` contains both modules and no `import` statement:
`greet` is inlined, and the JSON is turned into an object literal. Without
`--bundle`, `dist/app.js` still contains `import { greet } from "./greet.js"`.

Two consequences worth knowing: `external` and `alias` only mean something when
bundling, and using either without it is reported as an error, because there is
no graph for them to act on.

### Bundle vs convert at a glance

| | `bundle: false` (default) | `bundle: true` |
| --- | --- | --- |
| Imports resolved | no | yes |
| Imports inlined | no | yes |
| `external` applies | no (error) | yes |
| `alias` applies | no (error) | yes |
| `splitting` applies | no | yes |
| Tree shaking | limited | yes |

An HTML entry point turns bundling on automatically when it was not specified
either way, because an HTML document whose scripts are not bundled is rarely
what you want.

## Code splitting

`splitting` extracts code shared by several entry points into chunks that those
entries import. It requires ES module output.

```js
build: {
  entry: ['src/a.js', 'src/b.js'],
  outdir: 'dist',
  format: 'esm',
  bundle: true,
  splitting: true,
}
```

```js
// src/shared.js
export function shared() {
  return "used by both entries";
}

// src/a.js
import { shared } from "./shared.js";
console.log("a", shared());

// src/b.js
import { shared } from "./shared.js";
console.log("b", shared());
```

With `splitting`, `shared.js` does not appear twice. Instead it is emitted once
as a chunk, and both `a.js` and `b.js` import that chunk:

```text
dist/
  a.js        -> imports chunks/shared-<hash>.js
  b.js        -> imports chunks/shared-<hash>.js
  chunks/
    shared-<hash>.js
```

Without splitting, `shared()` would be duplicated into both outputs - correct,
but wasteful for a large shared dependency.

### When splitting helps

Splitting pays off when:

- several entry points import the same modules,
- a dependency is large (a framework, a date library, a design system), and
- users often navigate between the entry points, so a shared chunk is cached
  once and reused.

It is less useful for a single entry point, where there is no sharing to
extract, and for output formats that are not ES modules.

### Splitting requirements

- `bundle` must be on: splitting operates on the module graph.
- `format` must be `esm`. Browser bundles using `iife` cannot load chunks.
- There must be more than one entry point, or the graph must contain a module
  that is imported from more than one place.
- Chunk output names follow `chunkNames`.

```js
build: {
  entry: ['src/a.js', 'src/b.js'],
  outdir: 'dist',
  format: 'esm',
  splitting: true,
  chunkNames: 'chunks/[name]-[hash]',
}
```

## External packages

`external` keeps the named paths out of the graph and leaves the import
statement in the output:

```bash
guchho build src/app.js --bundle --external:react --external:react-dom
```

```js
build: {
  bundle: true,
  external: ['react', 'react-dom'],
}
```

With `format: 'esm'`, an import of `react` stays `import ... from "react"` in
the emitted bundle. For formats that do not preserve ESM syntax (such as
`cjs`), the external import becomes a runtime `require()` call. Either way, the
consumer supplies the package at runtime instead of receiving a second copy of
React inside the bundle.

An entry matches either exactly or as a `prefix*` / `*suffix` / `*middle*`
wildcard, so `--external:'@scope/*'` keeps a whole scope out. Entry points are
never external: an entry has to be a real file.

```js
// Keep one package and every subpath of a scope out of the bundle
build: {
  bundle: true,
  external: ['lodash', '@my-org/*', '*.node'],
}
```

`packages` is the package-wide switch: `bundle` (default) or `external`. It
applies to bare specifiers; relative and absolute paths still follow `external`
and the graph.

```js
build: {
  bundle: true,
  packages: 'external',   // leave every bare import as-is
}
```

This is the blunt version of `external`: use it for a Node build that should
keep `node_modules` on disk rather than inline it. Reach for `external` when
only a handful of packages should stay imports.

### `external` vs `packages`

| Need | Use |
| --- | --- |
| Keep React out of a library bundle | `external: ['react']` |
| Keep a whole scope out | `external: ['@scope/*']` |
| Keep every bare import out | `packages: 'external'` |
| Inline everything (default) | `packages: 'bundle'` |

## Aliasing

`alias` redirects a specifier before resolution starts:

```js
build: {
  alias: { '@': './src', 'lodash': 'lodash-es' },
}
```

```js
// src/app.js
import { helper } from "@/helper.js";   // resolves to ./src/helper.js
import debounce from "lodash";           // resolves to lodash-es
```

Aliases are prefix-based, so `@/x` becomes `./src/x` while an unrelated
specifier like `@scope/pkg` is untouched. Aliases run before Node resolution, so
they can also redirect a package name to a local file or a different package.

Alias and external interact in a predictable order: alias rewrites the
specifier first, then external decides whether the result stays an import. That
means you can alias a package and still mark it external by its new name.

## Choosing an entry export

`exports` selects the value the bundle exposes:

| Value | Meaning |
| --- | --- |
| `named` | A namespace object (the default). |
| `default` | The entry's default export is the value. |
| `none` | Publish nothing; valid only when nothing is exported. |
| `auto` | Infer from the entry's actual exports. |

All four are read for every output format. See [Formats](../reference/formats.md).

```js
// src/lib.js
export default function main() {}
export const helper = () => {};
```

| `exports` | Result as an IIFE global `App` |
| --- | --- |
| `named` | `App = { default: main, helper }` |
| `default` | `App = main` |
| `none` | no global assigned |
| `auto` | inferred from the entry's exports |

`none` is an error when the entry does export something, because there is no
sensible way to publish it while asking for nothing. Use `named` or `default`
for libraries, and `none` for a side-effect-only entry (for example, a script
that installs a global and exports nothing).

## Output names

Output file names are built from the `[dir]`, `[name]`, `[hash]` and `[ext]`
placeholders:

```js
build: {
  entryNames: '[name]-[hash]',
  chunkNames: 'chunks/[name]-[hash]',
  assetNames: 'assets/[name]-[hash]',
}
```

| Placeholder | Expands to |
| --- | --- |
| `[dir]` | The file's directory, relative to `outbase` |
| `[name]` | The file's base name, without extension |
| `[hash]` | A content hash of the output |
| `[ext]` | The file's extension, including the dot |

Every URL that points at a renamed file is rewritten to match, so the HTML,
CSS and JS stay connected after hashing.

```js
build: {
  entryNames: '[dir]/[name].[hash]',
  chunkNames: 'chunks/[name]-[hash]',
  assetNames: 'assets/[name]-[hash][ext]',
  publicPath: 'https://cdn.example.com/',
}
```

`publicPath` prefixes the URLs the bundle emits. It is separate from naming:
names decide what a file is called, `publicPath` decides how it is addressed
from a page served elsewhere. See [Deployment](deployment.md).

### A complete naming setup

```js
build: {
  entry: ['src/home.js', 'src/about.js'],
  outdir: 'dist',
  format: 'esm',
  bundle: true,
  splitting: true,
  entryNames: 'js/[name]-[hash]',
  chunkNames: 'js/chunks/[name]-[hash]',
  assetNames: 'assets/[name]-[hash]',
  publicPath: '/static/',
}
```

Produces something like:

```text
dist/
  js/home-a1b2c3.js
  js/about-d4e5f6.js
  js/chunks/shared-789abc.js
  assets/logo-0f1e2d.png
```

And every reference inside the JS, CSS and HTML is rewritten to
`/static/js/...` and `/static/assets/...`.

## Tree shaking

Tree shaking removes exports that nothing imports. It is on by default when
bundling or emitting an IIFE, and off for a plain ESM transform, so an explicit
setting is only needed to turn it off.

```js
build: {
  bundle: true,
  treeShaking: false,   // keep every export, even unused ones
}
```

```js
// src/math.js
export const used = () => "kept";
export const unused = () => "dropped";
```

```js
// src/app.js
import { used } from "./math.js";
console.log(used());
```

With tree shaking on, `unused` is not in the output. With it off, both functions
survive. A module that has side effects is kept whole unless package metadata
marks it as side-effect free - which is exactly what the `sideEffects` field in
`package.json` is for.

## Putting it together

A library build that keeps peer dependencies external and publishes ESM:

```js
// guchho.config.js
export default {
  build: {
    entry: ['src/index.js'],
    outdir: 'dist',
    format: 'esm',
    bundle: true,
    packages: 'bundle',
    external: ['react', 'react-dom'],
    exports: 'named',
    entryNames: '[name]',
    sourcemap: 'linked',
    target: 'es2020',
  },
};
```

An application build split across pages, with hashed assets on a CDN:

```js
// guchho.config.js
export default {
  build: {
    entry: ['src/home.js', 'src/dashboard.js'],
    outdir: 'dist',
    format: 'esm',
    bundle: true,
    splitting: true,
    minify: true,
    sourcemap: 'linked',
    entryNames: 'js/[name]-[hash]',
    chunkNames: 'js/chunks/[name]-[hash]',
    assetNames: 'assets/[name]-[hash]',
    publicPath: '/static/',
  },
};
```

## Common pitfalls

- **Using `external` or `alias` without `bundle`.** Both are graph operations;
  without a graph they are errors.
- **Splitting with `iife` or `cjs`.** Splitting requires ES module output.
- **Expecting one entry to split.** There is nothing to share; you need two or
  more entries (or several importers of the same module).
- **Externalizing an entry point.** Entries must be real files and are never
  externalized.
- **Forgetting to hash assets.** Without `[hash]`, a changed file keeps its old
  name and clients may serve a stale cache.
- **Hashing but not rewriting URLs.** Guchho rewrites URLs for you - do not
  hand-write asset paths in HTML or CSS.
- **Turning off tree shaking "to be safe".** It usually just makes the bundle
  larger; fix the missing `sideEffects` metadata instead.

## See also

- [Formats](../reference/formats.md) - `exports`, `format` and what each output
  format publishes.
- [Configuration: build](../reference/config/build.md) - `bundle`,
  `splitting`, `external`, `alias`, `packages` and every naming option.
- [Assets & loaders](assets-and-loaders.md) - how non-JS files reach the
  output.
- [Deployment](deployment.md) - `publicPath`, content hashing and source maps
  for production.
