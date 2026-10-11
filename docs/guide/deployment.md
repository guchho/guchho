---
title: Deployment
description: Shipping Guchho's output to a static host.
---

# Deployment

Guchho produces an output directory (`dist` by default) that can be uploaded to
any static host. There is no server-side component.

## Absolute URLs

`publicPath` prefixes asset URLs in the output, for a build served from a
sub-path or a CDN:

```js
build: { publicPath: 'https://cdn.example.com/assets/' }
```

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
working without manual edits.

## Source maps

For production, `linked` or `external` maps keep the `.map` files out of the
bundle while staying fetchable:

```js
build: { sourcemap: 'linked', sourcesContent: 'exclude' }
```

## CI

A typical CI step:

```bash
npm ci
npx guchho build --preset production
```

There is no `--preset` in Guchho itself; use a config file plus a build script
per environment, or pass flags directly:

```bash
guchho build --outdir=dist --minify --sourcemap
```

## Archiving

```bash
guchho pack dist -o release.zip
```

See [Packaging](packaging.md).
