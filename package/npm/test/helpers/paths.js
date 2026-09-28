"use strict";

// Where everything in the packaging tree is, worked out from this file rather
// than from a path anybody typed.
//
// A test that hard-codes "../../.." works until the file is moved, and a test
// that depends on the working directory works only when npm happens to set it
// the way the author expected. Both are found by walking up from here until the
// marker turns up, so the tests can be run from anywhere.

const fs = require("fs");
const path = require("path");

// The repository root: the first ancestor of this file holding the packaging
// directory, which is the one thing every test in this suite is about.
const PACKAGES_DIR = (() => {
    let dir = __dirname;
    for (let i = 0; i < 10; i += 1) {
        if (fs.existsSync(path.join(dir, "package", "npm"))) {
            return path.join(dir, "package", "npm");
        }
        const parent = path.dirname(dir);
        if (parent === dir) {
            break;
        }
        dir = parent;
    }
    throw new Error(`could not find package/npm above ${__dirname}`);
})();

const REPO_ROOT = path.resolve(PACKAGES_DIR, "..", "..");

// The main package: the one named "guchho", holding the command line, the API
// and the installer, and depending on the platform packages for the binary.
const GUCHHO_DIR = path.join(PACKAGES_DIR, "guchho");

// The directory holding one subdirectory per platform package.
const PLATFORMS_DIR = path.join(PACKAGES_DIR, "@guchho");

// The name of the native binary on this machine, which is the only part of a
// package that differs by platform.
const BINARY_NAME = process.platform === "win32" ? "guchho.exe" : "guchho";

// The platform key this machine would install, e.g. "win32-x64".
const PLATFORM_KEY = `${process.platform}-${process.arch}`;

// Every platform package directory, named by its own key, sorted so that a
// failure reads the same on every machine.
function platformDirs() {
    return fs
        .readdirSync(PLATFORMS_DIR, { withFileTypes: true })
        .filter((e) => e.isDirectory())
        .map((e) => e.name)
        .sort();
}

// Reads a package manifest from a directory, or returns null when there is
// none. A missing manifest is not an error here: a platform package that has
// not been created yet is a thing a test may be checking for.
function readManifest(dir) {
    try {
        return JSON.parse(fs.readFileSync(path.join(dir, "package.json"), "utf8"));
    } catch {
        return null;
    }
}

module.exports = {
    PACKAGES_DIR,
    REPO_ROOT,
    GUCHHO_DIR,
    PLATFORMS_DIR,
    BINARY_NAME,
    PLATFORM_KEY,
    platformDirs,
    readManifest,
};
