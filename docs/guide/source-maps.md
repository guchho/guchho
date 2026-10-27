---
title: Source maps
description: Inline, linked and external source maps.
---

# Source maps

A source map records how a generated file relates back to the files it was
built from. When a browser or an error tracker reads one, a stack trace that
points at `dist/assets/app-7C2D4E.js:1:842` can be shown as
`src/app.ts:12:7` instead.

`sourcemap` controls how the map for each emitted file is produced.

| Value | Behaviour |
| --- | --- |
| `none` | No map (the default). |
| `inline` | Appended as a `//# sourceMappingURL=data:…` comment. |
| `linked` | A `.map` file plus a linking comment. The usual choice for a build uploaded to a web server. |
| `external` | A `.map` file with no linking comment, for pipelines that upload maps to a symbol server. |
| `both` | Both, so the artefact works on its own and a symbol server still has a file. Config files also accept the alias `inline-and-external`. |

```bash
guchho build src/app.js --sourcemap --outdir=dist
```

The same choice through the API:

```js
await build({ entrypoints: ['src/app.js'], outdir: 'dist', sourcemap: 'linked' })
```

`true` selects the default mode: `linked` when the build has an output
location, and `inline` when the command line names no output location. `both`
writes the map into the file and beside it; the config-file alias is
`inline-and-external`.

## What each mode leaves on disk

The difference is easiest to see by looking at the output.

### `none`

Nothing is written and no comment is added. This is the default because an
unasked-for map changes the bytes of every output file.

### `inline`

The map is base64-encoded into the file itself:

```js
// dist/assets/app-7C2D4E.js
//# sourceMappingURL=data:application/json;base64,eyJ2ZXJzaW9uIjozLCJzb3VyY2VzIjpbIi4uL3NyYy9hcHAuanMiXSx9
```

The advantage is that the file is self-contained: copy one file and the
debugger still has its map. The cost is that the file is larger, and the map
cannot be replaced without replacing the code.

Inline maps are convenient for development, for a single-file distribution, or
for a worker or dynamic import that is loaded from somewhere the `.map` file
would be hard to place.

### `linked`

A separate map file is written next to the output, and the output gets a
comment that points at it:

```js
// dist/assets/app-7C2D4E.js
//# sourceMappingURL=app-7C2D4E.js.map
```

```text
dist/assets/
├── app-7C2D4E.js
└── app-7C2D4E.js.map
```

This is the usual production choice when the maps are allowed to be public: the
browser fetches the map only when devtools is open, and the code file stays
small.

### `external`

The map file is written, but the code has no `sourceMappingURL` comment:

```text
dist/assets/
├── app-7C2D4E.js
└── app-7C2D4E.js.map
```

The comment is omitted because the consumer is not the browser. A symbol server
or error tracker is told where the map is by the upload step, and leaving the
comment out means a browser does not discover it on its own. That is useful when
the map contains source you do not want to serve publicly.

### `both`

Both an inline copy and an external file are produced:

```text
dist/assets/
├── app-7C2D4E.js          # contains an inline map
└── app-7C2D4E.js.map      # the same map, for the symbol server
```

The artefact works on its own if it is copied somewhere without the `.map`
file, and the symbol server still gets a standalone file to store. The tradeoff
is size: the map is present twice.

## Choosing a mode

| Situation | Useful choice |
| --- | --- |
| Local development | `inline` or `linked` |
| A build deployed with its maps | `linked` |
| Maps uploaded to an error tracker, not published | `external` |
| A self-contained single file | `inline` |
| Both a standalone artifact and a symbol server | `both` |
| A build that must not reveal source | `none`, or `external` with private access |

There is no mode that both hides the map from a browser and leaves a comment
that the browser can follow. `external` is the mode for "the file exists, but
the browser is not told about it".

## Multiple outputs

The setting applies to every emitted JavaScript and CSS file, not just the entry
point. A build with an entry chunk, a shared chunk and a stylesheet writes three
pairs:

```text
dist/
├── assets/
│   ├── app-7C2D4E.js
│   ├── app-7C2D4E.js.map
│   ├── chunk-2B9E04.js
│   ├── chunk-2B9E04.js.map
│   ├── style-91B0AF.css
│   └── style-91B0AF.css.map
└── index.html
```

With `linked`, each `.js` and `.css` file carries the comment that names its own
map. A copied or renamed file does not share a map with the file it came from.

## Sources content

`sourcesContent` decides whether the pre-transform source text is embedded:

- `true` (default) - write a `sourcesContent` array, making the map
  self-contained.
- `false` - omit it, for much smaller maps when the sources are already
  reachable from wherever the debugger runs.

A self-contained map looks like this:

```json
{
  "version": 3,
  "sources": ["../src/app.js"],
  "sourcesContent": ["const answer = 42;\nconsole.log(answer);\n"],
  "mappings": "AAAA,MAAM,SAAS;AACf,QAAQ,IAAI..."
}
```

With `sourcesContent: false`, the `sourcesContent` key is absent. The debugger
can still show the original source if it can fetch the path in `sources`; if it
cannot, the stack trace shows the generated position instead.

```bash
guchho build src/app.js --outdir=dist \
  --sourcemap=linked --sources-content=false
```

The default is `true` because a map that works without a second request is more
useful. The reason to turn it off is size and privacy: embedding the source
makes the map larger, and it means the source travels wherever the map travels.
If the maps are uploaded to an error tracker and not published, keeping the
content is often the point of generating the map in the first place.

## Source root

`sourceRoot` writes a `sourceRoot` field into the map. Tools resolve each entry
in `sources` against that prefix:

```bash
guchho build src/app.js --outdir=dist \
  --sourcemap=linked \
  --source-root=https://example.com/
```

If a `sources` entry is `src/app.ts`, the debugger looks for
`https://example.com/src/app.ts`. The prefix changes how a tool locates the
source; it does not change where the file is written on disk.

Common uses:

```bash
# A source server
guchho build src/app.js --outdir=dist --sourcemap=external \
  --source-root=https://sources.example.com/

# A tracker that understands a virtual scheme
guchho build src/app.js --outdir=dist --sourcemap=external \
  --source-root=webpack://my-app/

# A sub-path on the same host
guchho build src/app.js --outdir=dist --sourcemap=linked \
  --source-root=/app/src/
```

Because the value is used as written, a wrong prefix produces paths that cannot
be fetched. Check `sources` and `sourceRoot` in the generated map if a debugger
shows the generated file instead of the original.

## Building on an existing map

When an input already has a source map, Guchho reads it and composes the new map
with the old one. A breakpoint in the final bundle still points at the original
file, not at an intermediate file that happened to be the input to this build.

That matters when a source file is already the output of another tool: a
TypeScript compiler, a CSS preprocessor, or a previous Guchho transform. The
chain is preserved as long as the incoming `sourceMappingURL` resolves.

For a caller that already holds a map and wants it embedded rather than written
beside the output, `sourcemapRaw` takes the map itself. This is mainly for
embedding scenarios where the map is produced elsewhere and should be attached
to the generated code.

## Source maps from `transform`

`transform` has no output directory, so it follows different rules:

- `sourcemap: true` or `'inline'` embeds the map in `code`, and also returns it
  on `map`.
- `'external'` leaves `code` without a `sourceMappingURL` comment and returns
  the map on `map`, for a caller that stores it itself.
- `'both'` embeds the map and returns it, which is useful when the same result
  is stored and passed on.
- `'linked'` is refused, because a link needs a separate file beside the output
  and a transform has no output directory.
- `sourcefile` is required when a map is requested, because the map needs a name
  for the original source.

```js
import { transform } from 'guchho';

const { code, map } = await transform(source, {
  loader: 'ts',
  sourcemap: true,
  sourcefile: 'src/app.ts',
});

console.log(code); // ends with a sourceMappingURL comment
console.log(map);  // the same map as JSON text
```

Without `sourcefile`, the build has no honest name to put in the map, and the
request is reported as an error rather than writing a map with a guessed name.

## A production workflow

A common production setup keeps the maps out of the browser's way while still
giving an error tracker what it needs:

```js
// release.mjs
import { build } from 'guchho';

await build({
  entrypoints: ['src/index.html'],
  outdir: 'dist',
  bundle: true,
  minify: true,
  sourcemap: 'linked',
  sourcesContent: false,
});
```

```bash
node release.mjs
```

The same build with flags:

```bash
guchho build src/index.html --outdir=dist --bundle --minify \
  --sourcemap=linked --sources-content=false
```

Then:

1. Upload the `.map` files to the error tracker as part of the release.
2. Either leave them on the server (if the source is not sensitive) or remove
   them from the public deploy after upload.
3. Keep the release's commit or version alongside the maps, so a stack trace
   from an older deploy can be matched to the maps that belong to it.

If the artifact must work without the map file and the tracker needs a separate
copy, use `both`:

```bash
guchho build --sourcemap=both --outdir=dist
```

If the maps must never be discoverable by a browser, use `external` and upload
the files yourself:

```bash
guchho build --sourcemap=external --outdir=dist
```

## Constraints

`linked` and `external` maps are written beside the emitted file, so a build
with no output path at all - for example, one that writes to standard output -
cannot use them. That case is reported as an error rather than dropping the map
silently.

On the command line, a bare `--sourcemap` means `linked` when `--outdir` or
`--outfile` is named, and `inline` when neither is. Naming `linked` or
`external` explicitly keeps that mode. In the JavaScript API, `sourcemap: true`
follows the same rule; pass `'linked'` or `'external'` to be explicit.

A linked or external map is written next to the file it describes. Renaming or
moving only the `.js` file leaves the comment pointing at a path that no longer
exists.

## Debugging a map

When a debugger shows generated code instead of the source:

1. **Is the comment there?** Look for `//# sourceMappingURL=...` at the end of
   the emitted file. `external` has none by design.
2. **Does the map exist at that URL?** Fetch the map path from the network
   panel. A `404` means the map was not deployed or the comment points at the
   wrong place.
3. **Are the `sources` paths right?** They are relative to the map's location
   unless `sourceRoot` changes them. A map in `dist/assets/` referring to
   `../src/app.js` expects the source tree to be reachable from there.
4. **Is `sourcesContent` present?** If it is absent and the source cannot be
   fetched, the debugger has nothing to display.
5. **Is devtools allowing maps?** The browser's setting for JavaScript source
   maps must be enabled. A map that works in one browser and not another is
   often a setting, not a malformed map.
6. **Was the map generated for this build?** A stale `.map` beside a new `.js`
   file produces line numbers that look plausible and are wrong. A content
   hash in the name is the simplest way to keep the pair together.

## Privacy and deployment

A source map can contain the original source text. Treat a map with
`sourcesContent: true` as source code, not as build metadata:

- Do not publish maps that contain private code.
- Upload maps to the error tracker and remove them from the public directory if
  they are not meant to be public.
- Use `sourcesContent: false` when the maps themselves are published but the
  source is not.
- Remember that `external` only removes the browser's pointer; it does not make
  a publicly served `.map` file private. Access control belongs to the host.

The [deployment guide](deployment.md) covers cache headers and the rest of a
production upload.

## See also

- [Minification](minification.md) - maps are what make compressed output
  debuggable.
- [Targets & browsers](targets-and-browsers.md) - lowering changes the mapping.
- [Deployment](deployment.md) - publishing or withholding maps.
- [Build options](../reference/config/build.md) - the full option list.
