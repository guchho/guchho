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

import mod from "./index.js";

export const { getBinaryPath, getPlatformKey, spawnBinary, transform, build } = mod;

export default mod;
