// BuildContext: a build set up once and repeated.
//
// The context is where the engine's caches live, which is the entire reason the
// API has one: the second build of a file that changed is cheaper than the
// first because the reading, resolving and parsing from the last one is still
// there. What is worth pinning down here is the lifecycle:
//
//   - rebuild() builds again with the options the context was made with, and
//     sees changes to the files;
//   - dispose() releases the context;
//   - a disposed context refuses further use, and says so rather than building
//     something on state the service has already let go of;
//   - dispose() is safe to call twice, because it is reached from clean-up
//     paths as often as from a caller.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");
const { cleanup, skipWithoutBinary } = require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };

// The engine dead-codes a module with nothing observable in it, so the entries
// have to make something observable to exist in the output at all — which is
// also the honest project shape.
function makeProject(initial = "const answer = 42;\nconsole.log(answer);\n") {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "guchho-context-test-"));
    fs.mkdirSync(path.join(root, "src"), { recursive: true });
    fs.writeFileSync(path.join(root, "src", "main.js"), initial);
    return root;
}

describe("context()", () => {
    it("establishes a context that can build", needsBinary, async () => {
        const root = makeProject();

        const ctx = await guchho.context({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            write: false,
        });

        const result = await ctx.rebuild();
        assert.deepEqual(result.errors, []);
        assert.equal(result.outputFiles[0].text.includes("42"), true);
        await ctx.dispose();
    });

    it("sees changes between rebuilds", needsBinary, async () => {
        const root = makeProject('console.log("first-value");\n');
        const ctx = await guchho.context({
            absWorkingDir: root,
            entryPoints: ["src/main.js"],
            write: false,
        });

        const before = await ctx.rebuild();
        assert.ok(before.outputFiles[0].text.includes("first-value"));

        fs.writeFileSync(path.join(root, "src", "main.js"), 'console.log("second-value");\n');
        const afterRebuild = await ctx.rebuild();

        assert.ok(afterRebuild.outputFiles[0].text.includes("second-value"));
        assert.ok(!afterRebuild.outputFiles[0].text.includes("first-value"));

        await ctx.dispose();
    });
});

describe("dispose", () => {
    it("is safe to call twice", needsBinary, async () => {
        const ctx = await guchho.context({
            absWorkingDir: makeProject(),
            entryPoints: ["src/main.js"],
            write: false,
        });

        await ctx.dispose();
        await ctx.dispose();
    });

    it("stops a context from being used again, and says so", needsBinary, async () => {
        const ctx = await guchho.context({
            absWorkingDir: makeProject(),
            entryPoints: ["src/main.js"],
            write: false,
        });

        await ctx.dispose();

        await assert.rejects(() => ctx.rebuild(), /disposed/);
        await assert.rejects(() => ctx.watch(), /disposed/);
        await assert.rejects(() => ctx.serve(), /disposed/);
    });
});