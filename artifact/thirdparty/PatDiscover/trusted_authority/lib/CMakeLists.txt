add_library(pat_disc_ta_lib
        src/config/CommandLineParameters.cpp
        src/config/CryptoContextFactory.cpp
        src/entity/TrustedAuthority.cpp
        src/service/TrustedAuthorityService.cpp)

add_dependencies(pat_disc_ta_lib openfhe_fetched)

target_include_directories(pat_disc_ta_lib PUBLIC include)
target_link_libraries(pat_disc_ta_lib PUBLIC pat_disc_shared)

target_precompile_headers(pat_disc_ta_lib PUBLIC include/pdta/pch.hpp)