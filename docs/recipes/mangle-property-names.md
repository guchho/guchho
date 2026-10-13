---
title: Mangle property names
description: Shorten property accesses safely and keep them stable between builds.
---

# Mangle property names

Mangling turns long property names into one- or two-letter ones in the output.
Because that changes an observable name, you choose which names it may touch
with a pattern, and you can pin the decision so it never shifts between builds.

```bash
guchho build src/index.js --bundle \
  --mangle-props='^_' \
  --reserve-props='^__' \
  --mangle-cache=.guchho-mangle.json
```

- `--mangle-props=` is a regular expression matching the property names to
  shorten. Here, names beginning with a single underscore.
- `--reserve-props=` names the ones to protect, even if the first pattern
  matched them.
- `--mangle-quoted` also shortens quoted accesses (`obj["name"]`), which are
  otherwise left alone.
- `--mangle-cache=` records every decision in a JSON file.

## Why the cache matters

Shortening is only safe if a given property keeps the **same** short name in
every build. If a name moved between two builds, every cached copy of the
previous file - a browser cache, a service worker, an incremental client -
would be invalidated and the whole application would reload for a change that
touched one property. The cache file holds the mapping and is the unit of
consistency; write it to a path you keep under version control or persist in CI.

## In a config file

```json
{
  "entryPoints": ["src/index.js"],
  "bundle": true,
  "mangleProps": "^_",
  "reserveProps": "^__",
  "mangleCache": ".guchho-mangle.json"
}
```

Property mangling and identifier minification are separate: `--minify-identifiers`
shortens local variables, which is always safe; `--mangle-props` changes names
that code outside the bundle may read, which is why it is opt-in and pattern
scoped.

## See also

- [Build options](../reference/config/build.md) (`mangleProps`, `reserveProps`, `mangleQuoted`, `mangleCache`)
- [Minification](../guide/minification.md)
