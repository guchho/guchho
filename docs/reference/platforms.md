---
title: Platforms
description: The platform conventions Guchho can target.
---

# Platforms

Set with `platform` in configuration or `--platform=<p>` on the command line.
The platform decides which built-in modules are available, which
`package.json` fields and conditions are consulted, and how the output refers
to globals.

| Platform | Meaning |
| --- | --- |
| `browser` | Browser globals; browser conditions. |
| `node` | Node built-ins and `node:` specifiers; the `node` condition. |
| `neutral` | No platform assumptions. |

## Targets

`target` sets the language version the output must run on, independently of the
platform. A target may be a single value or an array:

```js
export default {
  build: {
    target: ['es2020', 'chrome80', 'node16'],
  },
}
```

`engines` and `supported` refine this: `supported` overrides individual feature
checks at runtime.

## See also

- [Targets & browsers](../guide/targets-and-browsers.md)
- [Configuration: build](config/build.md)
