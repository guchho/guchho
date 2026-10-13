---
title: Introduction
description: What Guchho is, and the HTML-first model it is built around.
---

# Introduction

Guchho is a **fast, native build tool and bundler for modern web
applications**. The engine is a single C++20 binary: parsing, linking,
minification and tree shaking all happen in native code, not in a JavaScript
runtime.

## One binary, not a toolchain

The lexer, parser, transformer, printer, minifier, linker, resolver, watcher and
dev server are all the same program. There is no plugin worker pool, no Node
process per file, and no separate CSS toolchain to wire together. Installing
Guchho installs the whole pipeline.

## HTML-first

A web application is a document, not a module. Guchho can start at an HTML
entry point, follow every `<script>`, `<link>`, `import` and asset reference it
finds, and emit a complete output directory - hashed file names, rewritten URLs,
minified, ready to upload.

Bundling is a choice, not a requirement. Pointing Guchho at a single `.js` or
`.css` file converts that file; pointing it at an HTML document builds the
application behind it.

## Two ways to drive it

- **The CLI** - `guchho build`, `dev`, `serve`, `watch`, `init`, `clean`,
  `info`, `transform`, `pack`.
- **The JavaScript API** - the same engine, callable from Node with full
  TypeScript declarations. See the [API reference](../api/README.md).

Both go through the same option resolution, so a project behaves the same way
whichever one runs it.

## Next

- [Getting started](getting-started.md) - build something.
- [Core concepts](core-concepts.md) - the vocabulary used throughout these docs.
