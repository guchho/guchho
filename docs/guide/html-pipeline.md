---
title: HTML pipeline
description: How Guchho treats an HTML document as a build entry point.
---

# HTML pipeline

An HTML file is a first-class entry point. Guchho parses the document, finds
every reference to code, styles and assets, adds them to the graph, and re-emits
the document with rewritten URLs.

## What counts as a reference

Guchho follows, among others:

- `<script src="...">` and inline `<script type="module">`
- `<link rel="stylesheet" href="...">`
- `<img src="...">`, `<source src="...">`, `srcset`
- asset references discovered inside JavaScript and CSS

## Markup output

HTML is re-emitted from the parsed tree, so the printer options describe the
output rather than the file:

- `pretty` (default **on**) - indent the tree so the result is readable and
  editable.
- `minifyHtml` (default **off**) - collapse the markup.

Neither touches content where whitespace is significant, so turning either on
is always safe.

## Hashed names and URL rewriting

Output file names can carry a content hash via the naming templates
(`entryNames`, `chunkNames`, `assetNames`, e.g. `assets/[name]-[hash][ext]`).
Every URL that points at a renamed file is rewritten to match, so the document
keeps working without manual edits.

## Related

- [Assets & loaders](assets-and-loaders.md)
- [Configuration](configuration.md)
- [Deployment](deployment.md)
