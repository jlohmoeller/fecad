
add_executable(pat_disc_server_test
        src/main.cpp
        src/math/MathTests.cpp)


find_package(GTest CONFIG REQUIRED)

target_link_libraries(pat_disc_server_test PRIVATE GTest::gtest)
target_link_libraries(pat_disc_server_test PRIVATE pat_disc_server_lib)
target_link_libraries(pat_disc_server_test PRIVATE pat_disc_ta_lib)
