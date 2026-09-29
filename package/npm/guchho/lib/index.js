"use strict";

// The public surface of the package, as CommonJS.
//
// The guchho API and the three things that were already here. Those three stay
// because a caller that has to locate the binary in order to pass its own
// arguments to it has no other way to ask, and removing an export is a break
// that buys nothing.
//
// The platform table is deliberately not re-exported. It is the inside of how
// the binary is found, and a caller reading it to decide what to install is
// reading a private detail that changes whenever a platform is added.

const { getBinaryPath, getPlatformKey, spawnBinary } = require("./main");
const { transform } = require("./transform");
const { build, formatMessages, analyzeMetafile } = require("./build");
const { context, BuildContext } = require("./context");
const { stopService, ServiceError, BuildFailure } = require("./service");
const { version } = require("./info");

// Kept so a caller can be sure the process is gone before it cleans up after a
// build — removing an output directory while the service still has it open is an
// EPERM on Windows, and awaiting this is the difference between a teardown that
// works and one that does not.
module.exports = {
    // The guchho API.
    build,
    transform,
    context,
    formatMessages,
    analyzeMetafile,
    stop: stopService,
    version,

    // Was already public; kept.
    getBinaryPath,
    getPlatformKey,
    spawnBinary,

    // The failure shapes, so a caller can tell a build that failed from an
    // installation that is broken without matching on a message.
    BuildFailure,
    ServiceError,
    BuildContext,
};
