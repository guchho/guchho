---
title: Legal comments
description: Preserving or relocating license banners and headers.
---

# Legal comments

A legal comment is a banner a rewriter must not silently delete - `/*! … */`
headers, `//! …` comments and similar markers. They are how a dependency says
"this file contains code from a project whose licence requires this notice to
travel with it".

Minification removes ordinary comments. A legal comment is the exception: it is
identified before the printer and the printer follows `legalComments` when it
reaches one. The setting therefore answers a narrow question - *where should the
notice end up?* - while [banners and footers](#banners-and-footers) answer a
different one - *what text should I add?*

## What counts as a legal comment

Guchho recognizes a comment as legal in two ways:

- the opening marker is followed by `!`, as in `/*! ... */` or `//! ...`;
- the comment contains `@license` or `@preserve` anywhere, as in
  `/* @license MIT */` or `// @preserve Copyright 2026`.

Everything else is an ordinary comment. Ordinary comments disappear with
whitespace minification and are not moved by `legalComments`.

```js
/*! (c) 2026 CodeHemu - MIT */
//! Bundled under the MIT licence.

/* @license Apache-2.0 */
// @preserve This notice survives minification.
```

The detection applies to JavaScript and CSS alike. A CSS file with a `/*! ... */`
header is treated the same way as a JavaScript file with one, and the license
text of a bundled stylesheet is kept under the same rules.

The markers are a convention, not a licence check. Guchho cannot tell whether a
comment is legally required; it only knows which comments look like notices. If
a project needs a notice that is not written as one, add it with `banner`
instead of hoping the minifier preserves an ordinary comment.

## Why there is a setting

There are three reasonable answers for a bundle that collects code from many
packages:

1. Keep each notice next to the code it came from.
2. Move every notice into one block at the end of the file.
3. Put the notices in a separate file and leave a pointer in the code.

All three satisfy "the notice travels with the output", and they differ in how
readable the output is, how large it is, and where a person can find the text.
`legalComments` is how you choose between them.

## The values

| Value | Behaviour |
| --- | --- |
| *unset* | Resolves to `eof` when bundling, `inline` otherwise. |
| `none` | Drop the banners. Only legal when the licence allows it. |
| `inline` | Keep each banner next to the code it covers. |
| `eof` | Collect every banner and emit them once at the end of the output. |
| `linked` | Put the text in a `<name>.LEGAL.txt` file and leave a pointer comment. |
| `external` | Write the `<name>.LEGAL.txt` files with no pointer comment. |

The accepted values are `none`, `inline`, `eof`, `linked` and `external`. `eof`
is the short spelling of "end of file"; `endOfFile` is not accepted and is
reported as an invalid value rather than quietly ignored.

```bash
guchho build src/app.js --bundle --outdir=dist --legal-comments=eof
```

```js
import { build } from 'guchho';

await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
  bundle: true,
  legalComments: 'linked',
});
```

A value outside the list is an error that names the accepted set:

```text
Invalid value "eofile" in "--legal-comments=eofile"
NOTE: Valid values are "none", "inline", "eof", "linked", or "external".
```

A typo is refused rather than treated as "use the default", because a build that
silently drops licence text is worse than one that stops.

## The modes in detail

### `inline`

`inline` keeps the banner where the author put it. In a bundle, that means the
comment stays with the part of the output that came from its module:

```js
// input, src/a.js
/*! a.js - MIT */
export const a = 1;

// input, src/b.js
/*! b.js - Apache-2.0 */
export const b = 2;
```

```js
// output, roughly, with inline
/*! a.js - MIT */
const a = 1;

/*! b.js - Apache-2.0 */
const b = 2;
```

This is the default for an unbundled build and for a transform, where the output
still has the source file's shape. It is the easiest mode to audit because the
notice is beside the code it applies to. The cost is that a bundle with many
dependencies grows by one banner per module.

### `eof`

`eof` removes each banner from its original position and collects them at the
end of the output. It is the default for a bundle, because a bundle is one file
and the notices can be read together instead of interrupting the code.

```js
// input
// src/a.js
/*! a.js - MIT */
export const a = 1;

// src/b.js
/*! b.js - Apache-2.0 */
export const b = 2;
```

```js
// output, roughly, with eof
const a = 1;
const b = 2;

/*! a.js - MIT */
/*! b.js - Apache-2.0 */
```

First-party comments are de-duplicated: the same notice text is emitted once
regardless of how many files carried it. Comments that came from packages under
`node_modules` are grouped by package path and collected under a
`Bundled license information:` header, so the output makes it clear which
dependency each notice belongs to:

```js
const a = 1;

/*! Bundled license information:

some-package/dist/index.js:
  /*! (c) Some Package - MIT */

another-package/dist/index.js:
  /*! (c) Another Package - Apache-2.0 */
*/
```

The block is still at the end of the file. A configured `footer` is written
after it, which keeps generated text such as a build timestamp as the final line
of the file.

### `linked`

`linked` writes the notices to a separate file and puts a short pointer comment
in the code:

```text
dist/app.js
dist/app.js.LEGAL.txt
```

```js
// dist/app.js, roughly
/*! For license information please see app.js.LEGAL.txt */
```

```text
# dist/app.js.LEGAL.txt
/*! a.js - MIT */
/*! b.js - Apache-2.0 */

Bundled license information:

some-package/dist/index.js:
  /*! (c) Some Package - MIT */
```

The pointer is a legal comment itself, so it survives minification and still
tells a reader where the full text went. The rule file sits next to the chunk
and is named after the chunk's final output name, so a hashed bundle produces
`app-3F2A1B.js.LEGAL.txt` and a pointer that names the same file.

This mode is a good fit for a library or a large bundle: the shipped JavaScript
stays small, the notices are still part of the output directory, and a licence
scan can read a plain text file instead of parsing the bundle.

`linked` needs somewhere to write the file, so a build must have an output
directory or output file. A transform has no output directory, so `linked` is
rejected there; use `inline` or `eof` instead.

### `external`

`external` writes the same `.LEGAL.txt` files but leaves no pointer in the code:

```text
dist/app.js
dist/app.js.LEGAL.txt
```

```js
// dist/app.js, roughly
const a = 1;
const b = 2;
```

The notices exist on disk, but the shipped code does not refer to them. That is
useful when the notice is surfaced somewhere else - an "open source licences"
page, a documentation bundle, or a distribution that already includes a
`LICENSES/` directory. It is not useful when nobody reads the directory, because
the code no longer points at it.

Like `linked`, `external` needs an output location because a `.LEGAL.txt` file
has to be written somewhere.

### `none`

`none` removes every recognized banner:

```bash
guchho build src/app.js --bundle --legal-comments=none --outdir=dist
```

This is the only mode that can make the output legally incomplete. It is
appropriate for code whose licence does not require the notice to travel, or for
an intermediate build whose notices are handled by a later step. It is not a
general-purpose size optimization; if a dependency's licence requires the
notice, dropping it is the one size win not worth taking.

## Detection and de-duplication

The details are worth knowing when a notice seems to be missing or appears more
than once:

- **Detection happens per comment.** A comment containing `@license` or
  `@preserve` is legal even without a leading `!`.
- **First-party duplicates collapse.** Identical notice text from several source
  files is emitted once.
- **Third-party notices are grouped.** Comments from paths under
  `node_modules` are associated with their package path, and identical bodies
  from several files of the same package are merged.
- **JavaScript and CSS are handled separately.** A JavaScript chunk and a CSS
  chunk each get their own legal-comment output, and each `linked`/`external`
  chunk gets its own `.LEGAL.txt` file.
- **Closing tags are escaped.** In a bundle that may be inlined into HTML, a
  `</script>` inside a notice is escaped so it cannot end the script early. CSS
  notices get the same treatment for `</style>`.

The text is copied, not rewritten. Whitespace inside a banner is preserved, and
the original comment delimiters are kept. That keeps the notice recognizable and
avoids changing its meaning.

## Defaults and output shape

When `legalComments` is not set, the answer depends on how the output is built:

| Situation | Resolved value |
| --- | --- |
| `bundle: true` | `eof` |
| A build without bundling | `inline` |
| A transform | `inline` |

The reasoning is the same in each case: a bundle has one natural place for a
collected notice, while an unbundled file has a position that still corresponds
to the source. The default is a behaviour, not a value to pass; `default` is not
one of the accepted spellings on the command line.

A `.LEGAL.txt` file is named after the final output file plus `.LEGAL.txt`:
`app.js.LEGAL.txt`, `app-3F2A1B.css.LEGAL.txt`, and so on. In `linked` mode the
pointer comment uses a relative path between the chunk and the notice file, so
the pair keeps working when the whole output directory is moved.

A build that chooses `linked` or `external` without an output path fails with a
diagnostic rather than writing the notices into an unexpected location. The
build needs to know where the `.LEGAL.txt` file belongs.

## Banners and footers

`legalComments` preserves text that already exists. `banner` and `footer` add
text that did not exist in any source file. They are separate options because
they answer different questions:

- a legal comment comes from a file and may need to be preserved, moved or
  externalized;
- a banner or footer is written unconditionally, is not detected, and is not
  de-duplicated.

### In a config file

In `guchho.config.js`, both are maps from output kind to text. The recognized
keys are `js` and `css`:

```js
export default {
  build: {
    banner: {
      js: '/*! (c) 2026 CodeHemu - MIT */',
      css: '/*! theme - MIT */',
    },
    footer: {
      js: '/* build: production */',
      css: '/* generated by Guchho */',
    },
  },
};
```

### On the command line

A plain `--banner=` or `--footer=` means the JavaScript half. The colon form
names the kind:

```bash
guchho build src/index.html --outdir=dist \
  --banner='/*! (c) 2026 CodeHemu - MIT */' \
  --footer:css='/* generated by Guchho */'
```

The same applies to `--footer`. A colon flag without an `=` is an error that
shows the two accepted spellings, rather than guessing which half was meant.

### Through the API

The JavaScript API takes a string for `banner` and `footer`. A build applies it
to the JavaScript output; a transform applies it to whichever kind the loader
produced:

```js
await build({
  entrypoints: ['src/app.js'],
  outdir: 'dist',
  banner: '/*! (c) 2026 CodeHemu - MIT */',
  footer: '/* built with Guchho */',
});
```

```js
await transform(source, {
  loader: 'css',
  banner: '/*! theme - MIT */',
});
```

The text is written exactly as supplied. It is not wrapped in a comment for you,
so it must already be valid for the file it is going into. A banner that is not
a comment can turn valid JavaScript into a syntax error:

```js
// broken: this text is not a comment
banner: 'Copyright 2026 CodeHemu'
```

```js
// correct
banner: '/* Copyright 2026 CodeHemu */'
```

The same text is not removed by minification and is not folded into the legal
comments block. A banner is placed at the top of the file, after a hashbang if
the entry has one. A footer is placed at the bottom, after any `eof` legal
comments. Using both `linked` and a banner is fine: the banner stays at the top
of the code, and the pointer to the notice file stays where the legal comments
were collected.

## Practical recipes

### A bundled application

Leave the option unset. A bundle defaults to `eof`, which keeps the shipped file
small and puts every notice in one place:

```bash
guchho build src/index.html --outdir=dist --bundle --minify
```

### A library

Use `linked` so the notice file is shipped next to the code without inflating
the JavaScript:

```bash
guchho build src/index.ts --bundle --format=esm --outdir=dist \
  --legal-comments=linked
```

Check that the output directory contains both the bundle and its `.LEGAL.txt`
partner before publishing.

### Code with no notice requirement

Use `none` only after reading the licences involved:

```bash
guchho build src/internal-tool.js --outdir=dist --legal-comments=none
```

If a dependency requires attribution, this is the wrong switch no matter how
much smaller the file becomes.

### Your own notice plus dependency notices

Use `banner` for the notice you own and leave `legalComments` at its default for
the notices that come from other people:

```js
build: {
  banner: { js: '/*! My App - MIT */' },
}
```

The banner is written once at the top; collected dependency notices still go to
the end (or to a `.LEGAL.txt` file, if `linked` is chosen).

## Common pitfalls

- **Spelling it `endOfFile`.** The accepted value is `eof`.
- **Expecting every comment to survive.** Only `/*!`, `//!`, `@license` and
  `@preserve` comments are legal comments. Use `banner` for anything else.
- **Using `none` without checking the licences.** The build will happily remove
  notices it is legally required to keep.
- **Choosing `linked` or `external` without an output path.** The notice file has
  to be written somewhere; the build reports the missing output path.
- **Expecting a `.LEGAL.txt` file to contain only third-party notices.** It
  contains first-party notices too, followed by the grouped third-party block.
- **Assuming a banner is a comment.** The text is inserted verbatim, so the
  comment delimiters are your responsibility.
- **Wondering why a notice appears once.** Duplicate notices are intentionally
  collapsed; that is the deduplication working, not the notice being lost.

## See also

- [Minification](minification.md) - how ordinary comments are removed and which
  switches keep the output readable.
- [Build options](../reference/config/build.md) - `legalComments`, `banner`,
  `footer` and the rest of the output settings.
- [`guchho build`](../reference/cli/build.md) - the command-line flags in one
  table.
- [Configuration](configuration.md) - the config-file shape for `banner`,
  `footer` and the other build fields.
