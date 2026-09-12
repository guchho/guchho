"use strict";

const child_process = require("child_process");
const path = require("path");
const fs = require("fs");

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

function getPlatformKey() {
  const platform = process.platform;
  const arch = process.arch;
  return `${platform}-${arch}`;
}

function getBinaryName() {
  return process.platform === "win32" ? "guchho.exe" : "guchho";
}

function getBinaryPath() {
  const platformKey = getPlatformKey();
  const binaryName = getBinaryName();

  // 1. Check for platform-specific package (installed via optionalDependencies)
  const platformPkg = PLATFORM_PACKAGES[platformKey];
  if (platformPkg) {
    try {
      const pkgDir = path.dirname(require.resolve(`${platformPkg}/package.json`));
      const binary = path.join(pkgDir, "bin", binaryName);
      if (fs.existsSync(binary)) {
        return binary;
      }
    } catch {
      // Package not installed, fall through
    }
  }

  // 2. Check for locally built binary (development mode)
  // When installed via npm, __dirname is node_modules/guchho/lib/
  // When running from source, __dirname is package/npm/guchho/lib/
  const projectRoot = path.resolve(__dirname, "..", "..", "..", "..");
  const localPaths = [
    path.join(__dirname, "..", "bin", binaryName),
    path.join(projectRoot, "build", "release-win32-x64-ninja", "bin", binaryName),
    path.join(projectRoot, "build", "release-win32-x64", "bin", "Release", binaryName),
    path.join(projectRoot, "build", "release-linux-x64", "bin", binaryName),
    path.join(projectRoot, "build", "release-darwin-arm64", "bin", binaryName),
    path.join(projectRoot, "build", "debug-win32-x64-ninja", "bin", binaryName),
  ];
  for (const p of localPaths) {
    if (fs.existsSync(p)) {
      return p;
    }
  }

  throw new Error(
    `Could not find guchho binary for ${platformKey}.\n` +
      `Please install the platform-specific package:\n` +
      `  npm install @guchho/${platformKey}\n` +
      `Or build from source: https://github.com/guchho/guchho#readme`
  );
}

function spawnBinary(args, options = {}) {
  const binaryPath = getBinaryPath();
  const spawnOptions = {
    stdio: "inherit",
    ...options,
  };

  return new Promise((resolve, reject) => {
    const child = child_process.spawn(binaryPath, args, spawnOptions);
    child.on("close", (code) => {
      if (code !== 0) {
        const err = new Error(`guchho exited with code ${code}`);
        err.status = code;
        reject(err);
      } else {
        resolve(code);
      }
    });
    child.on("error", reject);
  });
}

module.exports = {
  getBinaryPath,
  getPlatformKey,
  spawnBinary,
};
