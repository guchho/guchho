"use strict";

const fs = require("fs");
const path = require("path");

const PLATFORM_PACKAGES = {
  "win32-x64": "@guchho/win32-x64",
  "linux-x64": "@guchho/linux-x64",
  "darwin-arm64": "@guchho/darwin-arm64",
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
