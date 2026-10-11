---
title: guchho pack
description: Reference for the archive command.
---

# `guchho pack`

```text
Usage: guchho pack <input...> [options]
```

Create an archive from files and directories.

Every input is stored under its own last path component: `guchho pack dist`
stores the tree under `dist/`, and `guchho pack src/app.js` stores the file as
`app.js`. Symlinks found inside a tree are skipped with a warning rather than
followed.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `--format=<format>` | `zip` | Archive format: `zip`, `tar` or `tar.gz`. |
| `-o`, `--outfile=<path>` | `<input>.<ext>` | Write the archive to this path. |
| `--level=<0-9>` | `6` | Compression level for `zip` and `tar.gz`. |
| `--allow-overwrite` | off | Replace the output if it already exists. |
| `-h`, `--help` | — | Show help. |

`--level` does not apply to `tar`, which stores its entries uncompressed.

## Examples

```bash
guchho pack dist/
guchho pack dist/ -o release.zip
guchho pack dist/ src/ -o release.zip
guchho pack dist/ --level=9
guchho pack dist/ --format=tar
guchho pack dist/ --format=tar.gz -o release.tar.gz
```

## See also

- [Packaging](../../guide/packaging.md)
- [`pack()` API](../../api/README.md#pack)
