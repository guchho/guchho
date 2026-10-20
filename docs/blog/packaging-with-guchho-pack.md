---
title: Shipping a distribution with guchho pack
description: Turning a build directory into one archive, and why the details matter.
date: 2026-10-14
---

# Shipping a distribution with `guchho pack`

Version 1.0.4 added a command that had been the last missing piece of a build
pipeline: `guchho pack`. After a build you usually want to hand the output to
someone - an artifact store, a release page, a colleague - as a single file.
`pack` does that, and getting the small details right is most of the work.

```bash
guchho build --outdir=dist
guchho pack dist/ -o release.zip
```

## The layout is the input's own name

Every input is stored under its own last path component. `guchho pack dist/`
stores the tree under `dist/`; `guchho pack src/app.js` stores the file as
`app.js`. That rule is what makes several inputs behave predictably - you get
one top-level entry per input, named the way you named it, rather than an
archive whose contents depend on where you ran the command.

## Format and level are separate choices

`--format` picks `zip`, `tar` or `tar.gz`. `--level` (0 through 9) is the
compression effort, and it only means something where there is compression to
do: `tar` stores its entries uncompressed and ignores the flag. Keeping them
separate is why `--level` is not silently accepted and ignored for `tar` - the
[reference](../reference/cli/pack.md) says so, and the implementation agrees.

## It will not surprise you with an overwrite

The output file must not already exist unless you pass `--allow-overwrite`.
Overwriting a release archive by accident is the kind of mistake that is
invisible until someone downloads the wrong bytes, so it is opt-in.

Symlinks found inside a tree are skipped with a warning rather than followed.
An archive cannot escape the directory you named, and packaging is not the place
to discover you have a link pointing at your home directory.

## From a program

The command line and the API are the same engine. [`pack()`](../api/README.md#pack)
returns the archive bytes instead of writing a file, so a caller can upload them
directly, sign them, or hold them in memory. See the
[`pack` recipe](../recipes/pack-a-distribution.md) for the command-line shapes.

## Where it fits

`pack` is the step after the build and before the distribution. It does not read
your project or your config; it takes paths and produces bytes, which is exactly
what makes it safe to run in a release script.
