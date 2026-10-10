---
name: npm-package
description: Work with guchho's npm packages — version bumping, staging platform binaries, running the JS package tests, and the manual publish workflow. Use when editing package/npm/**, bumping versions, or preparing a release.
---

# npm packages and releases

The published npm artifacts live in `package/npm/`:

- `package/npm/guchho/` — the main `guchho` package (JS wrapper + installer).
- `package/npm/@guchho/<platform>/` — one package per platform holding the
  native binary in `bin/` (`guchho` or `guchho.exe`).

The main package pins each platform package at the same version via
`optionalDependencies`, and `package/npm/guchho/lib/platforms.js` maps a
`process.platform`/`arch` key to its `@guchho/<platform>` package.

## Version bumping

```bash
./scripts/bump-version.sh 1.2.3     # rewrite the version everywhere
./scripts/bump-version.sh --verify-only
```

One source of truth is spread across `CMakeLists.txt`, every
`package/npm/**/package.json`, the Chocolatey `.nuspec`, `CONTRIBUTING.md`, and
a couple of source/generated constants. Always bump with the script, never by
hand — it reads every edit back and fails if one silently did nothing.
`--verify-only` asserts agreement without writing.

## Stage a binary and run the JS tests

```bash
bash scripts/build-npm.sh           # build native binary for this host and copy into @guchho/<platform>/bin/
bash scripts/test-npm.sh            # package/npm/test/** via node:test (no install, no network)
bash scripts/test-npm.sh --with-cpp # also drive the C++ suite (scripts/test.sh)
bash scripts/test-npm.sh --filter api-build
```

`build-npm.sh` supports `win32-x64`, `linux-x64`, `darwin-arm64`. `test-npm.sh`
runs the JS-side tests (manifest, installer, tarball, JS API) and needs no
`npm install`.

## Publishing

Publish is manual-only through `.github/workflows/npm-publish.yml`:

```bash
gh workflow run npm-publish.yml -f type=win32-x64 -f dry_run=true
```

- `type` accepts a platform (`linux-x64`, `linux-arm64`, `win32-x64`,
  `win32-arm64`, `darwin-arm64`), `guchho` (main package only), or `all`.
- `dry_run` defaults to `true`; publishing requires `-f dry_run=false`.
- `test_run` defaults to `true` and gates the native test step.
- The workflow file must be on the default branch for a `type` choice value to
  be accepted by `gh`.
- To add a platform: add it to the workflow's `type` options and the plan job's
  MATRIX, and ensure `package/npm/@guchho/<platform>/` and the pin in the main
  `package.json` exist. A native `method` also needs a matching entry in
  `CMakePresets.json` `testPresets` or the test step fails.

Chocolatey uses `scripts/build-chocolatey.sh` / `scripts/publish-chocolatey.sh`.
