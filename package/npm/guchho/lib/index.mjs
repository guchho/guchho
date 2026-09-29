// The public surface of the package, as an ES module.
//
// The implementation is CommonJS, which is what it has always been, and what
// keeps a package with no build step and no dependencies a package with no
// build step and no dependencies. This file is the whole of the ESM story: it
// is a named re-export of the CommonJS entry, so that
//
//     import * as guchho from "guchho";
//
// gives an object with transform and build on it as real named bindings,
// rather than a namespace whose only useful member is "default". Node's
// CommonJS interop detects named exports on its own, but it does so by
// guessing, and a guess is not something a public API should be built on.
//
// Every export is listed rather than spread, so that adding one to the CommonJS
// entry and forgetting it here is a missing export at the import site — a build
// that fails — instead of a silently undefined binding at runtime.

import mod from "./index.js";

export const {
    build,
    transform,
    context,
    analyze,
    stop,
    version,
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
    BuildFailure,
    ServiceError,
    BuildContext,
} = mod;

export default mod;