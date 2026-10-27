---
title: Deployment
description: Shipping Guchho's output to a static host.
---

# Deployment

Guchho produces an output directory (`dist` by default) that can be uploaded to
any static host. There is no server-side component, no runtime process and no
special headers required by the output itself. A deploy is the act of copying
that directory somewhere and pointing a URL at it.

This page covers the parts of a build that matter after the build: URLs, cache
invalidation, source maps, CI and archives.

## A production build

A reasonable production configuration:

```js
// guchho.config.js
export default {
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
    bundle: true,
    splitting: true,
    format: 'esm',
    target: 'es2020',
    minify: true,
    sourcemap: 'linked',
    entryNames: 'js/[name]-[hash]',
    chunkNames: 'js/chunks/[name]-[hash]',
    assetNames: 'assets/[name]-[hash]',
  },
};
```

```bash
guchho build
npx guchho serve dist --port=8080   # preview before uploading
```

The same build spelled with flags:

```bash
guchho build src/index.html \
  --outdir=dist \
  --bundle \
  --splitting \
  --format=esm \
  --minify \
  --sourcemap=linked
```

`splitting` requires ES module output and more than one entry or a shared
module. If it is not needed, leave it off; the build is simpler and the output
is smaller for a single entry.

## Absolute URLs with `publicPath`

`publicPath` prefixes asset URLs in the output. It changes how files are
addressed, not where they are written:

```js
build: { publicPath: 'https://cdn.example.com/' }
```

```html
<!-- before -->
<script type="module" src="./js/app-a1b2c3.js"></script>

<!-- after -->
<script type="module" src="https://cdn.example.com/js/app-a1b2c3.js"></script>
```

Use it when the output is served from a CDN or from a sub-path:

```js
// A CDN
build: { publicPath: 'https://cdn.example.com/assets/' }

// A static host under /app/
build: { publicPath: '/app/' }
```

A trailing slash is conventional but not required; Guchho does not double one if
you include it. The prefix is treated as configured, so a typo in the path is a
broken URL, not a corrected one.

`publicPath` does not rewrite the files on disk. `dist/js/app-a1b2c3.js` is
still written there; the HTML and JS simply refer to it through the prefix.

## Content hashing

Give output names a content hash so a deploy invalidates caches only for the
files that changed:

```js
build: {
  entryNames: '[name]-[hash]',
  chunkNames: 'chunks/[name]-[hash]',
  assetNames: 'assets/[name]-[hash]',
}
```

Guchho rewrites every URL that points at a renamed file, so the HTML keeps
working without manual edits. The hash changes when the content changes, which
is what lets you cache the file forever.

A typical layout:

```text
dist/
  index.html
  js/app-7f3a1c.js
  js/chunks/vendor-2b9e04.js
  assets/logo-91cc02.png
  assets/Inter-4d10aa.woff2
```

| File kind | Suggested cache policy |
| --- | --- |
| `index.html` | `no-cache` or a short lifetime |
| Hashed JS, CSS and assets | `public, max-age=31536000, immutable` |
| Source maps | private or excluded from the public deploy |

The HTML is the entry point, so it must revalidate; everything it points at is
addressed by content and can be cached indefinitely. This pairing is the point
of hashing.

### Hashes without a CDN

Hashing is still useful on a plain static host. The host serves the new
`app-7f3a1c.js` after a deploy, and the browser's cached `app-<oldhash>.js` is
simply never requested again. Without hashes, a changed file keeps its old name
and clients may keep serving the old bytes until the cache expires.

## Source maps

For production, `linked` or `external` maps keep the `.map` files out of the
bundle while staying fetchable. Set `sourcesContent` to `false` to omit the
original source text and make the maps much smaller:

```js
build: { sourcemap: 'linked', sourcesContent: false }
```

| Mode | What is emitted |
| --- | --- |
| `none` | No map |
| `inline` | A data URL appended to the output |
| `external` | A `.map` file beside the output, no comment |
| `linked` | A `.map` file and a `sourceMappingURL` comment |
| `both` | An inline copy and an external file |

`external` and `linked` are the usual production choices: the map exists for an
error tracker, but it is not inlined into the bundle. If the maps contain
proprietary source, upload them to your error tracker and do not publish them
with the site.

## CI

A typical CI step:

```bash
npm ci
npx guchho build
```

There is no `--preset` in Guchho itself. Use a config file plus a build script
per environment, or pass flags directly:

```bash
guchho build --outdir=dist --minify --sourcemap
```

An environment-aware config keeps the CI command short:

```js
// guchho.config.js
const production = process.env.NODE_ENV === 'production';

export default {
  build: {
    entry: 'src/index.html',
    outdir: 'dist',
    bundle: true,
    minify: production,
    sourcemap: production ? 'linked' : 'inline',
    define: {
      'process.env.NODE_ENV': production ? '"production"' : '"development"',
    },
  },
};
```

```bash
NODE_ENV=production npx guchho build
```

On Windows PowerShell the environment variable is set differently:

```powershell
$env:NODE_ENV = 'production'
npx guchho build
```

If the CI machine has no native binary, install `@guchho/wasm` and the CLI can
still build everything except `serve`, `watch` and `dev`. See
[WebAssembly](../wasm.md).

## Serving the result

`guchho serve` serves an already-built directory without rebuilding:

```bash
guchho serve                     # configured output directory
guchho serve dist                # explicit directory
guchho serve dist --port=8080
guchho serve dist --open
```

It is for previewing exactly what a deploy would ship. Use `guchho dev` when you
want a rebuild on change; `serve` never rebuilds.

## Client-side routing and SPA fallback

A static host needs to fall back to `index.html` for routes that do not
correspond to a file:

```text
/            -> index.html
/about       -> index.html   (no about.html exists)
/assets/x.js -> assets/x.js
```

How you configure that depends on the host (a rewrite rule, a fallback setting,
or a `404.html` copy). `publicPath` does not provide it; it only prefixes URLs
that the build emits.

For the JavaScript API's server, the `server.fallback` option does the same job.
For `guchho serve`, the CLI serves the directory as it is.

## MIME types and compression

Most static hosts infer these correctly, but a custom server should send:

| Extension | Type |
| --- | --- |
| `.js` | `text/javascript` |
| `.mjs` | `text/javascript` |
| `.css` | `text/css` |
| `.json` | `application/json` |
| `.wasm` | `application/wasm` |
| `.woff2` | `font/woff2` |

Compress text output at the host with gzip or Brotli rather than archiving it
into the deploy. A `.gz` file next to the original is not the same as
`Content-Encoding: gzip`; the host must actually negotiate the encoding.

## Archiving a release

If the deploy pipeline wants a single artifact:

```bash
guchho build
guchho pack dist -o release.zip
```

`pack` walks the paths you name and does not read the config, so it is safe to
run after the build. See [Packaging](packaging.md).

## Verifying a deploy

Before uploading:

```bash
guchho build
guchho serve dist --port=8080   # open the site locally
```

After uploading, check:

- the page loads with the network panel showing hashed names,
- `index.html` is not served from a stale cache,
- source maps resolve in your error tracker if you publish them,
- requests under a sub-path use the same `publicPath` you configured.

## Common pitfalls

- **Setting `publicPath` without a trailing slash is fine, but inventing the
  wrong base is not.** The prefix is used verbatim; check the network panel.
- **Hashing one kind of file but not another.** If HTML references an unhashed
  asset, that asset keeps its old cache entry when it changes.
- **Caching HTML forever.** The HTML is the file that names the hashed assets;
  it must revalidate.
- **Publishing source maps with source content.** `sourcesContent` defaults to
  true; set it to `false` if the source should not ship.
- **Using `guchho serve` in production.** It is a local preview server, not a
  production static host.
- **Expecting `publicPath` to configure SPA fallback.** It only prefixes URLs;
  route fallback is a host setting.
- **Deploying `dist` before the build finishes.** `pack` and most hosts do not
  care, but a half-written output is a broken deploy; wait for the exit code.

## See also

- [Bundling & splitting](bundling-and-splitting.md) - hashing, splitting and
  output names.
- [Configuration](configuration.md) - environment-aware config files.
- [Packaging](packaging.md) - zip, tar and tar.gz archives.
- [CLI overview](cli-overview.md) - `build`, `serve` and exit codes.
- [WebAssembly](../wasm.md) - building without a native binary.
