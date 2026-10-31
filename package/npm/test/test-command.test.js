const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const { after, describe, it } = require("node:test");

const { cleanup, makeTempDir, stageNodeModules } = require("./helpers/workspace");

after(cleanup);

const FAKE_RUNNER = `
const args = process.argv.slice(2);
process.stdout.write(JSON.stringify(args));
const marker = args.find((arg) => arg.startsWith("--exit-code="));
process.exit(marker ? Number(marker.slice("--exit-code=".length)) : 0);
`;

function writeRunner(modules, source = FAKE_RUNNER) {
    const runner = path.join(modules, "@guchho", "test");
    const cli = path.join(runner, "dist", "cli", "index.js");

    fs.mkdirSync(path.dirname(cli), { recursive: true });
    fs.writeFileSync(
        path.join(runner, "package.json"),
        JSON.stringify({
            name: "@guchho/test",
            version: "0.0.1",
            bin: { "guchho-test": "./dist/cli/index.js" },
        })
    );
    fs.writeFileSync(cli, source);
    return cli;
}

function stageWithRunner() {
    const staged = stageNodeModules({ withPlatform: false });
    writeRunner(staged.modules);
    return staged;
}

function runShim(staged, args, cwd = staged.project) {
    const shim = path.join(staged.guchhoPkg, "bin", "guchho.js");
    return spawnSync(process.execPath, [shim, ...args], {
        cwd,
        encoding: "utf8",
    });
}

describe("guchho test", () => {
    it("runs the runner and drops the test command from its arguments", () => {
        const staged = stageWithRunner();

        const result = runShim(staged, ["test", "--help"]);

        assert.equal(result.status, 0, result.stderr);
        assert.deepEqual(JSON.parse(result.stdout), ["--help"]);
    });

    it("returns the runner's exit status", () => {
        const staged = stageWithRunner();

        const result = runShim(staged, ["test", "--exit-code=7"]);

        assert.equal(result.status, 7, result.stderr);
        assert.deepEqual(JSON.parse(result.stdout), ["--exit-code=7"]);
    });

    it("does not need a native binary to dispatch", () => {
        const staged = stageWithRunner();

        const result = runShim(staged, ["test"]);

        assert.equal(result.status, 0, result.stderr);
        assert.deepEqual(JSON.parse(result.stdout), []);
    });

    it("finds a runner installed in the project when guchho is elsewhere", () => {
        const staged = stageNodeModules({ withPlatform: false });
        const project = makeTempDir("runner-project");
        fs.mkdirSync(path.join(project, "node_modules"), { recursive: true });
        writeRunner(path.join(project, "node_modules"));

        const result = runShim(staged, ["test", "--help"], project);

        assert.equal(result.status, 0, result.stderr);
        assert.deepEqual(JSON.parse(result.stdout), ["--help"]);
    });

    it("explains how to install the runner when it is missing", () => {
        const staged = stageNodeModules({ withPlatform: false });

        const result = runShim(staged, ["test", "--help"]);

        assert.equal(result.status, 1);
        assert.match(result.stderr, /npm install --save-dev @guchho\/test/);
        assert.match(result.stderr, /Node\.js 20\.19 or newer/);
    });
});
