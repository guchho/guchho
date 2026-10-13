---
title: FAQ
description: Questions that come up before the manual helps.
---

# Frequently asked questions

## Installation and requirements

**How do I install Guchho?**

```bash
npm install -g guchho
```

The npm package ships a prebuilt binary for your platform, so there is no
compile step. On Windows you can also install through Chocolatey.

**Do I need Node at runtime?**

Not to run the engine. The binary is self-contained. Node is how the package is
distributed and how the JavaScript API talks to the binary over a service
protocol.

**What platforms are supported?**

npm ships prebuilt binaries for the major desktop platforms (Linux, macOS,
Windows), plus `android-x64` since 1.0.4. If your platform has no prebuilt
binary, build from source - see
[CONTRIBUTING.md](https://github.com/guchho/guchho/blob/main/CONTRIBUTING.md).

## Building

**Why did my HTML build bundle when I did not ask?**

An HTML entry point cannot be served as one copied file, so when a document is
the entry and `bundle` was not set either way, Guchho enables bundling. An
explicit setting - a flag or a config field - always wins.

**How do I stop something being bundled?**

Mark it external: `--external:pkg` on the command line, or the `external` field
in config. See [build a library](../recipes/build-a-library.md).

**How do I see why a bundle is large?**

Add `--analyze` (or `--analyze=verbose`) and, if you want the raw data,
`--metafile`. See [analyze bundle size](../recipes/analyze-bundle-size.md).

**Where does configuration live, and what wins?**

`guchho.config.js`, `guchho.config.json` or `guchho.json` beside the project.
A command-line flag wins over the config file, which wins over the built-in
defaults. See the [configuration reference](../reference/config/README.md).

## Running and serving

**What is the difference between `dev`, `serve` and `watch`?**

`dev` builds, serves and rebuilds on change; `watch` rebuilds on change without
serving; `serve` serves an already-built directory without rebuilding. See
[serve and watch](../recipes/serve-and-watch.md).

**Can I use Guchho from JavaScript instead of the CLI?**

Yes - the package exposes `build`, `transform`, `pack`, `context`, `watch`,
`serve` and the compiler stages. See the
[JavaScript API](../api/README.md).

**Can I use it from C++?**

Yes. The engine is a library with a public header; the CLI is a thin layer over
it. See the [embedding guide](../embedding/README.md).

## Errors and exit codes

**What does exit code 2 mean?**

The command line itself was rejected - a bad flag or value - as opposed to a
build that ran and failed (1). The full list is in the
[exit codes reference](../reference/exit-codes.md).

**Why does my build not throw on an error?**

Failure is data in Guchho: problems come back as diagnostics in the result so a
tool can collect and display them. See
[errors and warnings](../reference/errors-and-warnings.md).

## Contributing and support

**Where do I report a bug or ask a question?**

Open an issue at
[github.com/guchho/guchho/issues](https://github.com/guchho/guchho/issues) with
the command, the output, and what you expected.

**Is Guchho open source?**

Yes, under the MIT license. See [Resources](README.md).
