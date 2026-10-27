---
title: CSS pipeline
description: Building, scoping and minifying stylesheets.
---

# CSS pipeline

Guchho parses CSS with its own parser and can bundle stylesheets alongside
JavaScript and HTML. A stylesheet is a node in the same graph as a module: its
`@import` rules and `url(...)` references are followed, and the rules reach the
output in the order the graph says they should.

## How CSS enters the build

There are four common ways a stylesheet becomes part of the graph:

```js
// 1. Imported from JavaScript for its side effects
import './global.css';
```

```html
<!-- 2. Linked from an HTML entry -->
<link rel="stylesheet" href="./style.css" />
```

```css
/* 3. Imported from another stylesheet */
@import './colors.css';
```

```bash
# 4. Built directly as an entry point
guchho build src/style.css --outdir=dist
```

All four produce a `CSSRepr` in the graph. What happens next depends on the
loader and on whether the build is bundling.

## Loaders

| Loader | Meaning |
| --- | --- |
| `css` | A standard stylesheet; class names stay as written |
| `global-css` | Class names are global by default |
| `local-css` | Class names are local to the file and are rewritten |

`.css` files use the `css` loader by default, and `.module.css` files use
`local-css` automatically:

```js
build: {
  loader: {
    '.css': 'css',
    '.module.css': 'local-css',
    '.global.css': 'global-css',
  },
}
```

The last two entries are explicit; only `.module.css` is special-cased by
default. Without the `'.global.css'` entry, a file named `theme.global.css`
would still be a standard stylesheet.

### `css` - a standard stylesheet

```css
/* style.css */
.title {
  color: #111;
}
```

```js
import './style.css';
```

The rule is emitted as written. There is no generated class-name mapping, which
is why this is the ordinary choice for global site styles.

### `local-css` - a CSS module

```css
/* app.module.css */
.button {
  color: white;
  background: #2563eb;
}
```

```js
import styles from './app.module.css';

const button = document.createElement('button');
button.className = styles.button;   // the rewritten class name
```

`local-css` gives every class, id and `@keyframes` name a file-scoped identity.
Without `minifyIdentifiers` the output name is readable and derived from the
file and the original name, for example `app_module_button`; with identifier
minification it becomes a short name. Because the identity includes the file, a
local `.button` does not collide with a global `.button` or with another
module's `.button`.

`composes` combines local names:

```css
/* base.module.css */
.base {
  padding: 0.5rem 1rem;
}

/* button.module.css */
.primary {
  composes: base from './base.module.css';
  background: #2563eb;
}
```

```js
import styles from './button.module.css';

styles.primary;   // space-separated rewritten names, e.g. "base_primary button_primary"
```

The composed class list is expanded into the exported string, so applying one
class applies both rules.

### `global-css` - global by default

```css
/* theme.global.css */
.theme-dark {
  background: #111;
  color: #eee;
}
```

With `global-css`, names are global unless you opt into a local one. That is the
mirror image of `local-css`, which is local unless you opt out. Use it for class
names that must match strings written outside the build - a theme class on
`<html>`, a class produced by a CMS, or a name a third-party script expects.

### Opting in and out per selector

In a local stylesheet, `:global(...)` marks a name as global:

```css
/* app.module.css */
:global(.theme-dark) .button {
  background: #333;
}
```

In a global stylesheet, `:local(...)` marks a name as local:

```css
/* theme.global.css */
:local(.button) {
  color: red;
}
```

Bare `:local` and `:global` are also accepted as annotations on a selector.

## Imports and URLs

`@import` rules are resolved and inlined like module imports:

```css
/* style.css */
@import './reset.css';
@import './theme.css';

body {
  margin: 0;
}
```

When bundling, the imported rules are placed in the output in import order, and
the `@import` statement itself is removed. When not bundling, the `@import`
stays and the file is only converted.

`composes: ... from ...` in a CSS module is resolved as a module reference, as
shown above. It is only meaningful in `local-css` or `global-css` mode; in a
plain `css` file the declaration is left alone (and the parser warns).

`url(...)` references are resolved and, when bundling, rewritten to point at the
emitted asset:

```css
/* style.css */
@font-face {
  font-family: 'Inter';
  src: url('./Inter.woff2') format('woff2');
}

.hero {
  background-image: url('./hero.jpg');
}
```

```js
build: {
  loader: {
    '.woff2': 'copy',
    '.jpg': 'copy',
  },
  assetNames: 'assets/[name]-[hash]',
}
```

`copy` emits the font and image unchanged and rewrites each `url(...)` to the
hashed output path. `dataurl` would inline a small image instead; `file` would
emit it and expose its URL. See [Assets & loaders](assets-and-loaders.md).

### Query strings and fragments

A URL may carry a query or fragment, which the resolver keeps as an ignored
suffix:

```css
.hero {
  background-image: url('./sprite.svg#icon-home');
}
```

The file is resolved from the path before `#` or `?`; the suffix stays attached
to the rewritten URL.

## Minification

`--minify-whitespace`, `--minify-syntax` and the `minify` shorthand apply to CSS
as well as JavaScript:

```bash
guchho build src/app.module.css --minify
guchho build src/app.module.css --minify-whitespace   # only one
```

`minifyIdentifiers` has an extra effect on `local-css`: local names become short
minified names instead of readable file-derived ones. See
[Minification](minification.md).

## Source maps

CSS output can carry source maps like JavaScript:

```js
build: {
  sourcemap: 'linked',
  sourcesContent: false,
}
```

The map points each generated rule back at the stylesheet it came from,
including rules pulled in through `@import`.

## Compiler stages

CSS can be parsed and printed through the
[JavaScript API](../api/compiler.md):

```js
import { parse, print } from 'guchho';

const source = '.used { color: red } .dead { color: blue }';

const css = await parse(source, { language: 'css' });
const out = await print(css.ast, {
  language: 'css',
  minifyWhitespace: true,
});

console.log(out.code);
```

The stages take the source text itself, not a path. `parse` returns a numeric
handle as `ast`, and `print` takes that handle back. The same names are exposed
by `@guchho/wasm` when CSS parsing must run in the browser; see
[WebAssembly](../wasm.md#top-level-functions).

## Ordering

Stylesheets are emitted in the order the graph reaches them, so a stylesheet
imported after another comes later and can override it:

```css
/* style.css */
@import './reset.css';   /* first */
@import './page.css';    /* second */

.page { margin: 1rem; }  /* last */
```

A JavaScript import of a CSS file participates in the same order as the
JavaScript module graph. If order matters, make the dependency explicit with an
`@import` or an import order in the entry module rather than relying on file
names.

## Common pitfalls

- **Expecting `css` to behave like a CSS module.** Class names are not rewritten
  and there is no `styles.button` mapping. Use `local-css` for that.
- **Expecting `local-css` names to stay readable in a minified build.**
  `minifyIdentifiers` shortens them; that is the point.
- **Using `composes` in a plain `.css` file.** It is only processed in
  `local-css` / `global-css` mode.
- **Forgetting asset loaders.** A `url(...)` to a `.woff2` needs `copy` (or
  another binary loader), or the build reports a missing loader.
- **Relying on file order.** Import order is graph order; name files to make the
  intended order explicit.
- **Mixing `global-css` and `local-css` in one file.** Pick one mode; use
  `:local` / `:global` annotations for the exceptions.

## See also

- [Assets & loaders](assets-and-loaders.md) - `copy`, `file`, `dataurl` and
  friends.
- [Minification](minification.md) - the three switches and identifier mangling.
- [HTML pipeline](html-pipeline.md) - `<link rel="stylesheet">` and URL
  rewriting.
- [Deployment](deployment.md) - hashed asset names and `publicPath`.
