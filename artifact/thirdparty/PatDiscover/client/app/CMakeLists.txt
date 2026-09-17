add_executable(pat_disc_client
        src/main.cpp)

target_precompile_headers(pat_disc_client REUSE_FROM pat_disc_client_lib)

target_link_libraries(pat_disc_client PRIVATE pat_disc_client_lib)