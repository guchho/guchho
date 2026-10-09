// The public surface, checked from the CommonJS side.
//
// api-transform.test.mjs covers the ES module entry, because "import * as
// guchho" is the documented way in and a namespace import has to be static to be
// worth anything. This file is the other half: that requiring the package gives
// the same things, and that the two entries cannot drift apart.
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

// What the package promises. Spelled out rather than derived, so that adding
// one to lib/index.js is a decision somebody has to notice here.
//
// The list is the naming breaking change CHANGELOG.md names: formatMessages,
// getBinaryPath, getPlatformKey and spawnBinary are gone. analyze changed
// meaning rather than disappearing — it takes a configuration now, and the old
// metafile report is analyzeMetafile. Nothing here is a leftover from the old
// surface, because a leftover is a promise nobody asked for.
const PUBLIC_API = [
    "analyze",
    "analyzeMetafile",
    "build",
    "context",
    "lexer",
    "parse",
    "print",
    "serve",
    "stop",
    "transform",
    "watch",
    "zip",
];

// The classes. Checked as constructors rather than as functions, because what a
// caller needs from these is `instanceof`.
const PUBLIC_CLASSES = ["BuildContext", "BuildFailure", "DevServer", "ServiceError", "Watcher"];

// The version, which is a value rather than a function, and is still part of
// the exported surface a caller can count on.
const PUBLIC_CONSTANTS = ["version"];

describe("the package's CommonJS entry", () => {
    it("exports the public API and nothing else", () => {
        assert.deepEqual(
            Object.keys(guchho).sort(),
            [...PUBLIC_API, ...PUBLIC_CLASSES, ...PUBLIC_CONSTANTS].sort()
        );
    });

    it("exports each of them, because a name is not a contract", () => {
        // A namespace holding undefined properties resolves, imports and
        // destructures without complaint, and only fails when called. Which is
        // to say: at the first build.
        for (const name of PUBLIC_API) {
            assert.equal(typeof guchho[name], "function", `${name} should be a function`);
        }
        for (const name of PUBLIC_CLASSES) {
            assert.equal(typeof guchho[name], "function", `${name} should be a constructor`);
        }
    });

    it("exports the version as a string, which is what a version is", () => {
        assert.equal(typeof guchho.version, "string");
        assert.ok(guchho.version.length > 0, "version should not be empty");
    });

    it("exports no name. it swore to drop", () => {
        // The break is documented as a break. A name that still leaks out is a
        // caller quietly depending on something the CHANGELOG said is gone.
        for (const name of ["formatMessages", "getBinaryPath", "getPlatformKey", "spawnBinary"]) {
            assert.equal(guchho[name], undefined, `${name} should not be on the surface`);
        }
    });

    it("exports the error types as constructors a caller can test against", () => {
        // Both are Errors, because a caller that catches everything and checks
        // `instanceof Error` must not find a build failure quietly missing.
        assert.ok(guchho.BuildFailure.prototype instanceof Error);
        assert.ok(guchho.ServiceError.prototype instanceof Error);
    });

    it("is the same function as the module it re-exports, not a wrapper", () => {
        // A wrapper would be a second answer to the same question, and the two
        // would drift: a caller checking the arity of build() or relying on
        // its default argument would be reading a copy.
        const { build, analyzeMetafile } = require("../guchho/lib/build");
        const { analyze } = require("../guchho/lib/analyze");
        const { transform } = require("../guchho/lib/transform");
        const { context, BuildContext } = require("../guchho/lib/context");
        const { watch, Watcher } = require("../guchho/lib/watch");
        const { serve, DevServer } = require("../guchho/lib/serve");
        const { stopService } = require("../guchho/lib/service");
        const { version } = require("../guchho/lib/info");
        const compile = require("../guchho/lib/compile");

        assert.equal(guchho.build, build);
        assert.equal(guchho.transform, transform);
        assert.equal(guchho.context, context);
        assert.equal(guchho.analyze, analyze);
        assert.equal(guchho.analyzeMetafile, analyzeMetafile);
        assert.equal(guchho.stop, stopService);
        assert.equal(guchho.version, version);
        assert.equal(guchho.BuildContext, BuildContext);
        assert.equal(guchho.watch, watch);
        assert.equal(guchho.Watcher, Watcher);
        assert.equal(guchho.serve, serve);
        assert.equal(guchho.DevServer, DevServer);
        assert.equal(guchho.lexer, compile.lexer);
        assert.equal(guchho.parse, compile.parse);
        assert.equal(guchho.print, compile.print);
    });

    it("exports no compiler function that spells one stage per language", () => {
        // The break is documented as a break. A name that still leaks out is a
        // caller quietly depending on something the CHANGELOG said is gone, and
        // the twelve per-language names are the clearest case: they were the
        // reason for the change.
        for (const language of ["HTML", "CSS", "JS"]) {
            for (const stage of ["lex", "parse", "transform", "print"]) {
                const name = `${stage}${language}`;
                assert.equal(guchho[name], undefined, `${name} should not be on the surface`);
            }
        }
        assert.equal(guchho.transformAst, undefined, "there is no fourth compiler function");
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
