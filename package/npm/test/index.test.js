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
const PUBLIC_API = ["getBinaryPath", "getPlatformKey", "spawnBinary", "transform", "build"];

describe("the package's CommonJS entry", () => {
    it("exports the public API and nothing else", () => {
        assert.deepEqual(Object.keys(guchho).sort(), [...PUBLIC_API].sort());
    });

    it("exports each of them as a function, because a name is not a contract", () => {
        // A namespace holding five undefined properties resolves, imports and
        // destructures without complaint, and only fails when called. Which is
        // to say: at the first build.
        for (const name of PUBLIC_API) {
            assert.equal(typeof guchho[name], "function", `${name} should be a function`);
        }
    });

    it("is the same function as the module it re-exports, not a wrapper", () => {
        // A wrapper would be a second answer to the same question, and the two
        // would drift: a caller checking the arity of build() or relying on
        // its default argument would be reading a copy.
        const { build } = require("../guchho/lib/build");
        const { transform } = require("../guchho/lib/transform");
        const { getBinaryPath, getPlatformKey, spawnBinary } = require("../guchho/lib/main");

        assert.equal(guchho.build, build);
        assert.equal(guchho.transform, transform);
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
