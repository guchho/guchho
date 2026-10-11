---
title: Packaging
description: Creating zip, tar and tar.gz archives from build output.
---

# Packaging

`guchho pack` creates an archive from files and directories already on disk.

```bash
guchho pack dist/
guchho pack dist/ -o release.zip
guchho pack dist/ src/ -o release.zip
guchho pack dist/ --level=9
guchho pack dist/ --format=tar
guchho pack dist/ --format=tar.gz -o release.tar.gz
```

Every input is stored under its own last path component: `guchho pack dist`
stores the tree under `dist/`, and `guchho pack src/app.js` stores the file as
`app.js`. Symlinks found inside a tree are skipped with a warning rather than
followed.

## Options

| Option | Meaning |
| --- | --- |
| `--format=<format>` | `zip` (default), `tar` or `tar.gz` |
| `-o`, `--outfile=<path>` | Output path (default `<input>.<ext>`) |
| `--level=<0-9>` | Compression for `zip` and `tar.gz` (default `6`) |
| `--allow-overwrite` | Replace an existing output |

`--level` is refused with `tar`, which stores its entries uncompressed.

## Programmatic

```js
import { pack } from 'guchho'

const { path, size, warnings } = await pack({
  inputs: ['dist', 'src/index.html'],
  outFile: 'release.zip',
})
```

`date` and `mode` options are **recorded, not applied**: they are metadata the
archive carries for whoever extracts it. A failure throws a `BuildFailure` with
the engine's own message. See the [pack API](../api/README.md#pack).
