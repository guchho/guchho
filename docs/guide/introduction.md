---
title: Introduction
description: What Guchho is, and the HTML-first model it is built around.
---

# Introduction

Guchho is a **fast, native build tool and bundler for modern web
applications**. The engine is a single C++20 binary: parsing, linking,
minification and tree shaking all happen in native code, not in a JavaScript
runtime.

That sentence describes the implementation. The useful part for a project is
the model it enables: you name an entry point, Guchho follows what that entry
refers to, and it writes a directory a browser can load.

## A document, not a module

Most bundlers ask you to name a JavaScript entry and then describe the HTML
somewhere else. Guchho can start with the HTML itself:

```text
my-site/
├── index.html
├── src/
│   ├── main.js
│   └── style.css
└── public/
    └── logo.svg
```

```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8">
    <title>My site</title>
    <link rel="stylesheet" href="./src/style.css">
  </head>
  <body>
    <h1>Hello</h1>
    <img src="./public/logo.svg" alt="Logo">
    <script type="module" src="./src/main.js"></script>
  </body>
</html>
```

```bash
guchho build index.html --outdir=dist
```

The HTML is not copied verbatim. Guchho reads the document, finds three
references, and builds the graph behind them:

- the stylesheet enters the CSS pipeline;
- the image enters the asset pipeline, which copies it by default;
- the module script enters the JavaScript graph.

It then writes the document with the right URLs for the files it emitted. Those
URLs point at the emitted names, which carry a content hash by default:

```text
dist/
├── assets/
│   ├── main-7F3K2Q.js
│   ├── style-2M8X1P.css
│   └── logo-9Q4V0B.svg
└── index.html
```

The document is the build description. There is no second manifest listing the
same files, and no rule that says an image in `public/` must be copied while an
image imported from JavaScript must be processed differently. The loader map
decides how each file is handled, and every reference is rewritten to the
result.

## One binary, not a toolchain

The lexer, parser, transformer, printer, minifier, linker, resolver, watcher and
dev server are all the same program. There is no plugin worker pool, no Node
process per file, and no separate CSS toolchain to wire together. Installing
Guchho installs the whole pipeline.

That does not mean a project has no configuration. It means the configuration
describes the build rather than the plumbing: an entry point, a target, a
loader, an output directory. The machinery that reads the configuration, walks
the graph and writes the files is already in the binary.

## Bundling is a choice

A build can do one of three things:

| Input | What Guchho does |
| --- | --- |
| one JavaScript or TypeScript file | parses, transforms and prints that file. Imports stay imports unless bundling is requested. |
| one CSS file | parses the stylesheet, processes `@import` and `url(...)`, and prints the result. |
| an HTML document | follows the document's references and builds the application behind it. |

This distinction is deliberate. A tool that always bundles makes a single-file
transform awkward. A tool that never bundles makes an application require a
second tool. Guchho uses the same engine for both, and an HTML entry point turns
bundling on by default because a document is a description of a whole
application rather than one module.

For a library, bundling is often the wrong choice: the consumer's bundler
should see the modules. For an application served straight to a browser,
bundling is usually the right one. The options are in
[Core concepts](core-concepts.md) and [Bundling & splitting](bundling-and-splitting.md).

## What one build does

The order is stable, and knowing it explains most diagnostics:

1. **Resolve options.** The command line wins over a config file, and the config
   file wins over built-in defaults.
2. **Discover the entry points.** A path, several paths, or the entries named
   by an HTML document.
3. **Resolve references.** Relative paths, package names, aliases and
   conditions are turned into files. This is also where `external` decides to
   leave a package alone.
4. **Load and transform.** Each file is handed to the loader for its extension:
   JavaScript, TypeScript, JSX, CSS, JSON, text, or a copy loader for assets.
5. **Link and tree-shake.** Modules are joined, unused exports are dropped when
   tree shaking is on, and shared code can be split into chunks.
6. **Print and name outputs.** The printer emits JavaScript and CSS, minifiers
   run if requested, and naming templates decide the paths and content hashes.
7. **Rewrite references and write.** HTML and CSS URLs are updated to the
   emitted names, and the output directory is written.

A failure in one stage becomes a diagnostic with a file and location rather than
an exception escaping the process. That is what makes the CLI, the JavaScript
API and an editor integration report the same problem the same way.

## Two ways to drive it

- **The CLI** - `guchho build`, `dev`, `serve`, `watch`, `init`, `clean`,
  `info`, `transform`, `lint`, `pack`, `unpack`.
- **The JavaScript API** - the same engine, callable from Node with full
  TypeScript declarations. See the [API reference](../api/README.md).

Both go through the same option resolution, so a project behaves the same way
whichever one runs it. The API is not a reimplementation: it starts the native
engine as a service and asks it to build, transform or serve.

A small API build looks like this:

```js
import { build } from 'guchho';

const result = await build({
  entrypoints: ['index.html'],
  outdir: 'dist',
  minify: true,
  metafile: true,
});

console.log(`${result.outputs.length} files`);
```

The result is data: output paths, sizes, warnings and, when requested, a
metafile. A release script can inspect that data without parsing terminal
output.

There is also a C++ API for programs that link the engine directly. That surface
is documented separately in [Embedding Guchho in C++](../embedding/README.md).

## What Guchho does not decide for you

Guchho is not a framework. It does not choose a UI library, a router or a state
container. It does not install packages. It does not require a particular
project layout beyond an entry point that resolves to a real file.

What it does provide is the path from source files to deployable files: parse,
resolve, transform, link, minify, hash and rewrite. Those steps are useful to a
single-page app, a static site, a component library and a command-line tool
alike, and the same options describe all of them.

## Where to go next

- [Getting started](getting-started.md) - install Guchho and produce a first
  build.
- [Why Guchho](why-guchho.md) - the design choices in more detail.
- [Core concepts](core-concepts.md) - the vocabulary used throughout these
  docs.
- [HTML pipeline](html-pipeline.md) - what happens when the entry is a document.
- [Configuration](configuration.md) - describe a build in a file.
