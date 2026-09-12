# ============================================================
# Guchho executable
# ============================================================

add_executable(guchho
    main.cpp
)

# target_link_libraries(guchho
#     PRIVATE
#         guchho_bundler
# )

guchho_set_warnings(guchho)
guchho_enable_sanitizers(guchho)

guchho_enable_resources(guchho)

set_target_properties(guchho PROPERTIES
    OUTPUT_NAME "guchho"
)