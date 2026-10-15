"use strict";

// CommonJS wrapper over the ES module. The engine glue is ESM-only
// (-sEXPORT_ES6), so require() gets it through a dynamic import; the returned
// promise is the engine, which is asynchronous to create in any dialect.

function createGuchho(options) {
    return import("./index.mjs").then((m) => m.createGuchho(options));
}

function runCli(argv) {
    return import("./index.mjs").then((m) => m.runCli(argv));
}

module.exports = createGuchho;
module.exports.createGuchho = createGuchho;
module.exports.runCli = runCli;
module.exports.default = createGuchho;
