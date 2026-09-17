
add_executable(pat_disc_server
    src/main.cpp)

target_precompile_headers(pat_disc_server REUSE_FROM pat_disc_server_lib)
target_link_libraries(pat_disc_server PUBLIC pat_disc_server_lib)
