---
title: Assets & loaders
description: How a loader turns a file's bytes into a module.
---

# Assets & loaders

A **loader** decides how a file's bytes become a module. Choose one per file
extension with the loader map:

```js
build: {
  loader: { '.svg': 'dataurl', '.png': 'file', '.txt': 'text' },
}
```

A `build` takes a loader **map** keyed by extension; a `transform` takes a
single loader name.

## The loaders

| Loader | Result |
| --- | --- |
| `js` | JavaScript |
| `jsx` | JavaScript with markup |
| `ts` | TypeScript (types erased) |
| `tsx` | TypeScript with markup |
| `json` | Parsed object whose keys may be named imports |
| `text` | A plain string |
| `css` | A stylesheet that participates in CSS bundling |
| `global-css` | A stylesheet with global rules |
| `local-css` | A stylesheet scoped to the importing module |
| `html` | An HTML document |
| `base64` | A `data:…;base64` URL string |
| `dataurl` | A `data:` URL |
| `file` | A file wrapper exposing path and bytes |
| `binary` | A typed-array view over the raw bytes |
| `copy` | Pass the file through untouched, expose its output URL |
| `empty` | An empty module, for stubbing a file out |

`none` and `default` mean "not chosen yet": the engine picks one from the file
extension, nearest package metadata, and any per-path overrides.

## Choosing a loader

- **`copy`** - the file is emitted verbatim and referenced by URL. Use it for
  anything the browser loads directly (fonts, large images).
- **`file`** - the module wraps the file and exposes its bytes and path.
- **`binary`** - the bytes arrive as an array you can pass to an API.
- **`text`** - the contents are an inert string.
- **`empty`** - a stub, useful for swapping a module out.

The full option is documented in the
[configuration reference](../reference/config/build.md) and the
[loaders reference](../reference/loaders.md).
