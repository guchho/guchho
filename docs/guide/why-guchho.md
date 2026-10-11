---
title: Why Guchho
description: The design choices behind Guchho and how they compare to other tools.
---

# Why Guchho

## Native, not transpiled

The engine is a single C++20 binary. Parsing, linking, minification and tree
shaking happen in native code, so a build does not spend its time in a garbage
collector or walking `node_modules`.

## HTML-first

Guchho starts at your HTML entry, follows every `<script>`, `<link>`, `import`
and asset reference, and produces a complete output directory. You describe the
document; Guchho works out the graph.

## One tool, not five

Lexer, parser, transformer, printer, minifier, linker, resolver, watcher and dev
server are all in the same binary. There is no plugin process, no worker pool,
and no separate CSS toolchain to configure.

## A real API

The CLI is a thin shim over a long-lived service protocol. Everything
`guchho build` does is also callable from JavaScript — including the compiler
stages (`lexer`, `parse`, `print`) — so the engine embeds in your own tools.

## Failure is data

Anything that can go wrong while building arrives as a diagnostic in the result,
not an exception. Messages can be filtered, reworded or forwarded to an editor
without a `try`/`catch`. Exceptions are reserved for programming mistakes.

## Where Guchho fits

- **A library or app** that wants one binary to parse, bundle, minify and serve.
- **A tool author** who wants the parser, printer and minifier as callable
  stages.
- **An editor integration** that needs to feed an unsaved buffer into the graph.


