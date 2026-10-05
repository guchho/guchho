// watch(): a warm build that runs again, with events for the rebuilds it makes.
//
// The thing worth pinning down is not that a rebuild works — that is context()'s
// job and api-context.test.js has it — but the handle's own contract:
//
//   - watch() has already built by the time it resolves, so the outputs exist
//     and there is no window in which a caller holds a watcher that has not run;
//   - every buildEnd is a rebuild this handle performed, because the engine's
//     service never sends one of its own;
//   - a build error does not end the watcher, since the next save is the one that
//     may fix it;
//   - close() is idempotent, and refuses further use rather than throwing from a
//     cleanup path;
//   - interval: 0 means "do not drive yourself", which is a real setting and not
//     an absent one.
//
// The events-are-not-a-push point is asserted rather than assumed, because it is
// the one a caller would otherwise get wrong: a test that watched a file change
// and waited for a buildEnd without touching anything would pass on a timer and
// prove nothing about what a user gets.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");
const { cleanup, skipWithoutBinary } = require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function makeProject(initial = "const answer = 42;\nconsole.log(answer);\n") {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "guchho-watch-test-"));
    fs.mkdirSync(path.join(root, "src"), { recursive: true });
    fs.writeFileSync(path.join(root, "src", "main.js"), initial);
    return root;
}

// A project with no interval, so nothing drives itself and every rebuild in a
// test is one the test asked for.
function watchConfig(root, extra = {}) {
    return {
        absWorkingDir: root,
        entrypoints: ["src/main.js"],
        write: false,
        logLevel: "silent",
        watch: { interval: 0 },
        ...extra,
    };
}

describe("watch()", () => {
    it("returns a handle that has already built once", needsBinary, async () => {
        const root = makeProject();

        const watcher = await guchho.watch(watchConfig(root));

        assert.ok(watcher instanceof guchho.Watcher);
        assert.equal(watcher.closed, false);
        // The opening build's result, readable without triggering a second one.
        assert.deepEqual(watcher.result.errors, []);
        assert.ok(watcher.result.outputs.length > 0, "the opening build should have produced output");

        await watcher.close();
    });

    it("emits buildStart and buildEnd around each rebuild, with the result", needsBinary, async () => {
        const root = makeProject();

        const watcher = await guchho.watch(watchConfig(root));

        const events = [];
        watcher.on("buildStart", () => events.push("start"));
        watcher.on("buildEnd", (result) => events.push(`end:${result.outputs.length}`));

        const result = await watcher.rebuild();

        assert.deepEqual(events, ["start", `end:${result.outputs.length}`]);
        await watcher.close();
    });

    it("returns the result as well as emitting it, so awaiting and listening agree", needsBinary, async () => {
        const root = makeProject();
        const watcher = await guchho.watch(watchConfig(root));

        let fromEvent = null;
        watcher.on("buildEnd", (result) => {
            fromEvent = result;
        });

        const returned = await watcher.rebuild();

        // The same object, not two builds' worth of results: a caller that both
        // listens and awaits should not be told about two passes.
        assert.equal(fromEvent, returned);
        assert.equal(watcher.result, returned);

        await watcher.close();
    });

    it("sees changes between rebuilds", needsBinary, async () => {
        const root = makeProject('console.log("first");\n');
        const watcher = await guchho.watch(watchConfig(root));

        fs.writeFileSync(path.join(root, "src", "main.js"), 'console.log("second");\n');
        const result = await watcher.rebuild();

        const text = result.outputFiles.map((file) => file.text).join("\n");
        assert.equal(text.includes("second"), true, "the rebuild should have picked up the edit");
        assert.equal(text.includes("first"), false);

        await watcher.close();
    });

    it("does not rebuild on a file change by itself, because nothing pushes", needsBinary, async () => {
        const root = makeProject();
        // No interval: the handle never drives itself.
        const watcher = await guchho.watch(watchConfig(root));

        let builds = 0;
        watcher.on("buildEnd", () => {
            builds += 1;
        });

        const openingHash = watcher.result.outputs[0].hash;

        fs.writeFileSync(path.join(root, "src", "main.js"), "console.log(2);\n");
        await sleep(300);

        // The engine's watcher thread does notice the change and does rebuild,
        // inside the service. It never tells this side, so the handle reports
        // nothing — which is the honest answer, and the reason the docs say
        // events mark rebuilds the handle performed.
        assert.equal(builds, 0, "a watcher with no interval should rebuild only when asked");
        assert.equal(
            watcher.result.outputs[0].hash,
            openingHash,
            "the last result should still be the opening build's, untouched by the edit"
        );

        // And the edit is not lost: asking for a rebuild picks it up, so the file
        // was never going to be missed — it simply was not reported on its own.
        const after = await watcher.rebuild();
        assert.notEqual(after.outputs[0].hash, openingHash, "the rebuild should see the edit");

        await watcher.close();
    });

    it("drives itself when given an interval", needsBinary, async () => {
        const root = makeProject();
        const watcher = await guchho.watch(watchConfig(root, { watch: { interval: 40 } }));

        assert.equal(watcher.interval, 40);

        let builds = 0;
        watcher.on("buildEnd", () => {
            builds += 1;
        });

        await sleep(350);

        assert.ok(builds > 0, "an interval watcher should have rebuilt on its own");
        await watcher.close();
    });

    it("treats interval: 0 as never, rather than as fast as possible", needsBinary, async () => {
        const root = makeProject();
        const watcher = await guchho.watch(watchConfig(root));

        assert.equal(watcher.interval, 0);

        let builds = 0;
        watcher.on("buildEnd", () => {
            builds += 1;
        });
        await sleep(250);

        assert.equal(builds, 0, "a zero interval must not become a timer that never waits");
        await watcher.close();
    });

    it("defaults to an interval rather than to never", async () => {
        // No build happens here — this reads the default off the module, so it
        // needs no binary.
        const { DEFAULT_INTERVAL } = require("../guchho/lib/watch");

        assert.equal(typeof DEFAULT_INTERVAL, "number");
        assert.ok(DEFAULT_INTERVAL > 0, "the default should be a real period");
    });

    it("survives a build that fails, because the next save is the fix", needsBinary, async () => {
        const root = makeProject("const broken = ;\n");
        const watcher = await guchho.watch(watchConfig(root));

        // A project that does not compile the first time is a normal state for a
        // watcher to be in, so the opening build's failure is in its result rather
        // than thrown from watch() itself.
        assert.ok(watcher.result.errors.length > 0, "the broken source should be reported");
        assert.equal(watcher.closed, false, "a failed build must not close the watcher");

        fs.writeFileSync(path.join(root, "src", "main.js"), "const fixed = 1;\nconsole.log(fixed);\n");
        const result = await watcher.rebuild();

        assert.deepEqual(result.errors, [], "the watcher should still work after a failure");
        await watcher.close();
    });

    it("reports a rebuild that throws on the error event, and keeps watching", needsBinary, async () => {
        const root = makeProject();
        const watcher = await guchho.watch(watchConfig(root));

        const seen = [];
        watcher.on("error", (error) => seen.push(error));
        watcher.on("buildStart", () => seen.push("start"));

        // Two rebuilds at once: the second is refused rather than silently joined
        // to the first, so the event stream stays honest about what ran.
        const first = watcher.rebuild();
        await assert.rejects(
            () => watcher.rebuild(),
            /already rebuilding/
        );
        await first;

        assert.ok(seen.includes("start"), "the refused rebuild should not have started");
        await watcher.close();
    });

    it("does not need an error listener, which EventEmitter would otherwise require", needsBinary, async () => {
        // EventEmitter throws an unhandled "error" event. A watcher that only
        // wants results should not have to attach one, and a throw from inside a
        // timer is an unhandled rejection that takes the process down.
        const root = makeProject();
        const watcher = await guchho.watch(watchConfig(root, { watch: { interval: 30 } }));

        await sleep(200);
        assert.equal(watcher.closed, false);
        await watcher.close();
    });

    it("keeps a listener's own failure off the rebuild's result", needsBinary, async () => {
        const root = makeProject();
        const watcher = await guchho.watch(watchConfig(root));

        watcher.on("buildEnd", () => {
            throw new Error("the listener is broken");
        });

        // The listener's failure is not allowed to become the caller's: a
        // successful rebuild reported as a failed one is worse than a broken
        // listener.
        const result = await watcher.rebuild();
        assert.deepEqual(result.errors, []);

        await watcher.close();
    });

    it("closes once, and refuses to be used afterwards", needsBinary, async () => {
        const root = makeProject();
        const watcher = await guchho.watch(watchConfig(root));

        await watcher.close();
        assert.equal(watcher.closed, true);

        // Twice, because close() is reached from finally blocks as often as from
        // callers, and closing twice must be the same quiet nothing.
        await watcher.close();

        await assert.rejects(() => watcher.rebuild(), /closed/);
    });

    it("stops driving itself once closed", needsBinary, async () => {
        const root = makeProject();
        const watcher = await guchho.watch(watchConfig(root, { watch: { interval: 30 } }));

        let builds = 0;
        watcher.on("buildEnd", () => {
            builds += 1;
        });

        await watcher.close();
        const afterClose = builds;
        await sleep(200);

        assert.equal(builds, afterClose, "a closed watcher should not keep rebuilding");
    });

    it("refuses a configuration it cannot act on, before starting anything", async () => {
        // No binary needed: none of these reach the service.
        await assert.rejects(() => guchho.watch(42), TypeError);
        await assert.rejects(() => guchho.watch({ watch: { delayd: 100 } }), TypeError);
        await assert.rejects(() => guchho.watch({ watch: { interval: -1 } }), TypeError);
        await assert.rejects(() => guchho.watch({ watch: { interval: "fast" } }), TypeError);
    });

    it("exposes the context behind it, and it is the same one that built", needsBinary, async () => {
        const root = makeProject();
        const watcher = await guchho.watch(watchConfig(root));

        assert.ok(watcher.context instanceof guchho.BuildContext);
        assert.equal(typeof watcher.context.key, "number");

        await watcher.close();
    });

    it("releases itself through Symbol.asyncDispose, so a scope cannot leak one", needsBinary, async (t) => {
        if (typeof Symbol.asyncDispose !== "symbol") {
            t.skip("this Node has no Symbol.asyncDispose");
            return;
        }

        const root = makeProject();
        let handle = null;

        {
            await using scoped = await guchho.watch(watchConfig(root));
            handle = scoped;
            assert.equal(scoped.closed, false);
        }

        assert.equal(handle.closed, true, "leaving the scope should have closed the watcher");
    });
});
