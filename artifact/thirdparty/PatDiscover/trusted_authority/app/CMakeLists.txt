add_executable(pat_disc_ta
        src/main.cpp)

target_link_libraries(pat_disc_ta PRIVATE pat_disc_ta_lib)
target_precompile_headers(pat_disc_ta REUSE_FROM pat_disc_ta_lib)

if (APPLE)
    add_custom_command(TARGET pat_disc_ta POST_BUILD
            COMMAND codesign -s - -v -f --entitlements ${CMAKE_SOURCE_DIR}/entitlements/debug.plist ${CMAKE_CURRENT_BINARY_DIR}/pat_disc_ta)
endif ()

