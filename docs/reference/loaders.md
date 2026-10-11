---
title: Loaders
description: Every loader Guchho ships, and how the loader map is spelled.
---

# Loaders

A loader says how a file's bytes are read. A build configures them as a map from
file extension to loader name:

```js
export default {
  build: {
    loader: {
      '.png': 'file',
      '.txt': 'text',
      '.wasm': 'binary',
    },
  },
}
```

On the command line the same map is written with repeated `--loader` flags:

```bash
guchho build src/index.html --loader:.png=file --loader:.txt=text
```

## Loader names

| Loader | Effect |
| --- | --- |
| `js` | Parse as JavaScript. |
| `jsx` | Parse as JavaScript with JSX. |
| `ts` | Parse as TypeScript. |
| `tsx` | Parse as TypeScript with JSX. |
| `json` | Parse as JSON; the module's default export is the value. |
| `text` | The file's text as a string default export. |
| `css` | Parse as CSS and contribute to the stylesheet. |
| `html` | Parse as an HTML document. |
| `base64` | The bytes, base64-encoded, as a string. |
| `dataurl` | A `data:` URL for the bytes. |
| `file` | Emit the file and export its URL. |
| `binary` | The bytes as a `Uint8Array`. |
| `copy` | Copy the file to the output unchanged and export nothing. |
| `empty` | An empty module. |

## See also

- [Assets & loaders](../guide/assets-and-loaders.md)
- [Configuration: build](config/build.md)
