---
title: Analyze bundle size
description: See what is big and why, without a second tool.
---

# Analyze bundle size

Two flags answer "why is my bundle this big". They can be used together.

```bash
guchho build app.js --bundle --minify --analyze
```

`--analyze` prints a size report grouped by what each package and file
contributes. For the full per-input breakdown, ask for the verbose form:

```bash
guchho build app.js --bundle --minify --analyze=verbose
```

## Keep the data

`--analyze` builds the report from the metafile, and if that metafile does not
exist yet it keeps it in memory for the report. To write it out as JSON as well,
ask for it explicitly:

```bash
guchho bui
