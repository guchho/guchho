---
title: HTML pipeline
description: How Guchho treats an HTML document as a build entry point.
---

# HTML pipeline

An HTML file is a first-class entry point. Guchho parses the document, finds
every reference to code, styles and assets, adds them to the graph, and re-emits
the document with rewritten URLs.

This is the part of Guchho that makes a plain static site buildable without a
separate manifest. The document already says where its files are. Guchho follows
those references instead of asking you to list them twice.

## A complete example

```text
site/
├── index.html
├── about.html
├── src/
│   ├── main.js
│   ├── theme.css
│   └── analytics.js
└── public/
    ├── logo.svg
    └── hero.jpg
```

```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Example</title>
    <link rel="stylesheet" href="./src/theme.css">
  </head>
  <body>
    <img src="./public/logo.svg" alt="Logo">
    <img
      src="./public/hero.jpg"
      srcset="./public/hero.jpg 1x, ./public/hero@2x.jpg 2x"
      alt="Hero"
    >
    <script type="module" src="./src/main.js"></script>
  </body>
</html>
```

```bash
guchho build site/index.html --outdir=dist
```

The entry is the document, so the output contains the document plus everything
it referred to:

```text
dist/
├── assets/
│   ├── main-4G7K2P.js
│   ├── theme-8Q1M3C.css
│   ├── logo-2C9B1R.svg
│   ├── hero-6L4T8N.jpg
│   └── hero@2x-1P7W5H.jpg
└── index.html
```

The exact names depend on the naming templates. The important part is that the
document no longer points at `./src/theme.css` or `./public/logo.svg`; it points
at the files that were emitted.

If `about.html` is also an entry, it is a separate document with its own
references. Files shared by both entries can become shared chunks when splitting
is enabled.

### The same build with a config file

```js
// guchho.config.js
export default {
  build: {
    entry: 'site/index.html',
    outdir: 'dist',
    assetNames: 'assets/[name]-[hash]',
    minifyHtml: true,
  },
};
```

```bash
guchho build
```

`assetNames` controls the copied assets, and `minifyHtml` controls the markup.
Neither is required for the build to work; they are shown because the output
tree above reflects those settings.

## What counts as a reference

Guchho follows references in several places. The table below is the practical
list; if an element is not here, assume its URL is ordinary markup and is left
alone.

| Where | What is followed | What it becomes |
| --- | --- | --- |
| `<script src="...">` | the script source | a JavaScript entry or module in the graph |
| `<script>` with no `src` | the inline body | processed as its own script and folded back into the document |
| `<link rel="stylesheet" href="...">` | the stylesheet | a CSS module in the graph |
| `<link href="...">` for other `rel` values | the linked file | an asset reference; see the note below |
| `<style>` | the inline stylesheet | processed as CSS and folded back into the document |
| `<img src>`, `<source src>` | the image or media file | an asset |
| `<img srcset>`, `<source srcset>` | each candidate URL | one asset per candidate, with descriptors preserved |
| `<link imagesrcset>` | each candidate URL | one asset per candidate |
| `<video src>` or `<video poster>` | the video or poster | an asset |
| `<audio src>`, `<track src>` | the media file | an asset |
| `<iframe src>`, `<embed src>` | the embedded document or object | an asset |
| `<object data>` | the object | an asset |
| `<input type="image" src>` | the image button | an asset |
| SVG `<use href>` / `<use xlink:href>` | the referenced SVG | an asset |
| SVG `<image href>` | the image | an asset |
| `style="... url(...) ..."` | each `url()` | an asset within the attribute, rewritten in place |
| CSS loaded by the graph | `@import` and `url(...)` | more CSS or assets; see [CSS pipeline](css-pipeline.md) |
| JavaScript loaded by the graph | `import`, `require`, dynamic import | more modules; see [Bundling & splitting](bundling-and-splitting.md) |

A few details are easy to miss:

- `rel` is matched by token, not by exact string. `rel="alternate stylesheet"`
  counts as a stylesheet.
- `type="module"` is exact and case-sensitive. A missing `type`, or
  `type="text/javascript"`, is a classic script.
- `srcset` parsing preserves the width or density descriptor and the
  whitespace between candidates; only the URL slice is rewritten.
- A `style` attribute can contain several `url(...)` references. Each one is
  resolved and rewritten on its own.
- A reference inside an inline `<script>` is resolved relative to the document,
  just like a reference in an external module.

### Non-stylesheet `<link>` elements

Every `<link href>` is a reference, but not every link is a stylesheet. A
favicon, a web manifest, a `preload` hint or a `modulepreload` hint is followed
as an asset reference.

There is one deliberate limit: a non-stylesheet `<link>` has its `href`
rewritten only when the target is emitted through the asset pipeline. A link to
another HTML page, to a script, or to anything else that is not a copied asset
keeps its original `href`. This prevents a hint from silently pointing at a
generated module that the browser was never meant to fetch by that URL.

For the stylesheet the browser should apply, use `rel="stylesheet"`. A
`rel="preload" as="style"` hint is a performance hint, not the document's
stylesheet declaration.

## What is left alone

These are not treated as local files to build:

- absolute URLs such as `https://cdn.example.com/app.js`;
- protocol-relative URLs such as `//cdn.example.com/app.js`;
- `data:`, `blob:`, `mailto:` and other non-file schemes;
- bare fragments such as `#main`;
- an empty or blank `src`/`href`, which produces a warning instead of a
  request for the current directory.

A root-relative path such as `/assets/logo.svg` is treated as a local path, not
as a URL for another origin. It is resolved by the same resolver as the rest of
the build.

Ordinary document links are not build references. `<a href="about.html">` is
navigation, so Guchho does not add `about.html` to the graph just because a page
links to it. If `about.html` is another page of the site, make it an entry point
or let another entry reach it explicitly.

## Opting an element out

Add `guchho-ignore` to an element to exclude it from collection and rewriting:

```html
<img src="/legacy/logo.png" guchho-ignore alt="Legacy logo">
```

The original `src` stays in the output. That is useful when the file is supplied
by the host at runtime and must not be copied or renamed. It also means Guchho
will not check that the file exists, so use it deliberately.

## Inline code and styles

An inline `<script>` without `src` is not copied as text. Guchho collects the
body, processes it, and folds the result back into the document. An inline
`<style>` works the same way for CSS:

```html
<style>
  .hero {
    background-image: url('./public/hero.jpg');
  }
</style>
```

The `url(...)` is resolved, the image is emitted, and the stylesheet in the
document points at the emitted file. This happens without a separate CSS entry
in the config.

Inline script and style bodies are also where a source map or minifier can
change the bytes inside the document. The surrounding HTML stays parsed markup,
so the result is still a document rather than a string replacement.

## Import maps

A `<script type="importmap">` is not a resource, so its JSON is not bundled as
JavaScript. Guchho does read its position in the document: if a map appears
after the first module script or `modulepreload` link, it is moved before that
pivot.

The move matters because browsers begin resolving module specifiers as soon as
they see the first module marker. A map that arrives later can be ignored. The
contents and the relative order of multiple maps are preserved; only their
position changes.

## Markup output

HTML is re-emitted from the parsed tree, so the printer options describe the
output rather than the file:

- `pretty` (default **on**) - indent the tree so the result is readable and
  editable.
- `minifyHtml` (default **off**) - collapse the markup.

`minify` is about JavaScript, CSS and assets; it does **not** turn on
`minifyHtml`. Ask for the markup pass separately:

```js
// guchho.config.js
export default {
  build: {
    entry: 'index.html',
    outdir: 'dist',
    minify: true,
    minifyHtml: true,
  },
};
```

`pretty` and `minifyHtml` can both be on. Pretty printing decides the layout of
the tree; HTML minification decides how much whitespace and punctuation around
that layout is kept. In practice, choose one:

```js
// Readable markup, minified code and assets.
build: { pretty: true, minifyHtml: false, minify: true }

// Compact markup and compact code.
build: { pretty: false, minifyHtml: true, minify: true }
```

With `pretty: false` and `minifyHtml: false`, the output is still valid HTML but
uses the printer's compact single-line layout. That is different from
minification: it does not strip comments or unnecessary attribute syntax.

### What is preserved

HTML minification is conservative about content:

- `<pre>`, `<textarea>` and raw-text elements such as `<script>` and `<style>`
  keep their contents;
- conditional comments are preserved;
- ordinary non-conditional comments can be dropped;
- markup inside `<template>` is parsed as template content, not as ordinary
  page text.

Whitespace between block elements is layout, not content, so both pretty
printing and minification may change it. Whitespace inside text that the browser
renders as text is handled by the same rules as the source document.

## Hashed names and URL rewriting

Output file names can carry a content hash via the naming templates
(`entryNames`, `chunkNames`, `assetNames`, e.g. `assets/[name]-[hash]`).
Every URL that points at a renamed file is rewritten to match, so the document
keeps working without manual edits.

The order is important:

1. the referenced file is resolved;
2. it is loaded and processed by its loader;
3. the output path and hash are decided;
4. the document's attribute is updated to that path.

This is why an HTML entry works even when the source document uses relative
paths and the output uses a different directory layout. The rewrite is based on
the resolved target, not on string replacement in the HTML source.

Content hashing is also what makes long-lived browser caches safe: an unchanged
file keeps its name, and a changed file gets a new URL. See
[Deployment](deployment.md) for the cache policy that goes with it.

## A failed reference

If a reference cannot be resolved, the build reports it as a diagnostic at the
element that contains it. That is more useful than emitting a document with a
broken link and calling the build successful.

Common causes are:

- a relative path written from the wrong directory;
- a package name that is not installed;
- a missing file that is supplied by the host at runtime;
- a URL that looks local but is meant for a CDN.

The last two are cases for `guchho-ignore` or an absolute URL. The first two are
ordinary build errors; fix the path or install the package.

## Common mistakes

- **Using `rel="preload"` as the only stylesheet link.** Add the real
  `rel="stylesheet"` link. A preload hint alone does not declare the document's
  stylesheet.
- **Expecting `minify` to minify the HTML.** Set `minifyHtml` as well, or use
  `--minify-html`.
- **Forgetting that an HTML entry enables bundling by default.** That is usually
  what you want for a site. If you only wanted a transform, use the JavaScript
  or CSS file as the entry instead.
- **Referencing an asset only in a JavaScript string.** Guchho follows imports
  and `url(...)`, not arbitrary strings that happen to contain a path. Import
  the asset, or put it in the HTML.
- **Using `guchho-ignore` and expecting the file to be copied.** Ignoring an
  element also removes its asset from the build. The original URL must still
  resolve at runtime.
- **Assuming an `<a href>` adds a page to the build.** Navigation links are not
  graph edges. Name additional pages as entries.
- **Changing `outdir` without changing the server directory.** `guchho serve`
  uses the configured output directory; a positional directory overrides it.
  `guchho dev` follows the build, so it needs no separate directory argument.

## See also

- [Assets & loaders](assets-and-loaders.md)
- [CSS pipeline](css-pipeline.md)
- [JavaScript & TypeScript](javascript-and-typescript.md)
- [Configuration](configuration.md)
- [Deployment](deployment.md)
