"use strict";

// The public surface of the package, as CommonJS.
//
// Four things are here and nothing else. transform() and build() are the API —
// one string in, one string out, and a project in, files out. getBinaryPath(),
// getPlatformKey() and spawnBinary() were already the package's public surface
// and stay, because a caller that has to find the binary to pass its own
// arguments to it has no other way to ask.
//
// The platform table is deliberately not re-exported. It is the inside of how
// the binary is found, and a caller that reads it to decide what to install is
// reading a private detail that changes whenever a platform is added.

const { getBinaryPath, getPlatformKey, spawnBinary } = require("./main");
const { transform } = require("./transform");
const { build } = require("./build");

module.exports = {
    getBinaryPath,
    getPlatformKey,
    spawnBinary,
    transform,
    build,
};
