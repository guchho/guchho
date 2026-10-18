"use strict";

// One registry: functions run when the shared service process ends.
//
// compile.js keeps a map of handle→language so print() can name the language a
// handle belongs to. That map is only valid while the service is alive — the
// service resets its id counter when it restarts, so a handle from the previous
// service is not the tree the caller thinks it is.
//
// Clearing the map needs a signal from the service's own teardown. The service
// is lower in the module graph than compile (compile requires transport, which
// transport-node requires service for; the browser transport never loads
// service at all), so a direct require from service into compile would invert
// the layering and leak a child_process-spawning module into the browser graph.
//
// This registry is the seam: a leaf module both can require without a cycle,
// and the browser never reaches because the browser graph's stop is
// transport-wasm's no-op. The wasm engine persists across a native stop — its
// handles remain valid — so hooks registered for native teardown never run
// there, which is the correct behavior.

const hooks = [];

/** Registers a function to run when the native service process ends. */
function addStopHook(fn) {
    hooks.push(fn);
}

/** Runs every registered hook. Clearing a map twice is safe; exceptions are not. */
function runStopHooks() {
    for (const fn of hooks) {
        try {
            fn();
        } catch {
            // A hook that throws must not prevent the remaining hooks from
            // running, or mask the original teardown error.
        }
    }
}

module.exports = { addStopHook, runStopHooks };
