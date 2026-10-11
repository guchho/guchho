---
title: Inject configuration at build time
description: Replace an identifier such as process.env.NODE_ENV in the output.
---

# Inject configuration at build time

`--define:` replaces an identifier with the text on the other side of the `=`
before any code runs. It is a substitution, not an environment lookup: the
value you give is the exact text that appears in the output.

```bash
guchho build app.js --define:process.env.NODE_ENV='"production"'
```

The inner quotes are part of the value: the replacement is a JavaScript string
literal, so `process.env.NODE_ENV` becomes `"production"` in the emitted code.
A value that is a number or boolean needs no quotes:

```bash
guchho build app.js --define:DEBUG=true --define:VERSION=42
```

Because the flag splits on the **first** `=`, a value may itself contain `=`:

```bash
guchho build app.js --define:API_URL='"https://example.test/?a=1"'
```

## In a config file

```json
{
  "entryPoints": ["src/index.js"],
  "define": {
    "process.env.NODE_ENV": "\"production\"",
    "DEBUG": "false"
  }
}
```

## What this is good for

- Select a branch of `if (process.env.NODE_ENV === "production")` and let
  minification drop the unreachable half.
- Bake a version string or build timestamp into the output.
- Replace a feature flag so the dead path disappears.

Definitions apply to identifiers, so use the fully qualified name
(`process.env.NODE_ENV`, not `NODE_ENV`) when the code spells it that way.

## See also

- [Build options](../reference/config/build.md) (`define`)
- [Minification](../guide/minification.md)
