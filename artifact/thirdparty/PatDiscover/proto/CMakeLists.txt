set(PROTO_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PROTO_OUT_DIR "${PROTO_BINARY_DIR}/pdproto")
set(PROTO_IMPORT_DIR "${CMAKE_CURRENT_BINARY_DIR}/processed-proto/")

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Perform preprocessing on proto files (Debug / Release configs)
if (MSVC)
    # Use MSVC preprocessing
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/data_transfer_objects.proto ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/services.proto
            DEPENDS src/services.proto src/data_transfer_objects.proto
            COMMAND cl /D PD_EVAL=$<BOOL:${PD_EVAL_MODE}> /D PD_DEBUG=$<CONFIG:Debug> /E ${CMAKE_CURRENT_SOURCE_DIR}/src/data_transfer_objects.proto > ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/data_transfer_objects.proto
            COMMAND cl /D PD_EVAL=$<BOOL:${PD_EVAL_MODE}> /D PD_DEBUG=$<CONFIG:Debug> /E ${CMAKE_CURRENT_SOURCE_DIR}/src/services.proto > ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/services.proto
            VERBATIM
    )
else ()
    # Use c preprocessor (cpp)
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/data_transfer_objects.proto ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/services.proto
            DEPENDS src/services.proto src/data_transfer_objects.proto
            COMMAND cpp -DPD_EVAL=$<BOOL:${PD_EVAL_MODE}> -DPD_DEBUG=$<CONFIG:Debug> -P ${CMAKE_CURRENT_SOURCE_DIR}/src/data_transfer_objects.proto > ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/data_transfer_objects.proto
            COMMAND cpp -DPD_EVAL=$<BOOL:${PD_EVAL_MODE}> -DPD_DEBUG=$<CONFIG:Debug> -P ${CMAKE_CURRENT_SOURCE_DIR}/src/services.proto > ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/services.proto
            VERBATIM
    )
endif ()


add_custom_target(proto_preprocessed DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/data_transfer_objects.proto ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/services.proto)

add_library(pat_disc_proto OBJECT
        ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/data_transfer_objects.proto
        ${CMAKE_CURRENT_BINARY_DIR}/processed-proto/services.proto)
add_dependencies(pat_disc_proto proto_preprocessed)

target_include_directories(pat_disc_proto PUBLIC "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>")
target_link_libraries(pat_disc_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)

protobuf_generate(TARGET pat_disc_proto PROTOC_OUT_DIR ${PROTO_OUT_DIR} IMPORT_DIRS ${PROTO_IMPORT_DIR})
protobuf_generate(
        TARGET pat_disc_proto
        LANGUAGE grpc
        GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
        PLUGIN "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
        PROTOC_OUT_DIR ${PROTO_OUT_DIR}
        IMPORT_DIRS ${PROTO_IMPORT_DIR}
)