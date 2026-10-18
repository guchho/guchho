"use strict";

// The path a metafile is written to when the caller did not name one. Kept out
// of the project directory on purpose: it is a receipt for one run, not a build
// output, and a build that has to clean up after itself should not be the
// reason a file shows up beside the sources.
//
// This is the one part of lib/flags.js that needs the host file system, so it
// lives on its own here: flags.js stays importable in the browser, where there
// is no os.tmpdir() to reach for.

const path = require("path");

function defaultMetafilePath(tag) {
    return path.join(require("os").tmpdir(), `guchho-${tag}-${process.pid}.json`);
}

module.exports = { defaultMetafilePath };
