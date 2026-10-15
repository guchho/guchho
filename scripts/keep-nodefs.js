// Linked into the wasm build so Emscripten does not dead-code-eliminate
// NODEFS. Nothing in the C++ engine mounts it — the Node wrapper does, after
// instantiation, via FS.filesystems.NODEFS — so without this anchor the link
// drops libnodefs.js and FS.filesystems only contains MEMFS.
mergeInto(LibraryManager.library, {
  guchho_keep_nodefs__deps: ['$NODEFS'],
  guchho_keep_nodefs: function () {},
});
