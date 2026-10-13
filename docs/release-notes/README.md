---
title: Release notes
description: What changed in each released version of Guchho.
---

# Release notes

One page per released version, newest first. Each page is written for someone
using Guchho: what changed for them, and what they should do about it. The
canonical diff for any release is the comparison on GitHub, linked from the
page.

The current version is **1.0.4**. Install or upgrade with:

```bash
npm install -g guchho
```

## Releases

| Version | Date | Highlight |
| --- | --- | --- |
| [1.0.4](v1.0.4.md) | 2026-10-11 | `guchho pack` and the `pack()` API; packaging formats. |
| [1.0.3](v1.0.3.md) | 2026-10-07 | Engine refinements; Chocolatey packaging; publish pipeline fixes. |
| 1.0.2 | 2026-10-06 | See the tag `v1.0.2`. |
| 1.0.1 | 2026-10-08 | See the tag `v1.0.1`. |

Tags live at
[github.com/guchho/guchho/tags](https://github.com/guchho/guchho/tags).

## How these are written

- Notes describe behaviour, not commits. A dozen internal commits that fix one
  user-visible bug become one line.
- A breaking change is called out under **Upgrading**, with the migration.
- If a release has nothing a user would notice, the page says so rather than
  padding the list.

## See also

- [Versions](../versions/README.md) - frozen documentation snapshots.
- [Changelog on GitHub](https://github.com/guchho/guchho/commits/main).
