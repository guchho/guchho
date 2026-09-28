"use strict";

// Temporary trees, stand-in binaries, and the question of whether a real one
// exists at all.
//
// Two rules shape this file. First, nothing here touches the real packaging
// directory: every test that writes anything writes into a temporary directory
// that is removed afterwards, because a test that packs the package and then
// leaves a stray file in it is a test that breaks the next publish. Second,
// every test that needs a real binary says so when there is not one, rather
// than failing: the suite is meant to be runnable on a machine that has not
// built anything yet.

const child_process = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

const { REPO_ROOT, GUCHHO_DIR, BINARY_NAME, PLATFORM_KEY } = require("./paths");

// Set to "1" to make a missing binary a failure instead of a skip. CI sets it,
// so that a green run there means the API was really exercised; a developer
// running the suite on a fresh checkout does not, and gets skips instead.
const REQUIRE_BINARY = process.env.GUCHHO_REQUIRE_BINARY === "1";

// Where a locally built binary might be, and where it might not.
//
// Only "release-" and "debug-" directories are looked at, and the "ci" one is
// excluded by name. That is not caution for its own sake: build/ci holds a
// stub that prints "Hello from Guchho!" and exits, which would satisfy a
// file-exists check and then fail every test that tried to run it. A search
// that found the stub would be worse than no search at all, because the failure
// would be somewhere else entirely.
const BINARY_SEARCH_DIRS = ["release-", "debug-"];

let tempCounter = 0;

// Creates a temporary directory and hands back its path together with a
// function that removes it. Registered for cleanup on the way out, so a test
// that throws still leaves nothing behind.
function makeTempDir(tag = "pkg") {
    tempCounter += 1;
    const dir = fs.mkdtempSync(
        path.join(os.tmpdir(), `guchho-npm-${tag}-${process.pid}-${tempCounter}-`)
    );

    tempDirs.add(dir);
    return dir;
}

const tempDirs = new Set();

// Removes every directory this file made. Called by the runner and by the test
// framework's teardown; safe to call more than once.
function cleanup() {
    for (const dir of tempDirs) {
        try {
            fs.rmSync(dir, { recursive: true, force: true });
        } catch {
            // A directory still held open by a child that outlived its test is
            // not worth failing the run over, and on Windows the removal is the
            // thing that would be refused. The directory is in the temporary
            // directory, so the operating system will have it eventually.
        }
    }
    tempDirs.clear();
}

// The bytes used to stand in for a native binary.
//
// A real executable is not needed to test the packaging: what is being checked
// is which file gets copied where, and whether the command that starts it
// exists. What must not happen is a test that runs this and is then reporting a
// result produced by something that is not Guchho, so the stand-in is plain
// text that is obviously not a program, and every test that would execute it
// uses the real binary instead.
function fakeBinary() {
    return `guchho test stand-in for ${PLATFORM_KEY} — not an executable\n`;
}

// Copies a directory tree. Used to build a node_modules that looks installed
// without npm having installed anything, which is what keeps the suite offline.
function copyDir(from, to) {
    fs.mkdirSync(to, { recursive: true });
    for (const entry of fs.readdirSync(from, { withFileTypes: true })) {
        const src = path.join(from, entry.name);
        const dst = path.join(to, entry.name);
        if (entry.isDirectory()) {
            copyDir(src, dst);
        } else if (entry.isFile()) {
            fs.copyFileSync(src, dst);
        }
    }
}

// Stages the main package and one platform package into a temporary
// node_modules, laid out the way npm would lay them out: the two as siblings,
// so that require("@guchho/<platform>/package.json") from inside the main
// package walks up one level and finds it. That resolution walk is exactly what
// install.js and lib/main.js depend on, so a layout that put the platform
// package somewhere else would test a shape npm never produces.
//
// The project sits one directory below the temporary root, which is not
// tidiness. lib/main.js works out where a build tree would be by climbing four
// directories above itself, and that only lands inside the temporary root at
// this depth — one level higher and it points at the shared temporary
// directory, where it would find whatever else happens to be there.
//
// The platform package's binary is a stand-in unless "realBinary" is true, in
// which case the local build is copied in. Returns the paths a test needs.
function stageNodeModules({ platformKey = PLATFORM_KEY, realBinary = false, withPlatform = true } = {}) {
    const root = makeTempDir("project");
    const project = path.join(root, "project");
    fs.mkdirSync(project, { recursive: true });

    const modules = path.join(project, "node_modules");
    const guchhoPkg = path.join(modules, "guchho");

    // The main package as npm would unpack it: only what the "files" field
    // lists, so a file that should not have shipped does not turn up here.
    for (const entry of ["bin", "lib"]) {
        copyDir(path.join(GUCHHO_DIR, entry), path.join(guchhoPkg, entry));
    }
    for (const file of ["install.js", "package.json"]) {
        fs.copyFileSync(path.join(GUCHHO_DIR, file), path.join(guchhoPkg, file));
    }

    if (withPlatform) {
        const platformPkg = path.join(modules, "@guchho", platformKey);
        fs.mkdirSync(path.join(platformPkg, "bin"), { recursive: true });
        fs.copyFileSync(
            path.join(GUCHHO_DIR, "..", "@guchho", platformKey, "package.json"),
            path.join(platformPkg, "package.json")
        );

        const contents = realBinary && findBuiltBinary()
            ? fs.readFileSync(findBuiltBinary())
            : fakeBinary();
        fs.writeFileSync(path.join(platformPkg, "bin", BINARY_NAME), contents);
    }

    return { root, project, modules, guchhoPkg };
}

// Finds a locally built binary, or returns null.
//
// GUCHHO_BINARY overrides the search, for a machine whose build lives somewhere
// the presets do not put it.
//
// Where several builds exist, the most recently written one wins. A repository
// that has been built for both MinGW and MSVC has more than one binary, and
// picking by directory order would report whichever was configured first — so a
// tree that had just been rebuilt would still be tested against a binary from
// last month, and the version test would fail for a reason that no longer
// applies. A stale binary that is the only one there is still found, which is
// the case worth failing on.
function findBuiltBinary() {
    const override = process.env.GUCHHO_BINARY;
    if (override) {
        return fs.existsSync(override) ? override : null;
    }

    const buildDir = path.join(REPO_ROOT, "build");
    if (!fs.existsSync(buildDir)) {
        return null;
    }

    const candidates = [];
    for (const entry of fs.readdirSync(buildDir, { withFileTypes: true })) {
        if (!entry.isDirectory()) {
            continue;
        }
        if (!BINARY_SEARCH_DIRS.some((prefix) => entry.name.startsWith(prefix))) {
            continue;
        }
        // Ninja and MSVC put the executable in different places, and both are
        // real layouts this project produces.
        candidates.push(path.join(buildDir, entry.name, "bin", BINARY_NAME));
        candidates.push(path.join(buildDir, entry.name, "bin", "Release", BINARY_NAME));
    }

    const present = candidates.filter((p) => fs.existsSync(p));
    if (present.length === 0) {
        return null;
    }

    present.sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs);
    return present[0];
}

// True when a real binary is available, which is the condition for the tests
// that actually run something.
function hasBinary() {
    return findBuiltBinary() !== null;
}

// Whether a test needing the binary should be skipped, and why. Meant to be
// handed straight to node:test's { skip } option, so the reason is recorded in
// the output rather than lost.
function skipWithoutBinary() {
    if (hasBinary()) {
        return false;
    }
    if (REQUIRE_BINARY) {
        // Deliberately not a skip: the run was asked to require a binary, and
        // quietly passing without one is the one outcome that would make the
        // setting a lie.
        throw new Error(
            "GUCHHO_REQUIRE_BINARY=1 but no built binary was found. " +
            "Build one (scripts/build-npm.sh) or set GUCHHO_BINARY."
        );
    }
    return "no built Guchho binary found; run scripts/build-npm.sh or set GUCHHO_BINARY";
}

// Runs a Node script in a staged tree and returns what it wrote. The suite
// spawns Node rather than calling install() directly, because install.js is a
// script that runs on require and postinstall runs it as a script: testing it
// any other way would not be testing the thing npm runs.
function runNode(scriptPath, args = [], options = {}) {
    return child_process.spawnSync(process.execPath, [scriptPath, ...args], {
        encoding: "utf8",
        ...options,
    });
}

// Runs "npm pack" in a directory and returns the parsed result. --dry-run
// writes nothing, so this is safe to run against the real packaging directory:
// it reports what a publish would contain without producing a tarball.
//
// On Windows npm is a .cmd script, and Node cannot execute one directly — the
// child process fails to start and comes back with a null status and no output,
// which reads as "npm said nothing" rather than "npm never ran". The shell is
// used there and only there, because a POSIX npm is an executable and does not
// want one.
function npmPackDryRun(dir) {
    const isWindows = process.platform === "win32";
    const result = child_process.spawnSync(
        isWindows ? "npm.cmd" : "npm",
        ["pack", "--dry-run", "--json"],
        { cwd: dir, encoding: "utf8", shell: isWindows }
    );

    if (result.error) {
        throw new Error(`could not run npm in ${dir}: ${result.error.message}`);
    }

    if (result.status !== 0) {
        throw new Error(
            `npm pack --dry-run failed in ${dir} (exit ${result.status}):\n${result.stderr}`
        );
    }

    // npm prints the JSON on stdout, but a notice about the tree going stale
    // can precede it, so the first "[" is where the document starts.
    const start = result.stdout.indexOf("[");
    if (start === -1) {
        throw new Error(`npm pack --dry-run printed no JSON in ${dir}:\n${result.stdout}`);
    }

    return JSON.parse(result.stdout.slice(start))[0];
}

// Runs a real "npm pack", writing the tarball into destDir, and returns its
// path. This is the difference between the dry run above and an install: the
// tarball is what a publish would upload and what "npm install" would fetch, so
// unpacking it is the only way to find out whether the package is usable from
// the outside rather than from inside the checkout.
function npmPack(dir, destDir) {
    const isWindows = process.platform === "win32";
    const result = child_process.spawnSync(
        isWindows ? "npm.cmd" : "npm",
        ["pack", "--pack-destination", destDir, "--json"],
        { cwd: dir, encoding: "utf8", shell: isWindows }
    );

    if (result.error) {
        throw new Error(`could not run npm in ${dir}: ${result.error.message}`);
    }
    if (result.status !== 0) {
        throw new Error(`npm pack failed in ${dir} (exit ${result.status}):\n${result.stderr}`);
    }

    const start = result.stdout.indexOf("[");
    if (start === -1) {
        throw new Error(`npm pack printed no JSON in ${dir}:\n${result.stdout}`);
    }

    return path.join(destDir, JSON.parse(result.stdout.slice(start))[0].filename);
}

// Whether a tar program is there to unpack with.
//
// Node has no built-in tar reader, and pulling in a dependency to unpack one
// archive would mean the packaging tests needed installing before they could
// check that installing works. The tar program is present on Windows 10 and
// later and on every Linux and macOS image these run on, so this asks rather
// than assumes, and the test that needs it is skipped rather than failed when
// the answer is no — a missing tar is a missing machine, not a broken package.
function hasTar() {
    // No shell: tar is a real executable everywhere these run, and a shell
    // would mangle the backslashes in any Windows path passed to it.
    const probe = child_process.spawnSync("tar", ["--version"], { encoding: "utf8" });
    return !probe.error && probe.status === 0;
}

// Unpacks a tarball into destDir and returns the single directory it contained.
//
// npm tarballs hold everything under "package/", so the interesting directory
// is whatever that was renamed to on the way in. Naming the outcome rather than
// assuming "package" is what makes this usable for a scoped package too.
// A path in the form tar accepts.
//
// Given a Windows path it will not: "tar -C C:\Users\..." fails with "Cannot
// open: No such file or directory" for a directory that is plainly there,
// because tar treats the backslashes as escape characters. Forward slashes work
// everywhere, including on Windows, so they are what it is given.
function toTarPath(target) {
    return target.split(path.sep).join("/");
}

function extractTarball(tarball, destDir) {
    // Run from the tarball's own directory and name it bare. Given a full
    // Windows path, tar reads the "C:" as the start of a remote host spec and
    // fails with "cannot connect to C:" — a message that says nothing about the
    // archive being fine.
    const result = child_process.spawnSync(
        "tar",
        ["-xzf", path.basename(tarball), "-C", toTarPath(destDir)],
        {
            cwd: path.dirname(tarball),
            encoding: "utf8",
        }
    );

    if (result.error) {
        throw new Error(`could not run tar: ${result.error.message}`);
    }
    if (result.status !== 0) {
        throw new Error(`tar could not unpack ${tarball} (exit ${result.status}):\n${result.stderr}`);
    }

    const entries = fs.readdirSync(destDir, { withFileTypes: true });
    if (entries.length !== 1 || !entries[0].isDirectory()) {
        throw new Error(
            `${path.basename(tarball)} unpacked to ${entries.length} entries in ${destDir}, expected one directory`
        );
    }

    return path.join(destDir, entries[0].name);
}

module.exports = {
    makeTempDir,
    cleanup,
    copyDir,
    stageNodeModules,
    findBuiltBinary,
    hasBinary,
    skipWithoutBinary,
    runNode,
    npmPackDryRun,
    npmPack,
    hasTar,
    extractTarball,
    fakeBinary,
    REQUIRE_BINARY,
};
