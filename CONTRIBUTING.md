# Contributing to Guchho

This guide explains how to build **Guchho** from source, run the test suite, and produce npm binaries. It covers the per-OS toolchain requirements, how to install them, and every build script - so you can build everything on your platform.


## 1. Project Overview

**Guchho** is a fast, native C++ build system and bundler for modern web applications, written in C++20 and built with **CMake**. It ships both as a source project and as platform-specific prebuilt binaries published to npm.

| Property            | Value                                            |
| ------------------- | ------------------------------------------------ |
| Language            | C++20 (C standard: C17)                          |
| Build system        | CMake (minimum version **3.25**)                 |
| Compilers           | MSVC, GCC, Clang                                 |
| Generators          | Ninja, Visual Studio (presets are generator-aware)|
| Testing             | CTest + header-only framework in `include/test`  |
| Packaging           | CPack (ZIP + NSIS on Windows, TGZ on macOS/Linux)|
| Current version     | 1.0.1                                            |
| Distribution        | npm (`guchho` + `@guchho/<platform>` binaries)   |

### Supported platforms

| Platform         | Native build | CMake presets                                                        |
| ---------------- | :----------: | -------------------------------------------------------------------- |
| Windows x64      | ✅           | `*-win32-x64` (Visual Studio 18 2026), `*-win32-x64-ninja` (Ninja)   |
| Linux x64        | ✅           | `*-linux-x64` (Ninja) + `asan-linux-x64`, `ubsan-linux-x64`          |
| Linux ARM64      | ✅           | `*-linux-arm64` (Ninja)                                              |
| macOS ARM64      | ✅           | `*-darwin-arm64` (Ninja)                                              |
| macOS x64        | ✅           | `*-darwin-x64` (Ninja)                                                |
| CI build         | ✅           | `ci` (inherits `release-win32-x64`, strict warnings)                  |

The npm distribution additionally ships prebuilt binary packages for many platforms
(AIX, Android, BSDs, Solaris, OpenHarmony, and all Linux/Windows architectures).
See [Section 5](#5-building-for-other-npm-distribution-platforms).


## 2. Common Requirements (All Operating Systems)

The following are needed on every OS:

- A C++20-capable compiler (MSVC, GCC, or Clang).
- **CMake ≥ 3.25** (includes `ctest` and `cpack`).
- A shell capable of running Bash scripts (`scripts/*.sh`). On Windows this means **Git Bash** or MSYS2. Alternatively run the raw `cmake`/`ctest` commands listed in [Section 7](#7-manual-cmake-commands).
- **Git** to clone the repository.

> Optional: **Ninja** is only required if you use a `*-ninja` preset or the Linux/macOS presets. It is auto-detected by CMake if the Ninja generator is selected.


## 3. Requirements by Operating System

### 3.1 Windows (x64)

#### Requirements

| Requirement     | Details                                                        |
| --------------- | -------------------------------------------------------------- |
| OS              | Windows 10 / 11 (64-bit)                                        |
| Shell           | Git Bash or MSYS2 (to run `scripts/*.sh` and `bash`)            |
| CMake           | ≥ 3.25 (`winget install Kitware.CMake`)                         |
| Compiler (pick) | **MSVC** via Visual Studio, or **MinGW-w64 GCC**                |
| Ninja           | Only for `*-ninja` presets (`winget install Ninja-build.Ninja`) |
| NSIS (optional) | Only needed to produce the Windows installer via CPack          |

#### Installing on Windows

Install CMake, Git, and Ninja with winget:

```bat
winget install Kitware.CMake
winget install Git.Git
winget install Ninja-build.Ninja
```

Install MSVC (recommended; supports the `Visual Studio 18 2026` generator used by the `*-win32-x64` presets):

```bat
winget install Microsoft.VisualStudio.2022.BuildTools
rem or full IDE:
winget install Microsoft.VisualStudio
```

> After installing Visual Studio Build Tools, open **"Developer Command Prompt for VS"** or run the CMake/`msbuild` commands from a prompt where `vcvarsall.bat` has been sourced so CMake can find MSVC.

Alternative - install MinGW-w64 GCC (used with the Ninja presets):

```bat
winget install mingw-win64.mingw64  --source winget
```

MinGW-w64 must provide a C++20-capable GCC (`g++`), which is satisfied by recent MinGW-w64 releases (GCC 11+).

#### Building on Windows

```bash
# Visual Studio generator (default for build.sh)
bash scripts/build.sh --preset release-win32-x64

# Ninja generator
bash scripts/build.sh --preset release-win32-x64-ninja
```

Binary output: `build/<preset>/bin/guchho.exe`


### 3.2 Linux (x64 and ARM64)

#### Requirements

| Requirement | Details                                                                 |
| ----------- | ----------------------------------------------------------------------- |
| OS          | Any modern Linux (Ubuntu/Debian tested via CI)                           |
| CMake       | ≥ 3.25 (`sudo apt install cmake`)                                        |
| Compiler    | **GCC** (`g++`, C++20) or **Clang** (`clang++`)                          |
| Ninja       | `sudo apt install ninja-build` (all Linux presets use Ninja)             |
| Git         | `sudo apt install git`                                                   |
| Make        | `build-essential` (provides GCC, `make`, and general build tools)         |

For Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git g++ clang
```

On other distros use the equivalent package manager:

```bash
# Fedora/RHEL
sudo dnf install -y gcc-c++ make cmake ninja-build git clang

# Arch
sudo pacman -S --needed base-devel cmake ninja git clang
```

#### Cross-compilation requirements (Linux variants)

Cross-building other Linux architectures requires the matching GCC cross toolchain. Packages below are for Debian/Ubuntu, as used in the CI workflow.

| Target platform | Cross toolchain packages                       |
| --------------- | ---------------------------------------------- |
| linux-arm       | `gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf` |
| linux-arm64     | `gcc-aarch64-linux-gnu g++-aarch64-linux-gnu`  |
| linux-ia32      | `gcc-multilib g++-multilib` (compile with `-m32`) |
| linux-s390x     | `gcc-s390x-linux-gnu g++-s390x-linux-gnu`      |
| linux-ppc64     | `gcc-powerpc64le-linux-gnu g++-powerpc64le-linux-gnu` |
| linux-riscv64   | `gcc-riscv64-linux-gnu g++-riscv64-linux-gnu`  |
| linux-mips64el  | `gcc-mips64el-linux-gnuabi64 g++-mips64el-linux-gnuabi64` |
| linux-loong64   | No GCC cross-compiler → use Docker + QEMU (image `ghcr.io/loong64/loongnix:25`, platform `linux/loong64`) |

#### Building on Linux

```bash
# x64
bash scripts/build.sh --preset release-linux-x64

# ARM64
bash scripts/build.sh --preset release-linux-arm64

# Sanitizers (AddressSanitizer / UndefinedBehaviorSanitizer)
bash scripts/build.sh --preset asan-linux-x64
bash scripts/build.sh --preset ubsan-linux-x64
```

Binary output: `build/<preset>/bin/guchho`


### 3.3 macOS (Apple Silicon ARM64 and Intel x64)

#### Requirements

| Requirement       | Details                                                      |
| ----------------- | ------------------------------------------------------------ |
| OS                | macOS 11+ (Apple Silicon ARM64 or Intel x64)                 |
| Xcode CLT         | `xcode-select --install` (provides Clang)                    |
| CMake             | ≥ 3.25 (`brew install cmake`)                                |
| Ninja             | `brew install ninja` (all macOS presets use Ninja)           |
| Git               | Bundled with Xcode CLT (or `brew install git`)               |
| Homebrew (optional)| Recommended package manager for cmake/ninja                 |

Installing:

```bash
xcode-select --install        # macOS developer tools (Clang)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install cmake ninja git
```

> Note: On macOS the ARM64 and x64 presets explicitly set `CMAKE_OSX_ARCHITECTURES`, so an Intel Mac can only build the `x64` presets and an Apple Silicon Mac the `arm64` presets (no x64-on-arm emulation needed).

#### Building on macOS

```bash
# Apple Silicon (M1/M2/M3/M4)
bash scripts/build.sh --preset release-darwin-arm64

# Intel
bash scripts/build.sh --preset release-darwin-x64
```

Binary output: `build/<preset>/bin/guchho`


### 3.4 FreeBSD / OpenBSD / NetBSD / Solaris (SunOS) / AIX

These platforms are not covered by CMake presets. They are built ad-hoc with CMake and are consumed through the npm `@guchho/*` prebuilt packages. Building from source requires:

- A C++20 compiler (Clang is the default on the BSDs).
- CMake ≥ 3.25.
- Ninja (or a Unix Makefiles generator).

```bash
cmake -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_STANDARD=17 -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_C_EXTENSIONS=OFF -DCMAKE_CXX_EXTENSIONS=OFF
cmake --build build/release --parallel
```

Binary output: `build/release/bin/guchho`


## 4. Install the Toolchain - Quick Script

The quickest per-OS install one-liners:

```bash
# ----------------------------------------
# Windows (run in PowerShell)
# ----------------------------------------
winget install Kitware.CMake Git.Git Ninja-build.Ninja

# ----------------------------------------
# Debian / Ubuntu
# ----------------------------------------
sudo apt update && sudo apt install -y build-essential cmake ninja-build git g++ clang

# ----------------------------------------
# macOS
# ----------------------------------------
xcode-select --install
brew install cmake ninja git
```

Verify the toolchain:

```bash
cmake --version     # >= 3.25
ctest --version
ninja --version     # if using Ninja presets
g++ --version       # or clang++ --version
```


## 5. Building for Other / npm-Distribution Platforms

The npm packages (`@guchho/<platform>`) are produced by the CI workflow `.github/workflows/npm-publish.yml`, which supports multiple build methods on Ubuntu runners:

### 5.1 Android (NDK cross-compilation)

Requirements on the build host (Linux):

- Android **NDK r26b** (`nttld/setup-ndk` action uses this version).
- CMake ≥ 3.25.

| Platform        | ANDROID_ABI      |
| --------------- | ---------------- |
| android-arm     | `armeabi-v7a`    |
| android-arm64   | `arm64-v8a`      |
| android-x64     | `x86_64`         |

All use `ANDROID_PLATFORM=android-21`, `ANDROID_STL=c++_shared`, and the NDK's `android.toolchain.cmake`.

```bash
cmake -B build/release-android-arm64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-21 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_C_STANDARD=17 -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_CXX_STANDARD_REQUIRED=ON
cmake --build build/release-android-arm64 --parallel
```

### 5.2 Windows MSVC cross-compilation (ia32 / arm64)

Build on a Windows runner with Visual Studio, using the `Visual Studio 18 2026` generator and arch `Win32` or `ARM64`:

```bash
cmake -B build/release-win32-arm64 \
  -G "Visual Studio 18 2026" -A ARM64 \
  -DCMAKE_C_STANDARD=17 -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_CXX_STANDARD_REQUIRED=ON
cmake --build build/release-win32-arm64 --config Release --parallel
```

### 5.3 GCC cross-compilation (Linux variants)

See the toolchain package table in [Section 3.2](#322-cross-compilation-requirements-linux-variants). Example:

```bash
cmake -B build/release-linux-arm \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc \
  -DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++ \
  -DCMAKE_C_STANDARD=17 -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DBUILD_TESTING=OFF
cmake --build build/release-linux-arm --parallel
```

### 5.4 Docker + QEMU (loong64)

For `linux-loong64` there is no GCC cross-compiler; use the LoongArch container image with QEMU:

```bash
docker run --rm --platform linux/loong64 \
  -v "$PWD:/workspace" -w /workspace ghcr.io/loong64/loongnix:25 \
  bash -c "apt-get update && apt-get install -y cmake ninja-build g++ pkg-config && \
    cmake -B build/release-linux-loong64 -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_STANDARD=17 -DCMAKE_CXX_STANDARD=20 \
      -DCMAKE_C_STANDARD_REQUIRED=ON -DCMAKE_CXX_STANDARD_REQUIRED=ON \
      -DBUILD_TESTING=OFF && \
    cmake --build build/release-linux-loong64 --parallel"
```

### 5.5 Installing a prebuilt npm binary (no build required)

If you just want to use guchho, install the prebuilt npm package instead of building:

```bash
npm install guchho
```

The `install.js` postinstall hook copies the correct `@guchho/<platform>` binary into `package/npm/guchho/bin/`.


## 6. Build Scripts (Reference)

All scripts live in `scripts/` and are Bash scripts run as `bash scripts/<name>.sh`.

### 6.1 `scripts/build.sh` - Build everything (configure + build + tests)

The **main build entry point**. Runs CMake configure, build, and CTest.

```
Usage: bash scripts/build.sh [options]
```

| Option                | Description                                                |
| --------------------- | ---------------------------------------------------------- |
| `--preset <preset>`   | CMake preset to use. **Default:** `release-win32-x64`      |
| `--target <target>`   | Build only a specific target (e.g. `guchho`, `test_assertions`) |
| `--filter <regex>`    | Run only tests matching the regex (CTest `-R`)             |
| `--timeout <seconds>` | Per-test timeout for CTest                                  |
| `--log <file>`        | Save full build/test output to a file                       |
| `--verbose`, `-v`     | Verbose test output                                         |
| `--all-errors`        | Show all errors. Enables `--no-stop` + verbose build/tests  |
| `--no-stop`           | Do not stop CTest after the first failure                   |
| `--build-verbose`     | Show full compiler/linker commands                          |
| `--test-verbose`      | Show full CTest output                                      |
| `-h`, `--help`        | Show help                                                  |

Examples:

```bash
# Default build (Windows: release-win32-x64)
bash scripts/build.sh

# Explicit preset (e.g. on Linux / macOS)
bash scripts/build.sh --preset release-linux-x64
bash scripts/build.sh --preset release-darwin-arm64

# Build everything and capture a full diagnostic log
bash scripts/build.sh --all-errors --log build-errors.log

# Run only a specific test
bash scripts/build.sh --filter "Assertions.TestName" --all-errors

# Build only a target + log
bash scripts/build.sh --target guchho --log build.log
```

### 6.2 `scripts/test.sh` - Run tests only

Runs CTest against an already-configured build directory. **Default preset:** `release-win32-x64-ninja`.

```
Usage: bash scripts/test.sh [options]
```

Options: `--preset <preset>`, `--filter <regex>`, `--timeout <seconds>`, `--log <file>`, `--verbose`/`-v`, `--all-errors`, `--no-stop`, `--test-verbose`, `-h`/`--help`.

```bash
bash scripts/test.sh                                          # all tests
bash scripts/test.sh --filter "Assertions.*"                   # one suite
bash scripts/test.sh --test-verbose --log test.log            # verbose + log
```

> Requires the project to be built first (`bash scripts/build.sh`), since it needs `<build>/CTestTestfile.cmake`.

### 6.3 `scripts/build-npm.sh` - Native binary → npm package

Builds the current platform's native binary and copies it into `package/npm/@guchho/<platform>/bin/`. Supported platforms: `win32-x64`, `linux-x64`, `darwin-arm64`.

```bash
bash scripts/build-npm.sh
```

Automatically detects the platform, maps it to a preset
(`win32-x64` → `release-win32-x64-ninja`, `linux-x64` → `release-linux-x64`,
`darwin-arm64` → `release-darwin-arm64`), builds, strips (Unix), and copies the binary.

### 6.4 `scripts/compile-commands.sh` - Regenerate `compile_commands.json`

Configures, builds, then copies `build/<preset>/compile_commands.json` to the project root for clangd / tooling.

```bash
bash scripts/compile-commands.sh
```

### 6.5 `scripts/bump-version.sh` - Version bump

Updates the version in `CMakeLists.txt`, `package/npm/guchho/package.json`, and every `package/npm/@guchho/*/package.json`.

```bash
bash scripts/bump-version.sh 1.2.3
```


## 7. Manual CMake Commands

Equivalent to the scripts, in case you do not want to use Bash (e.g. on Windows PowerShell):

```bash
# 1. Configure
cmake --preset <preset>

# 2. Build
cmake --build --preset <preset> --parallel

# 3. Test
ctest --preset <preset> --output-on-failure

# 4. (Optional) Install into a prefix
cmake --install build/<preset> --prefix <install-dir>

# 5. (Optional) Package (ZIP/NSIS on Windows, TGZ elsewhere)
cpack --config build/<preset>/CPackConfig.cmake
```

The presets also define **workflows** that run configure + build + test in one command:

```bash
cmake --workflow --preset release-win32-x64          # Windows
cmake --workflow --preset release-win32-x64-ninja    # Windows (Ninja)
cmake --workflow --preset release-linux-x64          # Linux x64
cmake --workflow --preset release-darwin-arm64       # macOS ARM64
cmake --workflow --preset ci                         # CI (strict)
```

Available single presets for each platform and build type:

| Build type    | Win (VS) / Win (Ninja)        | Linux x64 / arm64          | macOS arm64 / x64      |
| ------------- | ------------------------------ | -------------------------- | ---------------------- |
| Debug         | `debug-win32-x64`             | `debug-linux-x64`          | `debug-darwin-arm64`   |
|               | `debug-win32-x64-ninja`       | `debug-linux-arm64`        | `debug-darwin-x64`     |
| Release       | `release-win32-x64`           | `release-linux-x64`        | `release-darwin-arm64` |
|               | `release-win32-x64-ninja`     | `release-linux-arm64`      | `release-darwin-x64`   |
| RelWithDebInfo| `relwithdebinfo-win32-x64`    | `relwithdebinfo-linux-x64` | `relwithdebinfo-darwin-arm64` |
|               |                                | `relwithdebinfo-linux-arm64`| `relwithdebinfo-darwin-x64` |
| MinSizeRel    | `minsizerel-win32-x64`        | `minsizerel-linux-x64`     | `minsizerel-darwin-arm64` |
|               |                                | `minsizerel-linux-arm64`   | `minsizerel-darwin-x64` |

Special presets:

| Preset              | Purpose                                            |
| ------------------- | -------------------------------------------------- |
| `asan-linux-x64`    | Debug build with AddressSanitizer (GCC/Clang)      |
| `ubsan-linux-x64`   | Debug build with UndefinedBehaviorSanitizer        |
| `ci`                | Release Windows x64 + strict warnings + warnings-as-errors |

> Note: UBSan is ignored under MSVC (unsupported); ASan works on MSVC, GCC, and Clang.


## 8. Build Outputs & Packaging

| Artifact                          | Location                                        |
| --------------------------------- | ----------------------------------------------- |
| Binary (Windows)                  | `build/<preset>/bin/guchho.exe`                 |
| Binary (Linux/macOS/Unix)         | `build/<preset>/bin/guchho`                     |
| Test executables                  | `build/<preset>/test/`                          |
| Compile database (Ninja presets)  | `build/<preset>/compile_commands.json`          |
| Install location                  | `cmake --install` (default `<prefix>/bin`)      |
| npm platform binary               | `package/npm/@guchho/<platform>/bin/guchho[.exe]`|

CPack generators:

- **Windows:** `ZIP;NSIS` → `guchho-<version>-win32-x64.zip` + `.exe` installer (NSIS must be installed).
- **macOS / Linux / Unix:** `TGZ` → `guchho-<version>-<platform>.tar.gz`.

Package file naming: `guchho-<version>-<platform>` (e.g. `guchho-1.0.1-linux-x64`).


## 9. Quick Reference - "Build Everything"

One command per platform (assumes [Section 4](#4-install-the-toolchain--quick-script) was done):

```bash
# ---- Windows ----
bash scripts/build.sh --preset release-win32-x64-ninja   # or release-win32-x64 (VS)

# ---- Linux x64 ----
bash scripts/build.sh --preset release-linux-x64

# ---- Linux ARM64 ----
bash scripts/build.sh --preset release-linux-arm64

# ---- macOS Apple Silicon ----
bash scripts/build.sh --preset release-darwin-arm64

# ---- macOS Intel ----
bash scripts/build.sh --preset release-darwin-x64
```

For a **full build + tests + full error log** on any OS, use:

```bash
bash scripts/build.sh --preset <your-platform-preset> --all-errors --log build-errors.log
```

If the toolchain check fails, run `bash scripts/build.sh --help` and verify `cmake --version` is ≥ 3.25.