"use strict";

// The one place a compiler-stage request and a service come from.
//
// The high-level API — build, transform, pack, analyze, context and the three
// compiler stages — is written once and runs against whichever engine the
// entry point registered here. lib/transport-node.js registers the native
// service (a child process over a pipe); lib/browser/index.js registers the
// WebAssembly engine running in process. Nothing in the orchestrators names
// either, which is what lets the same lib/build.js run in both.
//
// A module imported directly, without going through an entry point, has no
// transport and says so rather than reaching for a native binary a browser
// does not have.

let impl = null;

/** Installs the transport the orchestrators will use. Called by an entry point. */
function setTransport(next) {
    impl = next;
}

function requireTransport() {
    if (impl === null) {
        throw new Error(
            'no Guchho engine transport is configured. Import "guchho" (or the ' +
                'browser entry "guchho/lib/browser/index.js") rather than an ' +
                "internal module directly."
        );
    }
    return impl;
}

/** The engine connection, started on first use. */
function getService() {
    return requireTransport().getService();
}

/** One compiler-stage request, to whichever engine is available. */
function compilerRequest(payload) {
    return requireTransport().compilerRequest(payload);
}

/** The config file's plugins after the caller's, or null when there are none. */
function withConfigPlugins(startDir, callerPlugins) {
    return requireTransport().withConfigPlugins(startDir, callerPlugins);
}

/** Ends the shared engine, once it has actually ended. */
function stopService() {
    return requireTransport().stopService();
}

module.exports = {
    setTransport,
    getService,
    compilerRequest,
    withConfigPlugins,
    stopService,
};
