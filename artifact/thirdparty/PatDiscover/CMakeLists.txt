cmake_minimum_required(VERSION 3.22)

project(pat_disc C CXX)
set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/antlr4")

if (APPLE OR UNIX)
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
    set(CMAKE_HAVE_THREADS_LIBRARY 1)
    set(CMAKE_USE_WIN32_THREADS_INIT 0)
    set(CMAKE_USE_PTHREADS_INIT 1)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
endif ()

set(OPENFHE_USE_HEXL 1)
option(PD_EVAL_MODE "Include evaluation functions" ON)

add_compile_definitions("PD_DEBUG=$<CONFIG:Debug>" "PD_EVAL=$<BOOL:${PD_EVAL_MODE}>")
set(CMAKE_CXX_FLAGS "-DOPENFHE_VERSION=1.5.1 -DMATHBACKEND=4")

add_library(gmp_external STATIC IMPORTED)
add_library(ntl_external STATIC IMPORTED)

add_library(openfhe_external_core SHARED IMPORTED)
add_library(openfhe_external_binfhe SHARED IMPORTED)
add_library(openfhe_external_pke SHARED IMPORTED)
add_library(hexl_external SHARED IMPORTED)

add_subdirectory(dependencies)

add_subdirectory(proto)
add_subdirectory(shared)
add_subdirectory(trusted_authority)
add_subdirectory(client)
add_subdirectory(server)
