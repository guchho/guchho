// install.js, which is what npm runs on its own after unpacking.
//
// It exists because the binary lives in a platform package and the command line
// has to end up somewhere the "bin" entry can point at. So the script's whole
// job is to find the right platform package and put its binary in the main
// package's own bin directory.
//
// It is run as a script here rather than imported, because that is how npm runs
// it and because it calls install() on require: importing it would either test
// something npm never does or start the real one against the real tree.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const { cleanup, runNode, stageNodeModules } = require("./helpers/workspace");
const { BINARY_NAME, PLATFORM_KEY } = require("./helpers/paths");

after(cleanup);

// Runs install.js inside a staged tree and returns what it said. The working
// directory is the project rather than the package, because the answer should
// not depend on where the script was invoked from.
function runInstall(staged) {
    return runNode(path.join(staged.guchhoPkg, "install.js"), [], { cwd: staged.project });
}

describe("install.js", () => {
    it("copies the platform package's binary into the main package", () => {
        const staged = stageNodeModules();
        const installed = path.join(staged.guchhoPkg, "bin", BINARY_NAME);

        assert.ok(!fs.existsSync(installed), "the binary should not be there before the install");

        const result = runInstall(staged);

        assert.equal(result.status, 0, `install.js failed: ${result.stderr}`);
        assert.ok(fs.existsSync(installed), "install.js did not put the binary in bin/");
        assert.match(result.stdout, /Installed binary for/);
    });

    it("copies the binary that is actually there, not an empty file", () => {
        // A copy that succeeds but writes nothing — a directory read as a file,
        // a truncated read — leaves an install that fails on first run with
        // nothing to point at.
        const staged = stageNodeModules();
        const source = path.join(
            staged.project, "node_modules", "@guchho", PLATFORM_KEY, "bin", BINARY_NAME
        );
        const expected = "a distinctive marker that only the source has\n";
        fs.writeFileSync(source, expected);

        runInstall(staged);

        const installed = path.join(staged.guchhoPkg, "bin", BINARY_NAME);
        assert.equal(fs.readFileSync(installed, "utf8"), expected);
        assert.ok(fs.statSync(installed).size > 0, "the installed binary is empty");
    });

    it("keeps the executable bit on a platform that has one", () => {
        // Windows has no executable bit and no need for one; everywhere else a
        // copied binary without it is a file the command line cannot run, and
        // the error is "permission denied" from something called node.
        if (process.platform === "win32") {
            return;
        }

        const staged = stageNodeModules();
        runInstall(staged);

        const installed = path.join(staged.guchhoPkg, "bin", BINARY_NAME);
        const mode = fs.statSync(installed).mode & 0o777;
        assert.equal(mode & 0o111, 0o111, `the installed binary is not executable (mode ${mode.toString(8)})`);
    });

    it("warns, and installs nothing, when the platform package is absent", () => {
        // The platform packages are optional dependencies, so being absent is a
        // normal outcome — a platform with no pre-built binary, or an install
        // run with --omit=optional. The install has to survive it.
        const staged = stageNodeModules({ withPlatform: false });

        const result = runInstall(staged);

        assert.equal(result.status, 0, `install.js should not fail the install: ${result.stderr}`);
        assert.match(result.stderr, /was not installed/);
        assert.match(result.stderr, new RegExp(`npm install @guchho/${PLATFORM_KEY}`), "it should say what to install");
        assert.ok(
            !fs.existsSync(path.join(staged.guchhoPkg, "bin", BINARY_NAME)),
            "nothing should have been installed"
        );
    });

    it("warns when the platform package is there but has no binary", () => {
        // A different failure with a different fix: the package installed, so
        // telling the user to install it again would be wrong.
        const staged = stageNodeModules();
        fs.rmSync(
            path.join(staged.project, "node_modules", "@guchho", PLATFORM_KEY, "bin", BINARY_NAME)
        );

        const result = runInstall(staged);

        assert.equal(result.status, 0);
        assert.match(result.stderr, /Binary not found/);
        assert.match(result.stderr, /github\.com\/guchho\/guchho\/issues/, "this one is a bug report, not a how-to");
        assert.ok(
            !fs.existsSync(path.join(staged.guchhoPkg, "bin", BINARY_NAME)),
            "nothing should have been installed"
        );
    });

    it("warns about a platform with no pre-built binary at all", () => {
        // The one case where the answer is "build it yourself", which is a
        // different sentence from either of the others. Reaching it means
        // standing on a platform the table has no entry for, so the process is
        // told it is on one before install.js is loaded — it reads
        // process.platform when it runs, not when it is parsed, so this is the
        // same value it would see on that machine.
        const staged = stageNodeModules();

        const driver = path.join(staged.project, "pretend.js");
        fs.writeFileSync(
            driver,
            [
                '"use strict";',
                'Object.defineProperty(process, "platform", { value: "madeup" });',
                'Object.defineProperty(process, "arch", { value: "x64" });',
                `require(${JSON.stringify(path.join(staged.guchhoPkg, "install.js"))});`,
                "",
            ].join("\n")
        );

        const result = runNode(driver, [], { cwd: staged.project });

        assert.equal(result.status, 0, "an unknown platform should warn, not fail the install");
        assert.match(result.stderr, /No pre-built binary available for madeup-x64/);
        assert.match(result.stderr, /build from source/i);
        assert.ok(
            !fs.existsSync(path.join(staged.guchhoPkg, "bin", BINARY_NAME)),
            "nothing should have been installed"
        );
    });

    it("leaves the shim next to the binary, because the bin entry names it", () => {
        // The copy goes into a directory the "bin" entry already points into.
        // If it went anywhere else, the shim would be a script with nothing to
        // run and the install would look successful.
        const staged = stageNodeModules();
        runInstall(staged);

        const binDir = path.join(staged.guchhoPkg, "bin");
        assert.ok(fs.existsSync(path.join(binDir, "guchho.js")), "the shim is missing");
        assert.ok(fs.existsSync(path.join(binDir, BINARY_NAME)), "the binary is missing");
    });

    it("is idempotent, because a second install must not fail on the first one's file", () => {
        // npm can run a postinstall more than once for the same tree — a
        // repair, a reinstall, a --force. A script that cannot cope leaves a
        // package that only works on a clean machine.
        const staged = stageNodeModules();

        const first = runInstall(staged);
        const second = runInstall(staged);

        assert.equal(first.status, 0);
        assert.equal(second.status, 0, `the second install failed: ${second.stderr}`);
        assert.match(second.stdout, /Installed binary for/);
    });
});

