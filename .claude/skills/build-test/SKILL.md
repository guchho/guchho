---
name: build-test
description: Build and test the guchho C++ project with CMake presets and the repo's scripts. Use when compiling, running tests, or debugging build/test failures in this repository.
---

# Build and test guchho

guchho is a C++20 / CMake project. Build and test with the repo scripts, which
wrap the presets in `CMakePresets.json`.

## Quick start

```bash
bash scripts/build.sh                 # configure + build + test (default preset)
bash scripts/test.sh                  # test only, existing build
```

Default preset is `release-win32-x64` on Windows. Pass `--preset` to pick another,
e.g. `release-win32-x64-ninja`, `release-linux-x64`, `release-darwin-arm64`.

## Useful options

```bash
bash scripts/build.sh --all-errors --log build-errors.log
bash scripts/build.sh --target guchho_bundler_tests
bash scripts/build.sh --filter "Bundler.*"
bash scripts/test.sh --preset release-linux-x64 --filter "Assertions.*" --all-errors
```

- `--all-errors` shows every build and test error (implies `--no-stop`).
- Test filters are ctest regexes; a `--target guchho_<module>_tests` derives the
  filter `<module>` automatically.
- `--filter` must be given a regex, not a literal test name, for `.*` matches.

## Raw commands (no script)

```bash
cmake --preset <preset> --fresh
cmake --build --preset <preset> --parallel
ctest --preset <preset> --output-on-failure --no-tests=error
```

## Notes

- Only `release-*` / `debug-*` presets defined in `CMakePresets.json` are valid.
- A `ctest --preset <preset>` needs a matching entry in `testPresets`; add one if
  you introduce a new preset and the test step fails with "unknown preset".
- Cross-compiled and MSVC builds pass `-DBUILD_TESTING=OFF`; only native builds
  can run the test suite.
- Windows binaries are `*.exe` under `build/<preset>/bin/` (sometimes
  `bin/Release/`); Unix binaries are named `guchho`.

## npm publish (maintainers)

The GitHub workflow is manual-only:

```bash
gh workflow run npm-publish.yml -f type=win32-x64 -f dry_run=true
```

`type` is one of the platforms listed in the workflow's `workflow_dispatch`
options, plus `guchho` (main package) and `all`. The workflow file must be on
the default branch for `-f type=<choice>` to be accepted.
