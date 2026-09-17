add_definitions(-DANTLR4CPP_STATIC)
include(ExternalAntlr4Cpp)

set(ANTLR_EXECUTABLE ${CMAKE_SOURCE_DIR}/dependencies/antlr4/jar/antlr-4.13.2-complete.jar)
find_package(ANTLR REQUIRED)

add_subdirectory(grammar)

add_library(pat_disc_client_lib
        src/config/CommandLineParameters.cpp
        src/data/PatientDataLoader.cpp
        src/data/QueryBuilder.cpp
        src/data/QueryParser.cpp
        src/entity/Client.cpp
        src/grpc/PatientDataServerClient.cpp
        src/grpc/TrustedAuthorityClient.cpp
        src/processing/ClientQueryProcessing.cpp
        ${ANTLR_SQLGrammarLexer_CXX_OUTPUTS}
        ${ANTLR_SQLGrammarParser_CXX_OUTPUTS})

add_dependencies(pat_disc_client_lib openfhe_fetched)
add_dependencies(pat_disc_client_lib sql_grammar)

target_link_libraries(pat_disc_client_lib PUBLIC pat_disc_shared)
target_link_libraries(pat_disc_client_lib PUBLIC antlr4_static)

target_include_directories(pat_disc_client_lib PUBLIC include)
target_include_directories(pat_disc_client_lib PUBLIC ${ANTLR4_INCLUDE_DIRS})
target_include_directories(pat_disc_client_lib PUBLIC ${ANTLR_SQLGrammarLexer_OUTPUT_DIR})
target_include_directories(pat_disc_client_lib PUBLIC ${ANTLR_SQLGrammarParser_OUTPUT_DIR})
target_precompile_headers(pat_disc_client_lib PRIVATE include/pdclient/pch.hpp)
