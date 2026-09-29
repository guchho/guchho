// The public surface, checked from the CommonJS side.
//
// api-transform.test.mjs covers the ES module entry, because "import * as
// guchho" is the documented way in and a namespace import has to be static to be
// worth anything. This file is the other half: that requiring the package gives
// the same five things, and that the two entries cannot drift apart.
//
// The reason it is a separate file rather than a couple of tests inside
// api-build.test.js is the thing being checked. That one asks "does build() work",
// which needs a binary. This asks "is build() on the namespace at all", which
// does not — and a test that can only run on a machine that has built the
// project is a test that says nothing on a fresh checkout, which is most of the
// machines the suite is run on in CI.
//
// On the platform table not being here: lib/index.js deliberately does not
// re-export PLATFORM_PACKAGES, because it is the inside of how the binary is
// found and a caller reading it to decide what to install is reading a private
// detail that changes whenever a platform is added. That decision is worth a
// test, since the cheapest way to reverse it by accident is to spread a
// re-export that looks helpful.

const assert = require("node:assert/strict");
const { describe, it } = require("node:test");

const guchho = require("../guchho/lib/index.js");
const { PLATFORM_PACKAGES } = require("../guchho/lib/platforms");

// The five things the package promises. Spelled out rather than derived, so
// that adding one to lib/index.js is a decision somebody has to notice here.
//
// It used to be five names. It is fourteen because the API is now the one
// esbuild publishes, and the three that were already there are still among them:
// getBinaryPath, getPlatformKey and spawnBinary stay because a caller that has
// to find the binary in order to pass its own arguments to it has no other way
// to ask.
//
// The three error types are on the list because a caller has to be able to tell
// a build that failed from an installation that is broken, and the only way to
// do that is to have both names to check.
const PUBLIC_API = [
    "analyzeMetafile",
    "build",
    "context",
    "formatMessages",
    "getBinaryPath",
    "getPlatformKey",
    "spawnBinary",
    "stop",
    "transform",
    "version",
];

// The classes. Checked as constructors rather than as functions, because what a
// caller needs from these is `instanceof`.
const PUBLIC_CLASSES = ["BuildContext", "BuildFailure", "ServiceError"];

describe("the package's CommonJS entry", () => {
    it("exports the public API and nothing else", () => {
        assert.deepEqual(
            Object.keys(guchho).sort(),
            [...PUBLIC_API, ...PUBLIC_CLASSES].sort()
        );
    });

    it("exports each of them as a function, because a name is not a contract", () => {
        // A namespace holding fourteen undefined properties resolves, imports and
        // destructures without complaint, and only fails when called. Which is
        // to say: at the first build.
        for (const name of PUBLIC_API) {
            assert.equal(typeof guchho[name], "function", `${name} should be a function`);
        }
    });

    it("exports the error types as constructors a caller can test against", () => {
        for (const name of PUBLIC_CLASSES) {
            assert.equal(typeof guchho[name], "function", `${name} should be a constructor`);
        }
        // Both are Errors, because a caller that catches everything and checks
        // `instanceof Error` must not find a build failure quietly missing.
        assert.ok(guchho.BuildFailure.prototype instanceof Error);
        assert.ok(guchho.ServiceError.prototype instanceof Error);
    });

    it("is the same function as the module it re-exports, not a wrapper", () => {
        // A wrapper would be a second answer to the same question, and the two
        // would drift: a caller checking the arity of build() or relying on
        // its default argument would be reading a copy.
        const { build, formatMessages, analyzeMetafile } = require("../guchho/lib/build");
        const { transform } = require("../guchho/lib/transform");
        const { context, BuildContext } = require("../guchho/lib/context");
        const { stopService } = require("../guchho/lib/service");
        const { getBinaryPath, getPlatformKey, spawnBinary } = require("../guchho/lib/main");
        const { version } = require("../guchho/lib/info");

        assert.equal(guchho.build, build);
        assert.equal(guchho.transform, transform);
        assert.equal(guchho.context, context);
        assert.equal(guchho.formatMessages, formatMessages);
        assert.equal(guchho.analyzeMetafile, analyzeMetafile);
        assert.equal(guchho.stop, stopService);
        assert.equal(guchho.version, version);
        assert.equal(guchho.BuildContext, BuildContext);
        assert.equal(guchho.getBinaryPath, getBinaryPath);
        assert.equal(guchho.getPlatformKey, getPlatformKey);
        assert.equal(guchho.spawnBinary, spawnBinary);
    });

    it("keeps the platform table to itself, because a package name is not an API", () => {
        // Exported here it would invite a caller to read the table and install
        // from it, and the table changes whenever a platform is added — so
        // every such caller would be reading a private detail.
        assert.equal(guchho.PLATFORM_PACKAGES, undefined);
        assert.equal(guchho.platFormPackages, undefined);
        assert.ok(PLATFORM_PACKAGES, "the table is still there for install.js and lib/main.js");
    });
});
