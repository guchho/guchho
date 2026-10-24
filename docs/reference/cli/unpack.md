---
title: guchho unpack
description: Reference for the archive extraction command.
---

# `guchho unpack`

```text
Usage: guchho unpack <archive> [options]
```

Extract a zip, tar or tar.gz archive into a directory.

The format is detected from the archive's extension first (`.zip`, `.tar`,
`.tar.gz`, `.tgz`), then from its magic bytes when the name gives nothing away.
Absolute paths and `..` components are rejected with a warning, so an archive
cannot write outside the output directory. Symbolic links and other special
entries are skipped with a warning rather than followed.

## Options

| Option | Default | Meaning |
| --- | --- | --- |
| `-o`, `--outdir=<dir>` | `<archive without extension>` | Extract into this directory. It is created if missing. |
| `--format=<format>` | auto-detect | Archive format: `zip`, `tar` or `tar.gz`. |
| `--overwrite` | off | Replace files that already exist. |
| `--strip-components=<n>` | `0` | Drop the first `n` path components from every entry. |
| `-h`, `--help` | - | Show help. |

Existing files are kept unless `--overwrite` is given; each skipped file is
reported as a warning. Permissions and modification times from the archive are
restored where the host file system supports them.

## Examples

```bash
guchho unpack dist.zip
guchho unpack release.tar.gz -o out/
guchho unpack archive.tar --format=tar
guchho unpack bundle.zip --strip-components=1
guchho unpack release.zip --overwrite
```

## See also

- [Packaging](../../guide/packaging.md)
- [`guchho pack`](pack.md)
- [`unpack()` API](../../api/README.md#unpack)
