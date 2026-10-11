---
title: Pack a distribution
description: Produce a versioned archive of the output.
---

# Pack a distribution

`guchho pack` turns files and directories into an archive, so a build's output
can be handed off as one file.

```bash
guchho build --outdir=dist
guchho pack dist/ -o release.zip
```

Every input is stored under its own last path component: `dist/` is stored as
the tree under `dist/`, and a single file `src/app.js` is stored as `app.js`.

## Choose the format and level

```bash
guchho pack dist/ --format=tar.gz -o release.tar.gz
guchho pack dist/ --format=tar                 # uncompressed
guchho pack dist/ --level=9 -o release.zip     # maximum compression
```

`--level` (0–9) applies to `zip` and `tar.gz`; `tar` stores its entries
uncompressed and ignores it.

## Name it for the release

The default output name is the input with the archive extension appended, so
give an explicit `-o` when the artifact has a version in its name:

```bash
guchho pack dist/ -o "release-1.4.0.zip"
```

Symlinks found inside a tree are skipped with a warning rather than followed, so
an archive cannot escape the directory you named.

## Keep it honest

- The output file must not exist unless you pass `--allow-overwrite`.
- Multiple inputs become one archive with one top-level entry each.

The programmatic form is [`pack()`](../api/README.md#pack), which hands back the
archive bytes rather than writing a file.

## See also

- [`guchho pack`](../reference/cli/pack.md)
- [Packaging](../guide/packaging.md)
