find_package(spdlog CONFIG REQUIRED)
find_package(boost_program_options CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(pat_disc_shared
        src/grpc/Chunking.cpp
        src/grpc/CostMetricInterceptor.cpp
        src/logging/Logger.cpp
        src/measure/CommunicationCost.cpp
        src/measure/ResultWriter.cpp
        src/measure/Timer.cpp
        src/model/MatchableAttribute.cpp
        src/util/Serialization.cpp
        src/util/Utilities.cpp
)

add_dependencies(pat_disc_shared openfhe_fetched)

get_target_property(openfhe_core_includes openfhe_external_core INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(openfhe_pke_includes openfhe_external_pke INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(openfhe_binfhe_includes openfhe_external_binfhe INTERFACE_INCLUDE_DIRECTORIES)


target_include_directories(pat_disc_shared PUBLIC include)
target_include_directories(pat_disc_shared PUBLIC ${openfhe_core_includes})
target_include_directories(pat_disc_shared PUBLIC ${openfhe_pke_includes})
target_include_directories(pat_disc_shared PUBLIC ${openfhe_binfhe_includes})

if (OPENFHE_USE_HEXL)
    get_target_property(hexl_includes hexl_external INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(pat_disc_shared PUBLIC hexl_includes)
endif()

target_link_libraries(pat_disc_shared PUBLIC pat_disc_proto)
target_link_libraries(pat_disc_shared PUBLIC openfhe_external_core)
target_link_libraries(pat_disc_shared PUBLIC openfhe_external_pke)
target_link_libraries(pat_disc_shared PUBLIC openfhe_external_binfhe)
target_link_libraries(pat_disc_shared PUBLIC spdlog::spdlog)
target_link_libraries(pat_disc_shared PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(pat_disc_shared PUBLIC Boost::program_options)
target_link_libraries(pat_disc_shared PUBLIC OpenSSL::Crypto)

if (OPENFHE_USE_HEXL)
    target_link_libraries(pat_disc_shared PUBLIC hexl_external)
endif()

target_precompile_headers(pat_disc_shared PRIVATE include/pdshared/pch.hpp)
