// Finding the binary, which is the one thing the package cannot do without.
//
// lib/main.js walks two places: the platform package for the machine, and then
// the package's own bin directory plus the build tree it might be running from.
// The order matters. The platform package is what an installed copy has and
// the build tree is what a checkout has, and a checkout that has both must use
// the build tree — otherwise a change to the binary would be invisible until
// the next publish.
//
// process.platform and process.arch are read inside the function rather than
// at load time, so they can be replaced here. That is what lets a machine be
// pretended into existence for a test: no chroot, no container, and the same
// function the real machine runs.

const assert = require("node:assert/strict");
const child_process = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const { cleanup, stageNodeModules, fakeBinary, findBuiltBinary, skipWithoutBinary } = require("./helpers/workspace");
const { BINARY_NAME } = require("./helpers/paths");

// spawnSync rather than the asynchronous spawn: the shim's whole job is to end
// the process, and a test that awaited a promise would be waiting for something
// the shim never offers.
const { spawnSync } = child_process;

after(cleanup);

// Swaps process.platform and process.arch for the length of a call, and puts
// them back afterwards. Node defines both as writable-through-defineProperty
// on the process object for exactly this sort of test.
//
// Loads a copy of lib/main.js out of a staged tree, so that the module's idea
// of where it is — which decides the build-tree fallback — is the staged one
// and not the real repository. Requiring the same file from two directories
// gives two separate module instances, which is what keeps one test's staging
// from deciding another's answer.
function loadMain(staged) {
    const entry = path.join(staged.guchhoPkg, "lib", "main.js");
    delete require.cache[require.resolve(entry)];
    return require(entry);
}

describe("getPlatformKey", () => {
    it("is the platform and the architecture, joined the way the table spells them", () => {
        const { getPlatformKey } = loadMain(stageNodeModules());

        assert.equal(getPlatformKey(), `${process.platform}-${process.arch}`);
        // Digits on both sides of the dash: Node's platforms include "win32"
        // and "sunos", and its architectures include "x64" and "arm64".
        assert.match(getPlatformKey(), /^[a-z0-9]+-[a-z0-9]+$/);
    });
});

describe("getBinaryPath", () => {
    it("prefers the platform package's binary", () => {
        const staged = stageNodeModules();
        const { project, guchhoPkg } = staged;
        const { getBinaryPath } = loadMain(staged);

        const resolved = getBinaryPath();
        assert.ok(
            resolved.startsWith(path.join(project, "node_modules", "@guchho")),
            `expected a binary inside the platform package, got ${resolved}`
        );
        assert.equal(path.basename(resolved), BINARY_NAME);
        assert.ok(fs.existsSync(resolved));
        assert.ok(resolved.startsWith(path.dirname(guchhoPkg)), resolved);
    });

    it("falls back to the package's own bin when no platform package is installed", () => {
        // The optional dependencies are allowed not to be there: a platform
        // that was skipped, or an install that was told to omit them. What the
        // package must not do is throw.
        const staged = stageNodeModules({ withPlatform: false });
        const { project, guchhoPkg } = staged;
        const { getBinaryPath } = loadMain(staged);

        const local = path.join(guchhoPkg, "bin", BINARY_NAME);
        fs.writeFileSync(local, fakeBinary());

        assert.equal(getBinaryPath(), local);
    });

    it("falls back to a build tree when the package has no binary of its own", () => {
        // A checkout running from source: no platform package, and the binary
        // is in build/<preset>/bin. The staged tree is one level below its
        // temporary root so that the four-directory climb in lib/main.js lands
        // there, and a build directory is put there rather than relying on the
        // real one.
        const staged = stageNodeModules({ withPlatform: false });
        const { root, guchhoPkg } = staged;
        const { getBinaryPath } = loadMain(staged);

        const buildDir = path.join(root, "build", `release-${process.platform}-${process.arch}-test`);
        fs.mkdirSync(path.join(buildDir, "bin"), { recursive: true });
        const built = path.join(buildDir, "bin", BINARY_NAME);
        fs.writeFileSync(built, fakeBinary());

        assert.equal(getBinaryPath(), built);
        assert.ok(!getBinaryPath().startsWith(guchhoPkg), "the build tree was skipped for the local copy");
    });

    it("prefers the build tree over a platform package that has no binary", () => {
        // A checkout can have a platform package directory with nothing in its
        // bin, which is what a half-finished build-npm.sh leaves behind. The
        // build tree is the answer then, not a throw.
        const staged = stageNodeModules();
        const { root, project } = staged;
        const platformBin = path.join(
            project, "node_modules", "@guchho", `${process.platform}-${process.arch}`, "bin", BINARY_NAME
        );
        fs.rmSync(platformBin);

        const buildDir = path.join(root, "build", `release-${process.platform}-${process.arch}-test`);
        fs.mkdirSync(path.join(buildDir, "bin"), { recursive: true });
        const built = path.join(buildDir, "bin", BINARY_NAME);
        fs.writeFileSync(built, fakeBinary());

        const { getBinaryPath } = loadMain(staged);
        assert.equal(getBinaryPath(), built);
    });

    it("throws with a message naming the package to install, on an unknown platform", () => {
        // A platform with no pre-built binary is a supported situation — the
        // table is not the whole set of platforms Node knows — and the error
        // is the only thing standing between that and a bare "not found".
        const staged = stageNodeModules();
        const { root, project } = staged;
        const { getBinaryPath } = loadMain(staged);

        assert.throws(
            () => onUnknownPlatform(getBinaryPath),
            /Could not find guchho binary for madeup-x64/,
            "the error should name the platform it could not serve"
        );

        // And the platform it names has to be one a person could install.
        assert.throws(
            () => onUnknownPlatform(getBinaryPath),
            /npm install @guchho\/madeup-x64/,
            "the error should say what to install"
        );
    });
});

// Runs a function while the process claims to be a platform the table has no
// entry for. Synchronous because that is what getBinaryPath is: pretending to
// be another platform should not have to make the test asynchronous, and an
// async wrapper around a sync function is two more ways to leak a swap that was
// not put back.
function onUnknownPlatform(fn) {
    const realPlatform = Object.getOwnPropertyDescriptor(process, "platform");
    const realArch = Object.getOwnPropertyDescriptor(process, "arch");
    Object.defineProperty(process, "platform", { value: "madeup", configurable: true });
    Object.defineProperty(process, "arch", { value: "x64", configurable: true });
    try {
        return fn();
    } finally {
        Object.defineProperty(process, "platform", realPlatform);
        Object.defineProperty(process, "arch", realArch);
    }
}

describe("spawnBinary", () => {
    // The remaining cases need a program that really runs, because what is
    // being checked is the exit status coming back. A stand-in cannot stand in
    // for that on every platform at once — a shell script is not a program on
    // Windows — so these use the real binary and skip without it.
    it("resolves with the exit code when the run succeeds", { skip: skipWithoutBinary() }, async () => {
        const staged = stageWithRealBinary();
        const { spawnBinary } = loadMain(staged);

        assert.equal(await spawnBinary(["--version"], { stdio: "ignore" }), 0);
    });

    it("rejects with the status when the binary exits non-zero", { skip: skipWithoutBinary() }, async () => {
        // A caller that cannot tell a non-zero exit from a crash is a caller
        // that reports the wrong thing: one is a rejected input and the other
        // is a broken install. The failure used here is an entry point that is
        // not there, rather than an unknown flag — the command line accepts an
        // unknown flag and exits 0, which is a separate decision.
        const staged = stageWithRealBinary();
        const { root } = staged;
        const { spawnBinary } = loadMain(staged);

        await assert.rejects(
            () => spawnBinary(["build", "no-such-entry.html"], { stdio: "ignore", cwd: root }),
            (err) => {
                assert.match(err.message, /exited with code \d+/);
                assert.equal(typeof err.status, "number");
                assert.notEqual(err.status, 0);
                return true;
            }
        );
    });
});

// A staged tree carrying the real built binary, both in the platform package
// and in the package's own bin, with the platform package removed afterwards so
// that the resolution has exactly one answer to give.
function stageWithRealBinary() {
    const staged = stageNodeModules({ realBinary: true, withPlatform: false });
    fs.copyFileSync(findBuiltBinary(), path.join(staged.guchhoPkg, "bin", BINARY_NAME));
    return staged;
}

// The shim is what npm puts on the PATH, so it is the file a person actually
// types. There is one thing about it worth a test, and it is the one thing
// nobody would notice breaking: it hands the binary's exit status back as its
// own. A shim that returned 0 whatever the binary said would make "guchho build"
// succeed in a shell, in a CI step and in a package.json script while the build
// had failed — and the failure would surface somewhere else entirely, as a
// missing file rather than as the error that was actually reported.
//
// So this is a test about the status, not about the output. What the shim prints
// is the binary's, passed through by inheritance, and there is nothing here that
// could change that.
describe("the bin shim", () => {
    it("exits with the status the binary gave it", { skip: skipWithoutBinary() }, () => {
        // A build of an entry that is not there is the failure used: the binary
        // reads the flag, rejects the path, and exits non-zero. An unknown flag
        // would not do — the command line has a rule of its own about those,
        // and the rule is not the same on every command.
        const staged = stageWithRealBinary();
        const shim = path.join(staged.guchhoPkg, "bin", "guchho.js");
        const missing = path.join(staged.project, "no-such-entry.html");

        const failed = spawnSync(process.execPath, [shim, "build", missing], {
            encoding: "utf8",
            cwd: staged.project,
        });

        assert.notEqual(
            failed.status,
            0,
            `expected a non-zero status, got ${failed.status}\n${failed.stderr}`
        );
    });

    it("exits 0 when the binary succeeded", { skip: skipWithoutBinary() }, () => {
        const staged = stageWithRealBinary();
        const shim = path.join(staged.guchhoPkg, "bin", "guchho.js");

        const ran = spawnSync(process.execPath, [shim, "--version"], {
            encoding: "utf8",
            cwd: staged.project,
        });

        assert.equal(ran.status, 0, `${shim} --version exited ${ran.status}\n${ran.stderr}`);
    });
});

