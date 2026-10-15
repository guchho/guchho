// Runs inside the module closure after the JS libraries, so NODEFS is in
// scope here. libfs.js only registers it in FS.filesystems when it was linked
// as a system library (NODERAWFS); we pull it in via --js-library instead, so
// the registration has to happen by hand.
FS.filesystems.NODEFS = NODEFS;
Module['NODEFS'] = NODEFS;

// On Windows, lstat on protected entries (System Volume Information,
// hiberfil.sys, etc.) throws EPERM. getdents64 calls FS.lookupNode for
// every name returned by readdirSync, and only skips entries whose lookup
// fails with EINVAL (errno 28); any other error (e.g. EPERM, errno 63)
// aborts the entire directory read. Convert those errors to EINVAL so
// getdents64 skips the entry instead of failing the whole read.
var _origLookupNode = FS.lookupNode;
FS.lookupNode = function (parent, name) {
  try {
    return _origLookupNode(parent, name);
  } catch (e) {
    if (e && e.errno === 63) {
      e.errno = 28;
    }
    throw e;
  }
};
