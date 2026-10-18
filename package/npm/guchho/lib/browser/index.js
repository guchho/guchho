"use strict";

// The browser surface of the package.
//
// It publishes the same names the Node entry does, for the APIs the
// WebAssembly engine can actually serve: build, transform, pack, analyze,
// analyzeMetafile, context, and the three compiler stages. Every one of them
// is the same function the Node entry uses — the orchestrators in lib/ are
// written once — running against the transport installed here instead of a
// pipe.
//
// watch, serve and stop are the three that need a host process, so they are
// kept as names that refuse rather than removed. A caller who wrote
// `import { watch } from "guchho"` gets a sentence naming the native package,
// not `undefined` and a crash three lines later.
//
// Plugins are held back by a JavaScript pre-check in the transport, not here:
// a build that names one is refused with a plain Error (not a BuildFailure)
// before the request reaches the engine. A project's config file is read
// by the engine from the in-memory tree, but the JavaScript half of the plugin
// bridge has no file to read it from in a browser, so config-file plugins are
// ignored. Both are documented in docs/wasm.md.

const transport = require("../transport-wasm");

const { build, analyzeMetafile } = require("../build");
const { analyze } = require("../analyze");
const { transform } = require("../transform");
const { pack } = require("../pack");
const { context, BuildContext } = require("../context");
const { BuildFailure, ServiceError } = require("../errors");
const { version } = require("../info");
const { lexer, parse, print } = require("../compile");

// A name the browser cannot keep, kept as a name that says why. Thrown when
// called, not when imported: an import of the module should not fail over a
// feature the caller never reaches for.
function unavailable(name) {
    return function unavailableInBrowser() {
        throw new Error(
            `${name} is not available in the browser: the WebAssembly engine has ` +
                "no sockets and no watchers. Run it with the native package, or " +
                "use build() and the compiler stages, which the WASM engine serves."
        );
    };
}

module.exports = {
    // The guchho API, as far as the in-process engine reaches.
    build,
    transform,
    pack,
    context,
    analyze,
    analyzeMetafile,
    version,

    // The compiler API: the same three functions, the same languages.
    lexer,
    parse,
    print,

    // The browser's own knob: the files the in-memory engine builds from.
    // configure() before the first call, loadFiles() to add more after.
    configure: transport.configure,
    loadFiles: transport.loadFiles,

    // Not available without a host process.
    watch: unavailable("watch"),
    serve: unavailable("serve"),
    stop: async () => {},

    // The failure shapes, and the context handle, exactly as the Node entry
    // publishes them.
    BuildFailure,
    ServiceError,
    BuildContext,
};
