"use strict";

const child_process = require("child_process");
const path = require("path");
const fs = require("fs");

const { PLATFORM_PACKAGES } = require("./platforms");

function getPlatformKey() {
  const platform = process.platform;
  const arch = process.arch;
  return `${platform}-${arch}`;
}

function getBinaryName() {
  return process.platform === "win32" ? "guchho.exe" : "guchho";
}

// Whether a build directory name is for this platform and architecture.
//
// The names come from the CMake presets: "release-win32-x64", "debug-linux-arm64",
// and a toolchain suffix on some, so the platform and arch are two adjacent
// pieces in the middle rather than a prefix. They are matched as whole pieces
// rather than as a substring, because a substring match on "win32-x64" also
// accepts "win32-x64backup" and would then run a binary for a different
// machine.
function matchesPlatform(dirName, [platform, arch]) {
  const parts = dirName.split("-");
  return parts.some((part, i) => part === platform && parts[i + 1] === arch);
}

function getBinaryPath() {
  const platformKey = getPlatformKey();
  const binaryName = getBinaryName();

  // An explicit override, before anything is searched. This has to be here
  // rather than only in the test helper: this is the function that decides
  // which file is run, so an override that only the tests honoured would leave
  // the tests exercising one binary and the package another. The two used to
  // be separate searches that could disagree, and the only thing that made
  // them agree was that neither was set.
  //
  // A name that is set but wrong is stopped on rather than fallen through
  // from. Silently using a different binary is the one outcome nobody could
  // debug from the result, because everything would pass and none of it would
  // be about the binary that was asked for.
  const override = process.env.GUCHHO_BINARY;
  if (override) {
    if (!fs.existsSync(override)) {
      throw new Error(
        `GUCHHO_BINARY is set to "${override}", which is not a file. ` +
          `Point it at a Guchho binary, or unset it to use the one that ships with the package.`
      );
    }
    return override;
  }

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
  const buildDir = path.join(projectRoot, "build");

  // The binary install.js copied next to the shim, if one is there. Ahead of
  // anything in build/, because that is the one a published install put here.
  const installed = path.join(__dirname, "..", "bin", binaryName);
  if (fs.existsSync(installed)) {
    return installed;
  }

  // Fall back to something in the build tree, for running against a checkout.
  // Where more than one build exists, the most recently written one wins. A
  // repository built for two toolchains has more than one, and taking them in
  // directory order means a tree rebuilt a minute ago is still served by a
  // binary from last month — which then fails in ways that look like a bug in
  // the code just changed, because it never ran.
  if (fs.existsSync(buildDir)) {
    const wanted = [process.platform === "win32" ? "win32" : process.platform, process.arch];
    const candidates = [];

    for (const entry of fs.readdirSync(buildDir, { withFileTypes: true })) {
      if (!entry.isDirectory() || !matchesPlatform(entry.name, wanted)) {
        continue;
      }
      // MSVC puts it one level deeper than the Ninja and Unix generators do,
      // and both are layouts this project produces.
      candidates.push(path.join(buildDir, entry.name, "bin", "Release", binaryName));
      candidates.push(path.join(buildDir, entry.name, "bin", binaryName));
    }

    const present = candidates.filter((candidate) => fs.existsSync(candidate));
    if (present.length > 0) {
      present.sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
      return present[0];
    }
  }

  throw new Error(
    `Could not find guchho binary for ${platformKey}.\n` +
      `Please install the platform-specific package:\n` +
      `  npm install ${platformPkg || `@guchho/${platformKey}`}\n` +
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
