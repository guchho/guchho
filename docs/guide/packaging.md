---
title: Packaging
description: Creating zip, tar and tar.gz archives from build output.
---

# Packaging

`guchho pack` creates an archive from files and directories already on disk. It
is usually the last step of a build:

```bash
guchho build --outdir=dist
guchho pack dist/ -o release.zip
```

It does not read your configuration or resolve a graph; it walks the paths you
name. That makes it safe to run in a release script after anything else has
produced the files.

```bash
guchho pack dist/
guchho pack dist/ -o release.zip
guchho pack dist/ src/ -o release.zip
guchho pack dist/ --level=9
guchho pack dist/ --format=tar
guchho pack dist/ --format=tar.gz -o release.tar.gz
```

## What goes in

Every input is stored under its own last path component: `guchho pack dist`
stores the tree under `dist/`, and `guchho pack src/app.js` stores the file as
`app.js`. The name is taken from the input after normalising it, so `dist/`,
`./dist` and `a/b/../dist` all root at `dist`, and no absolute host path, `.` or
`..` is ever stored in the archive. Several inputs therefore sit side by side:

```bash
guchho pack dist/ src/ -o release.zip
# release.zip
#   dist/...
#   src/...
```

The walk is deliberately literal:

- Hidden files are included. Nothing is filtered by name, so `.gitignore` and
  `.env` are archived like anything else. If something should not ship, keep it
  out of the input rather than expecting an ignore list.
- Empty directories are kept as trailing-slash entries, so a directory that
  held nothing does not disappear from an otherwise faithful copy.
- Symbolic links found inside a tree are skipped with a warning rather than
  followed. An input named on the command line is the exception: naming a link
  is naming the thing it points at, so it is followed exactly once, and the root
  it produces keeps the link's own name.
- Special files - fifos, sockets, devices - are skipped with a warning, because
  none of the formats has anywhere to put them.
- Two inputs that would produce the same archive path are an error rather than
  a silent second copy.
- The output file is excluded when it sits inside one of the input trees, with a
  warning, so `guchho pack dist/ -o dist/out.zip --allow-overwrite` does not
  archive the previous run's output into the next one.
- A missing input is an error, and no archive is written.

Warnings never fail the command. They are printed to stderr whether the run
succeeds or fails, and the [`pack()` API](#programmatic) returns them as a list.
The archive itself is written to a temporary file next to the destination and
renamed into place only after it is complete and closed, so a failed run leaves
an existing archive untouched and no half-written file under the requested name.

## Choosing a format

| Format | Extension | Compression | `--level` |
| --- | --- | --- | --- |
| `zip` | `.zip` | Deflate | yes |
| `tar` | `.tar` | None | no |
| `tar.gz` | `.tar.gz` | gzip | yes |

`zip` is the default. Format names are lower case and matched exactly; `ZIP` is
refused, and the error names every format that is supported. `tar.gz` is a
single format name rather than a tar with a separate compression switch, and its
default extension is the two-component `.tar.gz`.

`--level` runs from `0` (store, do not compress) to `9` (compress hardest) and
defaults to `6`. A value outside that range is refused rather than clamped, so a
typo cannot quietly become a different level. `tar` stores its entries
uncompressed, so a level paired with `tar` is refused too rather than accepted
and ignored.

## Naming the output

With one input, `-o` is optional. The default name replaces the input's last
extension with the format's own, or appends the extension when there is none:

| Input | Format | Default output |
| --- | --- | --- |
| `dist/` | `zip` | `dist.zip` |
| `dist/` | `tar.gz` | `dist.tar.gz` |
| `src/app.js` | `zip` | `src/app.zip` |
| `.bashrc` | `zip` | `.bashrc.zip` |

A name already written for the format is returned unchanged, which is why
`release.tar.gz` does not become `release.tar.tar.gz`. If that would mean
archiving an input into itself, name a different output with `-o`.

Multiple inputs need an explicit output:

```bash
guchho pack dist/ src/ -o release.zip
```

The output's parent directory is created if it is missing. An existing output is
never replaced unless you pass `--allow-overwrite`; overwriting a release archive
by accident is invisible until someone downloads the wrong bytes, so it is
opt-in.

## Reproducible metadata

The programmatic `pack()` accepts `date` and `mode` to give every entry the same
timestamp and permission bits. Both are **recorded, not applied**: the values are
written into the archive's entry headers and describe the files to whoever
extracts them; packing does not chmod or touch the files on disk. The command
line has no flags for them, because a release script that needs them is already
able to call the API.

### A fixed timestamp

`date` takes a `Date` and writes it into every entry header:

```js
const built = new Date('2024-01-01T00:00:00Z')

await pack({
  inputs: ['dist'],
  outFile: 'release.zip',
  date: built,
})
```

Without it each entry keeps the modification time of the file it came from, so
two archives of identical content can differ in bytes merely because they were
made at different times. With it, the archive is a function of the files and the
date alone.

The date is cut to whole seconds before it reaches the engine, and then stored at
the precision the format has - not the precision of the `Date`:

| Format | Field | Range | Precision |
| --- | --- | --- | --- |
| `zip` | MS-DOS date and time, in local time | 1980-01-01 through 2107-12-31 | 2 seconds |
| `tar` | Unix seconds | 1970-01-01 onwards | 1 second |
| `tar.gz` | Unix seconds inside the tar | 1970-01-01 onwards | 1 second |

A date outside the format's range is refused, not wrapped or clamped. The epoch
(`new Date(0)`) is therefore valid for `tar` and `tar.gz` and refused for `zip`;
that asymmetry is the MS-DOS field, not a choice this library made.

Because ZIP interprets its field in the machine's local time zone, the same
`Date` can produce different bytes on two machines in different zones. Pin the
zone when byte-identical zips matter:

```bash
TZ=UTC node release.js
```

`tar` stores an absolute instant and has no such dependency.

### Permission bits

`mode` is masked to `07777`, keeping setuid, setgid and sticky bits. When a mode
is given, a directory also gets its execute bits set, so a file-style value such
as `0644` never produces a directory nobody can enter.

### A reproducible release, step by step

1. Pick the timestamp the archive should carry. `SOURCE_DATE_EPOCH` is the
   convention reproducible-build tools already set, and a fixed date is the
   fallback when it is absent:

   ```js
   const epoch = Number(process.env.SOURCE_DATE_EPOCH)
   const date = Number.isFinite(epoch)
     ? new Date(epoch * 1000)
     : new Date('2024-01-01T00:00:00Z')
   ```

2. Build the files that will be archived, as usual.

3. Pack them with that date, and with `mode` if the permissions should not
   depend on the machine that produced them:

   ```js
   const archive = await pack({
     inputs: ['dist'],
     outFile: 'release.zip',
     date,
     mode: 0o644,
   })
   ```

4. Pin the time zone for the run so ZIP's local-time field does not move with
   the machine. Pack the same inputs again with a second `outFile` (and the same
   `date` and `mode`), then compare the two byte for byte. Equal files are the
   proof that the archive depends only on the inputs and the chosen metadata:

   ```bash
   TZ=UTC node release.js
   cmp release.zip release-copy.zip
   ```

   For `tar` and `tar.gz` the `TZ` prefix is unnecessary; their timestamp is an
   absolute instant.

5. Keep the date with the release rather than recomputing it. A release that
   regains "today's" timestamp is no longer the same release, and consumers
   comparing archives will see a difference that is not in the files.

## Programmatic

```js
import { pack } from 'guchho'

const { path, size, warnings } = await pack({
  inputs: ['dist', 'src/index.html'],
  outFile: 'release.zip',
})
```

| `pack()` option | Meaning |
| --- | --- |
| `inputs` | Non-empty array of files and directories. Required. |
| `outFile` | Where to write. Required; the parent is created if missing. |
| `format` | `zip` (default), `tar` or `tar.gz`. |
| `level` | `0`-`9`, default `6`. Refused with `tar`. |
| `overwrite` | Replace an existing `outFile`. Off by default. |
| `date` | A `Date` written into every entry header. |
| `mode` | Unix permission bits, `0` to `0o7777`, written into every entry. |

The result carries `path`, the finished archive's `size` in bytes, and
`warnings`. The request is validated before the engine starts: a missing option
or wrong type throws `TypeError`, an out-of-range level, mode or date throws
`RangeError`, and a refusal from the engine throws a `BuildFailure` with its own
message. See the [pack API](../api/README.md#pack).

## Extracting

`guchho unpack` is the other half: it reads a zip, tar or tar.gz archive and
writes its entries to disk.

```bash
guchho unpack release.zip
guchho unpack release.tar.gz -o out/
guchho unpack archive.tar --format=tar
guchho unpack bundle.zip --strip-components=1
guchho unpack release.zip --overwrite
```

The format is detected from the archive's extension first (`.zip`, `.tar`,
`.tar.gz`, `.tgz`), then from its magic bytes when the name gives nothing away.
An archive whose format can be neither recognised nor named is an error rather
than a guess.

Without `-o`, the output directory is the archive's name with a known extension
removed: `release.zip` extracts into `release/`. The directory is created if it
is missing, and an existing regular file at that path is an error. Existing
files are kept unless `--overwrite` is given, and every skipped file is reported
as a warning.

`--strip-components=<n>` drops the first `n` path components from every entry,
like `tar --strip-components`. An entry with too few components to strip is
skipped with a warning instead of being written under a shortened name that was
never in the archive.

Paths that are absolute or contain `..` are refused, so an archive cannot write
outside the output directory. Symbolic links are skipped with a warning rather
than followed, matching what `guchho pack` does with links found inside a tree;
hardlinks and special files are skipped the same way. Permissions and
modification times from the archive are restored where the host file system
supports them.

The function form takes the same options as flags:

```js
import { unpack } from 'guchho'

const { outDir, files, warnings } = await unpack({
  archive: 'release.zip',
  outDir: 'check',
  stripComponents: 1,
})
```

`outDir`, `format` and the rest have the same defaults as the command line;
`preservePermissions` and `preserveTimestamps` are additional booleans, both
`true` by default. `unpack()` is native-only: the browser entry exports the name
and throws, because the WebAssembly engine has no host file system to extract
into. See the [`unpack` reference](../reference/cli/unpack.md).

A quick round-trip check is to extract into a scratch directory and compare it
with the tree that was packed:

```bash
guchho pack dist/ -o release.zip
guchho unpack release.zip -o check/
diff -r dist check/dist
```

## See also

- [`guchho pack` reference](../reference/cli/pack.md)
- [`guchho unpack` reference](../reference/cli/unpack.md)
- [Pack a distribution](../recipes/pack-a-distribution.md)
- [Shipping a distribution with `guchho pack`](../blog/packaging-with-guchho-pack.md)
- [`pack()` and `unpack()` API](../api/README.md#pack)
