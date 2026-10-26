// The browser surface of the package, as an ES module.
//
// The implementation is CommonJS (lib/browser/index.js), for the same reason
// the Node entry is: the package has no build step. This file is the named
// re-export of it, so `import { lexer } from "guchho"` in a browser bundle is a
// real named binding rather than a namespace whose only useful member is
// "default".
//
// Every export is listed rather than spread, so adding one to the CommonJS
// entry and forgetting it here is a missing export at the import site.

import mod from "./index.js";

export const {
    build,
    transform,
    pack,
    lint,
    context,
    analyze,
    analyzeMetafile,
    version,
    lexer,
    parse,
    print,
    configure,
    loadFiles,
    watch,
    serve,
    unpack,
    stop,
    BuildFailure,
    ServiceError,
    BuildContext,
} = mod;

export default mod;
