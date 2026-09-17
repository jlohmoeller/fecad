find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(pat_disc_server_lib
        src/config/CommandLineParameters.cpp
        src/data/DatabaseWrapper.cpp
        src/entity/Server.cpp
        src/grpc/PatientDataServerService.cpp
        src/grpc/TrustedAuthorityClient.cpp
        src/math/CiphertextPowers.cpp
        src/processing/QueryProcessing.cpp
        src/processing/QueryProcessingPrimitive.cpp)

add_dependencies(pat_disc_server_lib openfhe_fetched)

get_target_property(ntl_include_location ntl_external INTERFACE_INCLUDE_DIRECTORIES)

target_link_libraries(pat_disc_server_lib PUBLIC unofficial::sqlite3::sqlite3)
target_link_libraries(pat_disc_server_lib PUBLIC pat_disc_shared)
target_link_libraries(pat_disc_server_lib PUBLIC ntl_external)
target_link_libraries(pat_disc_server_lib PUBLIC gmp_external)

target_precompile_headers(pat_disc_server_lib PUBLIC include/pdserver/pch.hpp)
target_include_directories(pat_disc_server_lib PUBLIC include)
target_include_directories(pat_disc_server_lib PUBLIC ${ntl_include_location})
