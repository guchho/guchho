---
title: Introducing Guchho
description: A native, HTML-first bundler, and the choices behind it.
date: 2026-10-14
---

# Introducing Guchho

Guchho is a web build tool written as a single native binary. You point it at a
project; it reads HTML, JavaScript, TypeScript, CSS and assets, works out how
they depend on each other, and writes a directory a browser can load. That
description fits a lot of tools, so this post is about the parts where the
choices are visible.

## One binary, no process boundary

Most of a JavaScript build spends its time in JavaScript. Guchho's engine is
C++20: the lexer, parser, transformer, printer, minifier, linker, resolver,
watcher and dev server are all in the same executable. A build does not pay for
a garbage collector or for walking `node_modules` in an interpreter, and there
is no plugin host process to start, talk to and keep alive.

The cost is real and worth naming: a native binary has to be built and shipped
per platform, which is why `npm install -g guchho` downloads a prebuilt binary
rather than compiling from source. The reward is a build whose work happens
where the data is.

## The document is the entry point

A lot of bundlers start from a JavaScript module and treat HTML as one more
input. Guchho starts the other way round. Give it `index.html` and it follows
every `<script>`, `<link>`, `import` and asset reference the document makes,
then produces the whole output directory. You describe the page; the tool works
out the graph.

This is also why bundling can turn itself on. An HTML entry cannot be served as
one copied file - it names its scripts and styles - so when a document is the
entry and you did not say either way, Guchho enables bundling. A request that
could only have meant one thing is not refused; it is answered.

## Failure is data

When something goes wrong in a build - a missing file, a syntax error, an
unresolved specifier - the problem comes back as a diagnostic in the result,
with a location, not as a thrown exception unwinding your stack. That matters
most for the callers that are tools themselves: an editor that runs a build on
every keystroke wants to *collect* the diagnostics, render them its own way,
and keep going.

Exceptions still exist, but they are reserved for programming mistakes and for
the handful of operations that genuinely cannot return a value - asking a
disposed context to serve, for instance.

## The same engine from C++ and from JavaScript

The command line is a thin shim. The real surface is a library: `guchho/api.hpp`
in C++, and the `guchho` npm package over a framed service protocol on stdin and
stdout. Everything `guchho build` does is callable from code, down to the
compiler stages - [`lexer`](../api/README.md), `parse`, `print` - so the parts
can be embedded on their own.

If you want to see that surface, the
[embedding guide](../embedding/README.md) is the map.

## What is next

The [release notes](../release-notes/README.md) record what each version added;
the [roadmap-shaped](https://github.com/guchho/guchho/issues) work lives in the
issue tracker.
