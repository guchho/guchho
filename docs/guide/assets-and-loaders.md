---
title: Assets & loaders
description: How a loader turns a file's bytes into a module.
---

# Assets & loaders

A **loader** decides how a file's bytes become a module. Every file that enters
the graph passes through exactly one loader: it is what tells the engine whether
`.ts` should be type-erased, whether `.png` should become a URL or a byte array,
or whether `.txt` should be an inert string. Choosing the right loader per
extension is the single biggest lever on how assets behave in the output.

You choose one per file extension with the loader map:

```js
build: {
  loader: { '.svg': 'dataurl', '.png': 'file', '.txt': 'text' },
}
```

The same map is spelled with repeated `--loader` flags on the command line:

```bash
guchho build src/index.html --loader:.svg=dataurl --loader:.png=file --loader:.txt=text
```

A `build` takes a loader **map** keyed by extension; a `transform` takes a
single loader name (it sees one input, so there is no extension to key on).

## What a loader controls

A loader affects three separate things, and it is worth keeping them apart:

1. **How the bytes are parsed.** `js`, `jsx`, `ts`, `tsx`, `json`, `css` and
   `html` are parsed languages. A syntax error in one of these becomes a build
   diagnostic with a file, a line and a column.
2. **What the module exports.** Loaders such as `text`, `file`, `base64`,
   `dataurl` and `binary` do not parse anything; they turn the bytes (or the
   emitted file's URL) into a module value.
3. **Whether the original file reaches the output.** `copy` and `file` emit the
   source file into `outdir`; `text`, `binary`, `base64` and `dataurl` keep the
   content inside the JavaScript instead; `empty` emits nothing.

Understanding that split is usually enough to pick a loader without thinking
about the internals.

## The loaders

| Loader | Result | Emits source file? |
| --- | --- | --- |
| `js` | JavaScript | - |
| `jsx` | JavaScript with markup | - |
| `ts` | TypeScript (types erased) | - |
| `tsx` | TypeScript with markup | - |
| `json` | Parsed object whose keys may be named imports | - |
| `text` | A plain string | no |
| `css` | A stylesheet that participates in CSS bundling | - |
| `global-css` | A stylesheet with global rules | - |
| `local-css` | A stylesheet scoped to the importing module | - |
| `html` | An HTML document | - |
| `base64` | A `data:…;base64` URL string | no |
| `dataurl` | A `data:` URL | no |
| `file` | An emitted file whose URL is the module's default export | yes |
| `binary` | A typed-array view over the raw bytes | no |
| `copy` | Pass the file through untouched, expose its output URL | yes |
| `empty` | An empty module, for stubbing a file out | no |

`none` and `default` mean "not chosen yet": the engine picks one from the file
extension, nearest package metadata, and any per-path overrides.

## Choosing a loader

- **`copy`** - the file is emitted verbatim and referenced by URL. Use it for
  anything the browser loads directly (fonts, large images, downloadable
  files). The content is not touched, so it is the cheapest option for big
  binaries.
- **`file`** - the file is emitted and the module's default export is its URL.
  Use it when your code needs a URL and you want the emitted asset tracked in
  the output.
- **`binary`** - the bytes arrive as an array you can pass to an API (a WASM
  decoder, a parser, a `Blob` constructor). Nothing is emitted as a file.
- **`text`** - the contents are an inert string. Ideal for templates, READMEs,
  shaders or SQL.
- **`empty`** - a stub, useful for swapping a module out (for example, a Node
  built-in you want to neutralize in a browser build).

### Which loader should I use?

| You want… | Use |
| --- | --- |
| An image the page loads by URL | `file` or `copy` |
| A font referenced from CSS | `copy` |
| A small SVG inlined into JS | `dataurl` |
| Raw bytes in JavaScript | `binary` |
| The bytes as a base64 string | `base64` |
| File contents as a string | `text` |
| To include a JSON file | `json` |
| To skip a file entirely | `empty` |

## Examples

### Images

```js
// src/logo.js
import logoUrl from "./logo.png";

const img = document.createElement("img");
img.src = logoUrl;
document.body.appendChild(img);
```

```js
build: { loader: { ".png": "file" } }
```

With `file`, `logo.png` is emitted into `outdir` (under `assets/` by default)
and `logoUrl` is a string URL pointing at the emitted copy. Small images can
instead be inlined as a data URL with `dataurl`, which avoids a network request
at the cost of a larger JS bundle.

### Fonts

```css
/* src/fonts.css */
@font-face {
  font-family: "Inter";
  src: url("./Inter.woff2") format("woff2");
}
```

```js
build: {
  loader: { ".woff2": "copy" },
  assetNames: "assets/[name]-[hash]",
}
```

`copy` emits `Inter.woff2` untouched and rewrites the CSS `url(...)` to the
hashed output path, so the browser fetches the real font file.

### Raw bytes

```js
// src/decode.js
import wasmBytes from "./decoder.wasm";

const module = await WebAssembly.compile(wasmBytes);
```

```js
build: { loader: { ".wasm": "binary" } }
```

`binary` hands you a `Uint8Array` over the file's bytes. This is the loader the
`WebAssembly.compile` path expects; it does not emit a separate `.wasm` file.

### Text

```js
// src/template.js
import template from "./page.html.txt";

document.body.innerHTML = template;
```

```js
build: { loader: { ".txt": "text" } }
```

The file's contents become a string default export. No URL, no file emitted.

### Base64 vs data URL

```js
import b64 from "./icon.png.base64";     // "iVBORw0KGgo…"
import url from "./icon.png.dataurl";    // "data:image/png;base64,…"
```

- `base64` gives you just the encoded payload.
- `dataurl` gives you the full `data:` URL, ready to drop into `src` or CSS.

### JSON

```js
import pkg from "./package.json";
import { version } from "./package.json";  // named import of a top-level key

console.log(pkg.version, version);
```

The `json` loader's default export is the parsed value; top-level keys may also
be imported by name.

### Stubbing a module out

```js
build: {
  loader: { ".wasm": "empty" },
}
```

An `empty` module exports nothing. This is the least invasive way to drop a
module without touching the source.

## Loaders interact with naming

Loaders that emit the source file as an asset (`file`, `copy`) use the
`assetNames` pattern, while loader map keys only concern extensions. Naming is
documented under [Output names](bundling-and-splitting.md#output-names); the
short version is that `[name]`, `[hash]`, `[dir]` and `[ext]` are substituted
into the pattern you supply.

```js
build: {
  loader: { ".png": "file", ".woff2": "copy" },
  assetNames: "assets/[name]-[hash]",
}
```

## Loaders vs `transform`

A **build** walks the graph and applies the loader map to every file it finds.
A **transform** works on a single input from stdin and takes a single loader
name, so there is no map:

```bash
echo 'const x: number = 1' | guchho transform --loader=ts
```

Same loaders, different shape of input: the build has extensions to key on, a
transform has one input and must be told.

## Common pitfalls

- **Forgetting `file`/`copy` for a binary.** Without a loader, an unknown
  extension is an error; with the wrong loader it may parse as text and corrupt
  the bytes.
- **Inlining large assets.** `dataurl` and `base64` embed content in JS. That is
  great for a 1 KB icon and terrible for a 2 MB photo. Use `file` or `copy` for
  anything large.
- **Expecting `binary` to emit a file.** It does not; the bytes live in the JS.
  Use `copy` if the browser must fetch the asset by URL.
- **Expecting `file` to hand you bytes.** Its default export is a URL string.
  Use `binary` when you need the raw bytes.
- **Mixing `global-css` and `local-css` in one file.** Pick one; `css` is the
  graph-aware default that participates in CSS bundling.

## See also

- [Loaders reference](../reference/loaders.md) - the canonical table of names.
- [Configuration: build](../reference/config/build.md) - `loader`, `assetNames`
  and every other build option.
- [Bundling & splitting](bundling-and-splitting.md) - where assets land in the
  output and how they are named.
