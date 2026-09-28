# npm (package resolution) test sources.
#
# One file per subject, in the order the resolution code reads a package:
# the node_modules walk, the "exports"/"imports" maps, the package.json fields,
# and symlink handling. The last one is the odd one out: it cannot use the
# shared mock file system, which cannot represent a symlink, so it carries its
# own and its comment says why.

add_library(guchho_npm_test_objects OBJECT
    npm_modules_test.cpp
    npm_exports_test.cpp
    npm_packagejson_test.cpp
    npm_symlink_test.cpp
)

target_include_directories(
    guchho_npm_test_objects
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(
    guchho_npm_test_objects
    PRIVATE
        guchho_resolver
        guchho_bundler
        guchho_common
)

guchho_set_warnings(guchho_npm_test_objects)
guchho_enable_sanitizers(guchho_npm_test_objects)
