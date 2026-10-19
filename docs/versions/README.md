---
title: Versions
description: How Guchho's documentation is versioned and where frozen snapshots live.
---

# Versions

The documentation you are reading describes the **current** line of Guchho -
the next release, kept in step with `main`. When a version is released, a frozen
snapshot of the whole docs tree is taken and never edited again.

## The current version

**1.0.8** is the latest release, and the pages at the top level of this site
describe it. See the [release notes](../release-notes/README.md) for what each
version changed.

## How snapshots work

- The top-level sections - [guide](../guide/README.md),
  [reference](../reference/README.md), [api](../api/README.md),
  [plugins](../plugins/README.md), [embedding](../embedding/README.md),
  [recipes](../recipes/README.md) - are the living documentation.
- On a release, that tree is copied under `versions/v<major>.<minor>/` and
  becomes read-only. A reader who needs the docs for an older version reads the
  snapshot, not a tag they have to check out.
- Snapshots are never patched. If a page in a snapshot is wrong, the fix goes
  into the current tree and into the next snapshot; the old one stands as what
  that release actually shipped.
- Generated pages ([api](../api/generated/README.md),
  [embedding](../embedding/generated/README.md)) are frozen with their
  snapshot, because they describe the headers of that release.

## Permalinks

When linking across versions, link to the snapshot explicitly rather than to a
top-level page, so the target does not change under the reader when the current
line moves on.
