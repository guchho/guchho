"use strict";

// CommonJS wrapper over the ES module. The engine glue is ESM-only
// (-sEXPORT_ES6), so require() gets it through a dynamic import; the returned
// promise is the engine, which is asynchronous to create in any dialect.
//
// Every name the ES module publishes at top level is reachable here too, so
// `const { lexer } = require("@guchho/wasm")` works the same as the ESM import.
// The module is loaded once through the shared promise, and every wrapper
// attaches to that one promise, so calls run in the order they were made:
// configure() still lands before the build that follows it.

let modulePromise = null;

function load() {
    if (modulePromise === null) {
        modulePromise = import("./index.mjs");
    }
    return modulePromise;
}

function createGuchho(options) {
    return load().then((m) => m.createGuchho(options));
}

function runCli(argv) {
    return load().then((m) => m.runCli(argv));
}

function configure(options) {
    return load().then((m) => m.configure(options));
}

function loadFiles(files) {
    return load().then((m) => m.loadFiles(files));
}

function lexer(source, options) {
    return load().then((m) => m.lexer(source, options));
}

function parse(source, options) {
    return load().then((m) => m.parse(source, options));
}

function print(ast, options) {
    return load().then((m) => m.print(ast, options));
}

function build(entry, options) {
    return load().then((m) => m.build(entry, options));
}

function request(payload) {
    return load().then((m) => m.request(payload));
}

function version() {
    return load().then((m) => m.version());
}

module.exports = createGuchho;
module.exports.createGuchho = createGuchho;
module.exports.runCli = runCli;
module.exports.configure = configure;
module.exports.loadFiles = loadFiles;
module.exports.lexer = lexer;
module.exports.parse = parse;
module.exports.print = print;
module.exports.build = build;
module.exports.request = request;
module.exports.version = version;
module.exports.default = createGuchho;
