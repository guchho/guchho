// guchho.lint(), reached through the package entry and answered by the service.
//
// The validation tests need no binary: every one of them is refused before a
// request is sent, which is the point of validating in JavaScript rather than
// letting the engine explain a malformed option. The rest run the real engine
// and check that what it found is what the result says, including the one
// option the engine cannot answer itself: maxWarningsExceeded.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");
const { cleanup, makeTempDir, skipWithoutBinary } = require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };

function makeProject(files = {}) {
    const root = makeTempDir("lint");
    const sources = { "src/a.js": "missing();\n", ...files };

    for (const [relative, contents] of Object.entries(sources)) {
        const file = path.join(root, relative);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, contents);
    }

    fs.writeFileSync(path.join(root, "guchho.lint.config.json"), "{}\n");
    return root;
}

describe("the module's shape", () => {
    it("has lint as a named binding", () => {
        assert.equal(typeof guchho.lint, "function");
    });
});

describe("lint() options", () => {
    it("refuses an options value that is not an object", async () => {
        await assert.rejects(guchho.lint(null), TypeError);
        await assert.rejects(guchho.lint([]), TypeError);
        await assert.rejects(guchho.lint("src/a.js"), TypeError);
    });

    it("refuses an empty file list, which would lint nothing by accident", async () => {
        await assert.rejects(
            guchho.lint({ files: [] }),
            /files must not be empty/
        );
    });

    it("refuses file entries that are not non-empty strings", async () => {
        await assert.rejects(guchho.lint({ files: [""] }), TypeError);
        await assert.rejects(guchho.lint({ files: [1] }), TypeError);
    });

    it("refuses a rule level it does not publish", async () => {
        await assert.rejects(
            guchho.lint({ rules: { "no-undef": "loud" } }),
            /must be "off", "warn", "warning", or "error"/
        );
        await assert.rejects(guchho.lint({ rules: { "no-undef": 2 } }), TypeError);
        await assert.rejects(guchho.lint({ rules: [] }), TypeError);
    });

    it("refuses a maxWarnings that is not a non-negative whole number", async () => {
        for (const value of [-1, 1.5, Number.NaN, "1"]) {
            await assert.rejects(
                guchho.lint({ maxWarnings: value }),
                /maxWarnings must be a non-negative integer/
            );
        }
    });

    it("refuses fix and fixDryRun together", async () => {
        await assert.rejects(
            guchho.lint({ fix: true, fixDryRun: true }),
            /cannot both be true/
        );
    });

    it("refuses an empty or non-string config path", async () => {
        await assert.rejects(guchho.lint({ config: "" }), TypeError);
        await assert.rejects(guchho.lint({ config: 1 }), TypeError);
    });
});

describe("lint()", () => {
    it("returns the findings as data, with paths only on the flat list", needsBinary, async () => {
        const root = makeProject();

        const result = await guchho.lint({
            absWorkingDir: root,
            files: ["src/a.js"],
        });

        assert.equal(result.errorCount, 1);
        assert.equal(result.warningCount, 0);
        assert.equal(result.fixCount, 0);
        assert.equal(result.filesWithProblems, 1);
        assert.equal(result.maxWarningsExceeded, false);

        assert.equal(result.files.length, 1);
        const file = result.files[0];
        assert.equal(file.path, "src/a.js");
        assert.equal(file.errorCount, 1);
        assert.equal(file.warningCount, 0);
        assert.equal(file.messages.length, 1);
        assert.equal("path" in file.messages[0], false);

        const message = result.messages[0];
        assert.equal(message.path, "src/a.js");
        assert.equal(message.ruleId, "no-undef");
        assert.equal(message.severity, "error");
        assert.equal(message.line, 1);
        assert.equal(message.column, 1);
        assert.equal(message.syntax, false);
        assert.equal(message.fixCount, 0);
    });

    it("lints the project's files when files is omitted", needsBinary, async () => {
        const root = makeProject();

        const result = await guchho.lint({ absWorkingDir: root });

        assert.deepEqual(result.files.map((file) => file.path), ["src/a.js"]);
    });

    it("applies rule overrides without changing the config file", needsBinary, async () => {
        const root = makeProject();

        const result = await guchho.lint({
            absWorkingDir: root,
            files: ["src/a.js"],
            rules: { "no-undef": "off" },
        });

        assert.equal(result.errorCount, 0);
        assert.deepEqual(result.messages, []);
        assert.equal(
            fs.readFileSync(path.join(root, "guchho.lint.config.json"), "utf8"),
            "{}\n"
        );
    });

    it("computes maxWarningsExceeded at the threshold", needsBinary, async () => {
        const root = makeProject({ "src/a.js": "const unused = 1;\n" });

        const atThreshold = await guchho.lint({
            absWorkingDir: root,
            files: ["src/a.js"],
            maxWarnings: 1,
        });
        assert.equal(atThreshold.warningCount, 1);
        assert.equal(atThreshold.maxWarningsExceeded, false);

        const overThreshold = await guchho.lint({
            absWorkingDir: root,
            files: ["src/a.js"],
            maxWarnings: 0,
        });
        assert.equal(overThreshold.maxWarningsExceeded, true);
    });

    it("reports a dry-run fix without writing it", needsBinary, async () => {
        const root = makeProject({ "src/a.js": "debugger;\n" });

        const result = await guchho.lint({
            absWorkingDir: root,
            files: ["src/a.js"],
            fixDryRun: true,
        });

        assert.equal(result.fixCount, 1);
        assert.equal(
            fs.readFileSync(path.join(root, "src/a.js"), "utf8"),
            "debugger;\n"
        );
    });

    it("writes the fixes fix asked for", needsBinary, async () => {
        const root = makeProject({ "src/a.js": "debugger;\n" });

        const result = await guchho.lint({
            absWorkingDir: root,
            files: ["src/a.js"],
            fix: true,
        });

        assert.equal(result.fixCount, 1);
        assert.equal(fs.readFileSync(path.join(root, "src/a.js"), "utf8").trim(), "");
    });

    it("reports an unknown rule as a BuildFailure", needsBinary, async () => {
        const root = makeProject();

        await assert.rejects(
            guchho.lint({
                absWorkingDir: root,
                files: ["src/a.js"],
                rules: { "no-such-rule": "error" },
            }),
            guchho.BuildFailure
        );
    });
});
