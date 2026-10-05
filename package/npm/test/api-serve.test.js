// serve(): the engine's dev server, with a handle a caller can hold and close.
//
// The engine's HTTP server is not reimplemented here, so what is worth pinning
// down is the handle and the wiring around it — and in particular that the server
// actually serves the build:
//
//   - serve() resolves once the server is listening, so the address is a value
//     rather than something to be scraped out of a log;
//   - the served directory defaults to the build's output directory. Without that
//     the engine leaves its served tree empty and every request is a 404 — a
//     running, reachable server that serves no files — so this is asserted by
//     fetching, not by reading the configuration back;
//   - a rebuild is visible without restarting the server;
//   - close() stops it, and the port stops answering;
//   - the URL points somewhere connectable, which is not the same as the address
//     the engine bound.

const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const os = require("node:os");
const path = require("node:path");
const { after, describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");
const { cleanup, skipWithoutBinary } = require("./helpers/workspace");

after(cleanup);

const needsBinary = { skip: skipWithoutBinary() };

function makeProject(initial = 'console.log("first");\n') {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "guchho-serve-test-"));
    fs.mkdirSync(path.join(root, "src"), { recursive: true });
    fs.writeFileSync(path.join(root, "src", "main.js"), initial);
    return root;
}

// Port 0 every time. The engine takes the first free port from 8000 upwards, so
// two servers in one test run do not collide, and a hard-coded 3000 would make
// every test fail at once on a machine that already has something there.
function serveConfig(root, extra = {}) {
    return {
        absWorkingDir: root,
        entryPoints: ["src/main.js"],
        logLevel: "silent",
        server: { port: 0 },
        ...extra,
    };
}

// A GET that resolves with the status and body, and rejects on a refused
// connection — which is how the closed-server test tells "stopped" from "serving
// an empty 404".
function get(url) {
    return new Promise((resolve, reject) => {
        const request = http.get(url, (response) => {
            let body = "";
            response.setEncoding("utf8");
            response.on("data", (chunk) => {
                body += chunk;
            });
            response.on("end", () => resolve({ status: response.statusCode, body }));
        });
        request.on("error", reject);
    });
}

// Asserts that nothing is listening on this URL any more.
//
// The shape of the refusal is not fixed: Node resolves a hostname to both an IPv4
// and an IPv6 address and races the two, so a closed port surfaces as an
// AggregateError holding two ECONNREFUSEs rather than as one. What matters is
// that the connection was refused — which is what separates "the server stopped"
// from "the server is running and answered something else".
async function assertNotListening(url) {
    let thrown = null;
    try {
        await get(url);
    } catch (error) {
        thrown = error;
    }

    assert.ok(thrown !== null, `expected ${url} to stop answering, but it answered`);

    const detail = [thrown.code, thrown.message, ...(thrown.errors || []).map((inner) => `${inner.code} ${inner.message}`)]
        .filter(Boolean)
        .join(" ");
    assert.match(detail, /ECONNREFUSED/, `expected a refused connection, got: ${detail}`);
}

describe("serve()", () => {
    it("returns a handle with an address once it is listening", needsBinary, async () => {
        const root = makeProject();

        const server = await guchho.serve(serveConfig(root));

        assert.ok(server instanceof guchho.DevServer);
        assert.equal(server.closed, false);
        assert.equal(typeof server.port, "number");
        assert.ok(server.port > 0, "the port that was bound should be reported");
        assert.equal(typeof server.host, "string");
        assert.ok(server.host.length > 0, "the host should never be empty");
        assert.ok(Array.isArray(server.hosts));

        await server.close();
    });

    it("serves the build's output, which is the whole point of it", needsBinary, async () => {
        const root = makeProject('console.log("first");\n');

        const server = await guchho.serve(serveConfig(root));

        const response = await get(`${server.url}/main.js`);

        assert.equal(response.status, 200, "the built file should be served");
        assert.ok(response.body.includes("first"), `got: ${JSON.stringify(response.body.slice(0, 60))}`);

        await server.close();
    });

    it("points its url at an address a browser can actually open", needsBinary, async () => {
        const root = makeProject();
        const server = await guchho.serve(serveConfig(root));

        // The engine binds every interface by default and reports that as the
        // host, and "0.0.0.0" is not a destination a browser can route to. The URL
        // has to be a connectable one even when the bound address is a wildcard.
        assert.match(server.url, /^http:\/\/(localhost|127\.0\.0\.1|\[::1\]):\d+$/);
        assert.equal(server.url.includes("0.0.0.0"), false, "a wildcard bind is not a url");

        await server.close();
    });

    it("serves a rebuild without being restarted", needsBinary, async () => {
        const root = makeProject('console.log("first");\n');
        const server = await guchho.serve(serveConfig(root));

        assert.ok((await get(`${server.url}/main.js`)).body.includes("first"));

        fs.writeFileSync(path.join(root, "src", "main.js"), 'console.log("second");\n');
        const result = await server.rebuild();

        assert.deepEqual(result.errors, []);
        assert.ok((await get(`${server.url}/main.js`)).body.includes("second"));

        await server.close();
    });

    it("serves another directory when the caller names one", needsBinary, async () => {
        const root = makeProject();
        const publicDir = path.join(root, "public");
        fs.mkdirSync(publicDir, { recursive: true });
        fs.writeFileSync(path.join(publicDir, "index.html"), "<h1>from servedir</h1>\n");

        const server = await guchho.serve(
            serveConfig(root, { server: { port: 0, servedir: publicDir } })
        );

        const response = await get(`${server.url}/index.html`);
        assert.equal(response.status, 200);
        assert.ok(response.body.includes("from servedir"));

        await server.close();
    });

    it("closes once, stops answering, and refuses to be used afterwards", needsBinary, async () => {
        const root = makeProject();
        const server = await guchho.serve(serveConfig(root));

        const url = server.url;
        assert.equal((await get(`${url}/main.js`)).status, 200);

        await server.close();
        assert.equal(server.closed, true);

        // Twice, because close() is reached from finally blocks as often as from
        // callers.
        await server.close();

        // The port stops answering, rather than serving 404s: the difference
        // matters because a 404 would look like a working server with the wrong
        // path, which is the failure this test exists to catch.
        await assertNotListening(`${url}/main.js`);
        await assert.rejects(() => server.rebuild(), /closed/);
    });

    it("keeps the build warm across requests, which is what a server is for", needsBinary, async () => {
        const root = makeProject();
        const server = await guchho.serve(serveConfig(root));

        // The context behind the server is the same one the server was built
        // from, so the expensive half of a build is not repeated per rebuild.
        assert.ok(server.context instanceof guchho.BuildContext);

        const first = await server.rebuild();
        const second = await server.rebuild();

        assert.deepEqual(first.errors, []);
        assert.deepEqual(second.errors, []);
        assert.equal(typeof second.duration, "number");

        await server.close();
    });

    it("refuses 'open' rather than quietly not opening a browser", async () => {
        // No binary needed: this is refused before anything starts.
        await assert.rejects(
            () => guchho.serve({ server: { open: true } }),
            /not supported by the JavaScript API/
        );
    });

    it("refuses a server option it does not have, before starting anything", async () => {
        await assert.rejects(() => guchho.serve(42), TypeError);
        await assert.rejects(() => guchho.serve({ server: { prot: 3000 } }), TypeError);
        await assert.rejects(() => guchho.serve({ server: "localhost" }), TypeError);
    });

    it("releases itself through Symbol.asyncDispose, so a scope cannot leak a server", needsBinary, async (t) => {
        if (typeof Symbol.asyncDispose !== "symbol") {
            t.skip("this Node has no Symbol.asyncDispose");
            return;
        }

        const root = makeProject();
        let handle = null;
        let url = "";

        {
            await using scoped = await guchho.serve(serveConfig(root));
            handle = scoped;
            url = scoped.url;
            assert.equal((await get(`${url}/main.js`)).status, 200);
        }

        assert.equal(handle.closed, true, "leaving the scope should have closed the server");
        await assertNotListening(`${url}/main.js`);
    });
});
