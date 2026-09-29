"use strict";

// The public surface of the package, as CommonJS.
//
// This is the API the package publishes, and removing names from it is the
// breaking change CHANGELOG.md names. A caller that used formatMessages,
// analyzeMetafile, getBinaryPath, getPlatformKey or spawnBinary has no
// replacement on this list, deliberately: each was the inside of how the
// package works, and a name that is not on the list is not a name this package
// promises.
//
// The three error types stay on the list because "throws structured
// BuildFailure" only helps a caller if BuildFailure is importable — matching
// on a message string is how a caller ends up binding itself to wording that
// changes. Everything else that used to be exported is internal now, reachable
// by requiring the module that has it, which a caller depending on a promise
// should not be doing.

const { build, analyze } = require("./build");
const { transform } = require("./transform");
const { context, BuildContext } = require("./context");
const { stopService, ServiceError, BuildFailure } = require("./service");
const { version } = require("./info");
const {
    lexHTML,
    lexCSS,
    lexJS,
    parseHTML,
    parseCSS,
    parseJS,
    transformHTML,
    transformCSS,
    transformJS,
    printHTML,
    printCSS,
    printJS,
} = require("./compile");

// Kept so a caller can be sure the process is gone before it cleans up after a
// build — removing an output directory while the service still has it open is an
// EPERM on Windows, and awaiting this is the difference between a teardown that
// works and one that does not.
module.exports = {
    // The guchho API.
    build,
    transform,
    context,
    analyze,
    stop: stopService,
    version,

    // The compiler API: lex, parse, transform and print, per language.
    lexHTML,
    parseHTML,
    transformHTML,
    printHTML,
    lexCSS,
    parseCSS,
    transformCSS,
    printCSS,
    lexJS,
    parseJS,
    transformJS,
    printJS,

    // The failure shapes, so a caller can tell a build that failed from an
    // installation that is broken without matching on a message.
    BuildFailure,
    ServiceError,
    BuildContext,
};