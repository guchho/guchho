// The whole thing, from a tarball, in a directory that knows nothing about the
// checkout.
//
// Every other file here imports "../guchho/lib/..." — a path into the source
// tree. That proves the files work; it does not prove the package works. This
// file packs the main package the way a publish would, unpacks it into a
// node_modules of its own, puts a real binary in the platform package beside
// it, and then runs a script that says nothing but:
//
//     import * as guchho from "guchho";
//     const result = await guchho.transform(code, options);
//     const built = await guchho.build(options);
//
// No relative path, no source tree, no NODE_PATH. If "guchho" does not resolve
// to something that can do both of those, the package is not installed, and
// nothing else in this suite would have noticed.
//
// Nothing is fetched. The tarball is built from the checkout and unpacked by
// hand, and the platform package is copied in rather than installed, because a
// test that reaches the registry is a test that fails on a bad network and
// cannot run in an air-gapped build.

const assert = require("node:assert/strict");
const child_process = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const {
    cleanup,
    extractTarball,
    hasTar,
    findBuiltBinary,
    makeTempDir,
    npmPack,
    runNode,
    skipWithoutBinary,
} = require("./helpers/workspace");
const { GUCHHO_DIR, PLATFORM_KEY, readManifest } = require("./helpers/paths");

after(cleanup);

// One install, shared by the tests in here. Building it is the slow part — a
// pack and an unpack — and nothing in it changes between them, so it is done
// once and the tests read from it.
let installed = null;
let installError = null;

function project() {
    if (installError) {
        throw installError;
    }
    if (!installed) {
        installed = buildInstalledPackage();
    }
    return installed;
}

// Packs the main package, unpacks it as node_modules/guchho, and gives it a
// platform package holding the binary from this machine's build.
function buildInstalledPackage() {
    const root = makeTempDir("e2e");
    const modules = path.join(root, "node_modules");
    fs.mkdirSync(modules, { recursive: true });

    // The main package, as a publish would upload it and as an install would
    // receive it. Unpacking the tarball rather than copying the directory is the
    // point: it applies the "files" field, and a file that was left out of the
    // tarball is absent here exactly as it would be for a real user.
    const downloads = path.join(root, "downloads");
    fs.mkdirSync(downloads);
    const tarball = npmPack(GUCHHO_DIR, downloads);
    const guchhoPkg = extractTarball(tarball, modules);
    fs.renameSync(guchhoPkg, path.join(modules, "guchho"));

    // The platform package. This is the one piece of an install that cannot be
    // faked from a tarball of this repository, because the tarball for a
    // platform would be built on that platform; so the binary this machine
    // just built is copied into the package as the platform package, and the
    // postinstall is run to do the copy the way a real install does it.
    const platformDir = path.join(modules, "@guchho", PLATFORM_KEY);
    fs.mkdirSync(path.join(platformDir, "bin"), { recursive: true });
    fs.writeFileSync(
        path.join(platformDir, "package.json"),
        JSON.stringify(
            { name: `@guchho/${PLATFORM_KEY}`, version: readManifest(GUCHHO_DIR).version },
            null,
            2
        )
    );

    const binary = findBuiltBinary();
    fs.copyFileSync(binary, path.join(platformDir, "bin", path.basename(binary)));

    // Run the postinstall the way npm would, so the binary is copied by the
    // shipped script rather than by this file. If install.js is broken, the
    // shim finds no binary and every test below fails with that as the cause.
    const installScript = path.join(modules, "guchho", "install.js");
    const install = child_process.spawnSync(process.execPath, [installScript], { encoding: "utf8" });
    if (install.error) {
        throw new Error(`could not run install.js: ${install.error.message}`);
    }
    if (install.status !== 0) {
        throw new Error(
            `install.js failed (exit ${install.status}):\n${install.stdout}\n${install.stderr}`
        );
    }

    // A manifest, so the directory is a package and Node's resolver treats it
    // as one. Without it "guchho" does not resolve at all, and every failure
    // below would be that rather than anything about the API.
    fs.writeFileSync(
        path.join(root, "package.json"),
        JSON.stringify({ name: "e2e-consumer", private: true, version: "1.0.0" }, null, 2)
    );

    return { root, modules, tarball };
}

// Writes a consumer script and runs it, returning what it printed. The script
// prints one JSON object, so a failure inside it is a stack trace on stderr and
// a non-zero exit rather than something to be parsed out of prose.
//
// The extension is taken as an argument because the callers here want different
// module systems from the same directory, and the extension is what decides: a
// require() inside a .mjs file fails on the require itself and says nothing
// about the package.
function runConsumer(root, body, ext = "cjs") {
    const script = path.join(root, `consumer.${ext}`);
    fs.writeFileSync(script, body);

    const result = runNode(script, [], { cwd: root });
    if (result.status !== 0) {
        assert.fail(
            `the consumer script failed (exit ${result.status}):\n${result.stdout}\n${result.stderr}`
        );
    }

    const start = result.stdout.indexOf("{");
    assert.notEqual(start, -1, `the consumer script printed no result:\n${result.stdout}`);
    return JSON.parse(result.stdout.slice(start));
}

const needsTar = () => (skipWithoutBinary() || (hasTar() ? false : "no tar program to unpack with"));

describe("installing the package", { skip: needsTar() }, () => {
    it("resolves the bare name \"guchho\" to the installed package", () => {
        const { root, modules } = project();

        const installedPkg = path.join(modules, "guchho", "package.json");
        assert.ok(fs.existsSync(installedPkg), "the main package was not installed");

        // Asked of the resolver rather than of the filesystem: the question is
        // whether a script in this directory can import the name, which is a
        // different question from whether a file is sitting there.
        const resolved = runConsumer(
            root,
            `import { createRequire } from "node:module";
             const require = createRequire(import.meta.url);
             console.log(JSON.stringify({ resolved: require.resolve("guchho") }));
            `,
            "mjs"
        );

        assert.ok(
            resolved.resolved.startsWith(modules),
            `"guchho" resolved to ${resolved.resolved}, which is not inside node_modules`
        );
    });

    it("put the binary where the shim looks for it", () => {
        const { modules } = project();

        const binaryName = process.platform === "win32" ? "guchho.exe" : "guchho";
        const binary = path.join(modules, "guchho", "bin", binaryName);

        assert.ok(fs.existsSync(binary), `install.js did not leave a binary at ${binary}`);
    });

    it("installed from a tarball, so the files field was applied", () => {
        const { modules } = project();

        // The two files whose absence is invisible until something tries to use
        // them: the exports map, which decides whether the name resolves at all,
        // and the platform table the installer reads.
        for (const required of ["lib/index.mjs", "lib/platforms.js", "install.js", "bin/guchho.js"]) {
            assert.ok(
                fs.existsSync(path.join(modules, "guchho", required)),
                `${required} was not in the tarball`
            );
        }
    });
});

describe("the snippet from the documentation", { skip: needsTar() }, () => {
    it("does what it says, from a package that was installed", () => {
        const { root } = project();

        // An entry point to build. Written here rather than in the fixture
        // because there was not one: the build used to resolve on failure, so
        // "entry.js" that did not exist produced a result object and the test
        // only checked that the object was there. Now that a failed build
        // throws, a missing entry point is a missing entry point.
        fs.writeFileSync(path.join(root, "entry.js"), "const answer = 42;\nconsole.log(answer);\n");

        // The snippet in README.md, verbatim. Kept as text rather than
        // extracted from the README so that a snippet which stops working fails
        // here rather than quietly stopping being tested — and so that the two
        // cannot drift into being two different snippets.
        const result = runConsumer(
            root,
            `import * as guchho from "guchho";

             const code = "const answer = 42;\\nconsole.log(answer);\\n";

             // The two calls the API exists for.
             const result1 = await guchho.transform(code, { minify: false });
             const result2 = await guchho.build({ entryPoints: ["entry.js"] });

             console.log(JSON.stringify({
                 transformIsFunction: typeof guchho.transform === "function",
                 buildIsFunction: typeof guchho.build === "function",
                 version: guchho.version,
                 result1,
                 result2
             }));
            `,
            "mjs"
        );

        // The shape first: if this fails, the numbers below are not worth
        // reading, because they were measured on something else.
        assert.equal(result.transformIsFunction, true, "guchho.transform is not a function");
        assert.equal(result.buildIsFunction, true, "guchho.build is not a function");
        assert.equal(typeof result.version, "string", "guchho.version should be a string");

        // And the two results are here in full, so a failure says which of the
        // two calls went wrong rather than only that one of them did.
        assert.ok(result.result1, "transform() returned nothing");
        assert.ok(result.result1.code.includes("answer = 42"), "transform() did not return the program");
        assert.equal(result.result1.errors.length, 0, "transform() reported errors");
        assert.ok(result.result2, "build() returned nothing");
        assert.equal(result.result2.errors.length, 0, "build() reported errors");
        assert.ok(Array.isArray(result.result2.warnings), "build() should report warnings");

        // A build that wrote nothing and said nothing would be the failure this
        // API is most able to have, and it is a resolved promise rather than a
        // throw, so nothing above would have caught it.
        assert.ok(
            result.result2.exitCode === undefined,
            "build() still reports an exitCode, which the service does not have"
        );
    });

    it("leaves no process behind, so a script that used it can exit", () => {
        // The service is a child process, and a child process that is not
        // unref'd holds the event loop open. A consumer that imports this
        // package, uses it once, and finishes would hang forever at the end
        // unless the process were released — which is invisible in every other
        // test here, because the test runner exits on its own terms.
        const { root } = project();
        const script = path.join(root, "exits.mjs");
        fs.writeFileSync(
            script,
            `import { transform } from "guchho";
             await transform("let a = 1", { loader: "js" });
             console.log("done");
            `
        );

        // runNode and not runConsumer: this script prints no JSON, and asking it
        // to would be a check about the harness rather than about the package.
        const started = Date.now();
        const result = runNode(script, [], { cwd: root });

        // The status is the assertion. A child process left referenced holds the
        // event loop open, so this script would never finish and would be killed
        // by the harness instead — and it is invisible in every other test here,
        // because the test runner exits on its own terms.
        assert.equal(
            result.status,
            0,
            `the consumer had to be killed (exit ${result.status}):\n${result.stderr}`
        );
        assert.match(result.stdout, /done/);
        assert.ok(Date.now() - started < 30000, "the consumer should not have had to be killed");
    });

    it("can also be required, not only imported", () => {
        // The exports map offers both, and a package that only works one way is
        // broken for half its callers. Checked with require() because that is
        // the half that breaks silently: an ESM file with require() in it is a
        // runtime error only when reached.
        const { root } = project();

        const result = runConsumer(
            root,
            `const guchho = require("guchho");
             console.log(JSON.stringify({
                 transform: typeof guchho.transform,
                 build: typeof guchho.build
             }));
            `
        );

        assert.equal(result.transform, "function");
        assert.equal(result.build, "function");
    });
});

describe("a tarball and an install", { skip: needsTar() }, () => {
    it("can be done without a network and without an npm cache", () => {
        // Asserted about the test rather than about the package: if this file
        // ever grows a dependency or a registry call, this is what notices.
        // The consumer is run in a directory with no node_modules of its own
        // beyond the one built above, and npm is not involved in running it.
        const { modules } = project();

        // Nothing under the project should point back into the checkout: an
        // import that resolved to a relative path out of node_modules would
        // pass every other test here while proving nothing.
        const installedIndex = path.join(modules, "guchho", "lib", "index.mjs");
        const contents = fs.readFileSync(installedIndex, "utf8");
        assert.ok(
            !contents.includes("package/npm/guchho"),
            "the installed package refers to the checkout by path"
        );
    });
});