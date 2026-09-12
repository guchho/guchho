"use strict";

const fs = require("fs");
const path = require("path");

const PLATFORM_PACKAGES = {
  "aix-ppc64": "@guchho/aix-ppc64",
  "android-arm": "@guchho/android-arm",
  "android-arm64": "@guchho/android-arm64",
  "android-x64": "@guchho/android-x64",
  "darwin-arm64": "@guchho/darwin-arm64",
  "darwin-x64": "@guchho/darwin-x64",
  "freebsd-arm64": "@guchho/freebsd-arm64",
  "freebsd-x64": "@guchho/freebsd-x64",
  "linux-arm": "@guchho/linux-arm",
  "linux-arm64": "@guchho/linux-arm64",
  "linux-ia32": "@guchho/linux-ia32",
  "linux-loong64": "@guchho/linux-loong64",
  "linux-mips64el": "@guchho/linux-mips64el",
  "linux-ppc64": "@guchho/linux-ppc64",
  "linux-riscv64": "@guchho/linux-riscv64",
  "linux-s390x": "@guchho/linux-s390x",
  "linux-x64": "@guchho/linux-x64",
  "netbsd-arm64": "@guchho/netbsd-arm64",
  "netbsd-x64": "@guchho/netbsd-x64",
  "openbsd-arm64": "@guchho/openbsd-arm64",
  "openbsd-x64": "@guchho/openbsd-x64",
  "openharmony-arm64": "@guchho/openharmony-arm64",
  "sunos-x64": "@guchho/sunos-x64",
  "win32-arm64": "@guchho/win32-arm64",
  "win32-ia32": "@guchho/win32-ia32",
  "win32-x64": "@guchho/win32-x64",
};

function getBinaryName() {
  return process.platform === "win32" ? "guchho.exe" : "guchho";
}

function install() {
  const platformKey = `${process.platform}-${process.arch}`;
  const platformPkg = PLATFORM_PACKAGES[platformKey];
  const binaryName = getBinaryName();

  if (!platformPkg) {
    console.warn(
      `guchho: No pre-built binary available for ${platformKey}.\n` +
        `You can build from source instead:\n` +
        `  https://github.com/guchho/guchho#readme`
    );
    return;
  }

  // Check if the platform-specific package was installed
  try {
    const pkgDir = path.dirname(
      require.resolve(`${platformPkg}/package.json`)
    );
    const srcBinary = path.join(pkgDir, "bin", binaryName);
    const destBinary = path.join(__dirname, "bin", binaryName);

    if (!fs.existsSync(srcBinary)) {
      console.warn(
        `guchho: Binary not found in ${platformPkg}.\n` +
          `Please report this issue: https://github.com/guchho/guchho/issues`
      );
      return;
    }

    // Copy binary to guchho's bin directory
    fs.mkdirSync(path.dirname(destBinary), { recursive: true });
    fs.copyFileSync(srcBinary, destBinary);

    // Make executable on Unix
    if (process.platform !== "win32") {
      fs.chmodSync(destBinary, 0o755);
    }

    console.log(`guchho: Installed binary for ${platformKey}`);
  } catch {
    // Platform package not installed (optional dependency may have been skipped)
    console.warn(
      `guchho: Platform package ${platformPkg} was not installed.\n` +
        `The binary will not be available until you install it:\n` +
        `  npm install ${platformPkg}`
    );
  }
}

install();
