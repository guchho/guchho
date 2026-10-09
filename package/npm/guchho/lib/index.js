"use strict";

// The public surface of the package, as CommonJS.
//
// This is the API the package publishes, and removing names from it is the
// breaking change CHANGELOG.md names. A caller that used formatMessages,
// getBinaryPath, getPlatformKey or spawnBinary has no replacement on this list,
// deliberately: each was the inside of how the package works, and a name that is
// not on the list is not a name this package promises.
//
// analyze changed meaning rather than disappearing. It used to take a metafile
// and return a text report; it now takes a build configuration and returns the
// analysis as data. The old behaviour is still reachable, under the name that
// says what it does: analyzeMetafile.
//
// The three error types stay on the list because "throws structured
// BuildFailure" only helps a caller if BuildFailure is importable — matching
// on a message string is how a caller ends up binding itself to wording that
// changes. Everything else that used to be exported is internal now, reachable
// by requiring the module that has it, which a caller depending on a promise
// should not be doing.

const { build, analyzeMetafile } = require("./build");
const { analyze } = require("./analyze");
const { transform } = require("./transform");
const { zip } = require("./zip");
const { context, BuildContext } = require("./context");
const { watch, Watcher } = require("./watch");
const { serve, DevServer } = require("./serve");
const { stopService, ServiceError, BuildFailure } = require("./service");
const { version } = require("./info");
const { lexer, parse, print } = require("./compile");

// Kept so a caller can be sure the process is gone before it cleans up after a
// build — removing an output directory while the service still has it open is an
// EPERM on Windows, and awaiting this is the difference between a teardown that
// works and one that does not.
module.exports = {
    // The guchho API.
    build,
    transform,
    zip,
    context,
    analyze,
    analyzeMetafile,
    watch,
    serve,
    stop: stopService,
    version,

    // The compiler API: one function per stage, told which language it is for.
    // lexer, parse and print — not twelve functions spelling the same three
    // operations once per language.
    lexer,
    parse,
    print,

    // The failure shapes, so a caller can tell a build that failed from an
    // installation that is broken without matching on a message.
    BuildFailure,
    ServiceError,

    // The handle classes, so `instanceof` is possible without importing an
    // internal module to get at them.
    BuildContext,
    Watcher,
    DevServer,
};