---
title: Guchho Documentation
description: Fast, HTML-first bundler, compiler and build tool for modern web applications.
---

# Guchho Documentation

Guchho is a fast, native C++20 build system and bundler for modern web
applications. It parses HTML, JavaScript, TypeScript, JSX/TSX, CSS and assets,
and produces a complete, deployable output directory.

These docs are plain Markdown. Every directory has a `README.md` that acts as
its index, so the whole tree is readable directly on GitHub.

## Start here

- [Introduction](guide/introduction.md) - what Guchho is and how it thinks.
- [Getting started](guide/getting-started.md) - install and run a first build.
- [Core concepts](guide/core-concepts.md) - entries, the graph, loaders, bundles.

## Contents

### Guide - learning-oriented

| Page | What it covers |
| --- | --- |
| [Introduction](guide/introduction.md) | The HTML-first model |
| [Getting started](guide/getting-started.md) | Install, scaffold, build |
| [Why Guchho](guide/why-guchho.md) | Native engine, one tool |
| [Core concepts](guide/core-concepts.md) | Entries, graph, loaders |
| [Configuration](guide/configuration.md) | `guchho.config.js` and friends |
| [CLI overview](guide/cli-overview.md) | The eleven commands at a glance |
| [HTML pipeline](guide/html-pipeline.md) | Document entries, URL rewriting |
| [JavaScript & TypeScript](guide/javascript-and-typescript.md) | Parsing, targets, JSX |
| [CSS pipeline](guide/css-pipeline.md) | CSS, scoping, imports |
| [Assets & loaders](guide/assets-and-loaders.md) | Every loader |
| [Bundling & splitting](guide/bundling-and-splitting.md) | `bundle`, `splitting`, `external` |
| [Minification](guide/minification.md) | Whitespace, identifiers, syntax |
| [Tree shaking](guide/tree-shaking.md) | Dead code removal |
| [Source maps](guide/source-maps.md) | Inline, linked, external |
| [Legal comments](guide/legal-comments.md) | Banners and license headers |
| [Targets & browsers](guide/targets-and-browsers.md) | `target`, `engines`, `supported` |
| [Dev server & watch](guide/dev-server-and-watch.md) | `dev`, `serve`, `watch` |
| [Packaging](guide/packaging.md) | `guchho pack` |
| [Deployment](guide/deployment.md) | Shipping the output |

### Reference - look-up material

- [CLI reference](reference/cli/README.md)
- [Configuration reference](reference/config/README.md)
- [Loaders](reference/loaders.md), [Formats](reference/formats.md), [Platforms](reference/platforms.md)
- [Metafile](reference/metafile.md), [Errors & warnings](reference/errors-and-warnings.md), [Exit codes](reference/exit-codes.md)

### API - programmatic surface

- [JavaScript / TypeScript API](api/README.md)
- [Generated option types](api/generated/README.md)

### More

- [Writing plugins](plugins/README.md)
- [Embedding (C++)](embedding/README.md)
- [WebAssembly](wasm.md)
- [Recipes](recipes/README.md)
- [Resources](resources/README.md)
- [Blog](blog/README.md), [Release notes](release-notes/README.md)
- [Archived versions](versions/README.md)

---

Guchho is licensed under **MIT**. Copyright © [CodeHemu](https://github.com/code-hemu).
