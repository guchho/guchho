---
title: Core concepts
description: The vocabulary used throughout the Guchho documentation.
---

# Core concepts

## Entry points

An **entry point** is a file the build starts from. It may be an HTML document,
a JavaScript module, or a stylesheet. Everything reachable from an entry point
is part of the build.

## The graph

As Guchho reads a file it discovers imports, URLs, `@import` rules and asset
references. The result is a **module graph**. Whether the graph is inlined or
left as separate files depends on the `bundle` option.

## Loaders

A **loader** decides how a file's bytes become a module. A `.json` file is an
object that participates in bundling; a `.png` under the `copy` loader is an
opaque asset emitted verbatim and referenced by URL; the same bytes under `text`
are an inert string. See [Assets & loaders](assets-and-loaders.md).

## Bundle vs. transform

- A **build** has a file system, a graph and package resolution. Use it to
  produce a directory of output.
- A **transform** works on a single string in memory. Use it for editor
  integrations and pipelines that already did their own bundling.

## Options are input only

`BuildOptions` and `TransformOptions` are plain structs whose members all have
defaults. You name the few fields you care about and the rest fall into place.

## Failure is data

A build hands back a result carrying `errors` and `warnings`. A build with only
warnings is still a successful build. On failure the `errors` list is non-empty
and the output fields stay empty.

## Effective options

Guchho ranks settings in a fixed order:

1. **Explicit** — a flag on the command line, or a field assigned in C++.
2. **Config file** — `guchho.config.js`, `guchho.config.json`, then
   `guchho.json`.
3. **Built-in defaults.**
4. **Derived values** — an output file implies its directory; a bundle implies a
   format.

Every command and embedding goes through the same resolution, so a project sees
the same settings however it is driven. See [Configuration](configuration.md).
