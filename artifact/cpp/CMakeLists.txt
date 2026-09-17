cmake_minimum_required(VERSION 3.16)
project(fecad_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Required for linking static libs (TFHEpp, SEAL, HEDB) into shared libfecad_bridge.so
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Opt-in AddressSanitizer / UBSan for the bridge .so files. Use with
# `cmake -DFECAD_SANITIZE=address` (or `address,undefined`). The Go side picks
# this up automatically via `go test -asan`. Catches ODR violations across
# shared libraries — exactly how the 2026-05-26 destructor SIGSEGV (different
# Condition layouts across fecad/engorgio/patdiscover bridges) was found.
if(FECAD_SANITIZE)
    add_compile_options(-fsanitize=${FECAD_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${FECAD_SANITIZE})
endif()

# HE3DB location
# Default: thirdparty/HE3DB lives one level above this file
# (fecad/cpp/ -> fecad/ -> paper root -> fecad/thirdparty/HE3DB).
if(NOT DEFINED HE3DB_DIR)
    set(HE3DB_DIR "${CMAKE_SOURCE_DIR}/../fecad/thirdparty/HE3DB"
        CACHE PATH "Path to HE3DB source (contains its own CMakeLists.txt)")
endif()

if(NOT EXISTS "${HE3DB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "HE3DB not found at ${HE3DB_DIR}.\n"
        "Initialize the submodule:\n"
        "  cd fecad && git submodule update --init thirdparty/HE3DB\n"
        "or pass -DHE3DB_DIR=/absolute/path/to/HE3DB to cmake.")
endif()

# Engorgio location (includes its OpenFHE submodule)
if(NOT DEFINED ENGORGIO_DIR)
    set(ENGORGIO_DIR "${CMAKE_SOURCE_DIR}/../fecad/thirdparty/Engorgio"
        CACHE PATH "Path to Engorgio source (contains its own CMakeLists.txt)")
endif()

if(NOT DEFINED OPENFHE_DIR)
    set(OPENFHE_DIR "${ENGORGIO_DIR}/thirdparty/openfhe-development"
        CACHE PATH "Path to OpenFHE source (contains its own CMakeLists.txt)")
endif()

# Discover OpenMP before OpenFHE so its WITH_OPENMP matches what we link.
find_package(OpenMP)
if(OpenMP_FOUND)
    set(WITH_OPENMP ON CACHE BOOL "" FORCE)
else()
    set(WITH_OPENMP OFF CACHE BOOL "" FORCE)
endif()

# OpenFHE build tuning — must be set before add_subdirectory().
#   WITH_NATIVEOPT : OpenFHE subdir does not inherit our -march=native.
#   WITH_INTEL_HEXL: AVX512 NTT acceleration (IFMA52 on Ice Lake+, DQ path on
#                    Skylake). OpenFHE fetches HEXL itself.
#   BUILD_*        : skip OpenFHE's own tests/examples/benchmarks (build time).
set(WITH_NATIVEOPT  ON  CACHE BOOL "" FORCE)
set(WITH_INTEL_HEXL ON  CACHE BOOL "" FORCE)
set(BUILD_UNITTESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

set(HAVE_OPENFHE OFF)
if(EXISTS "${OPENFHE_DIR}/CMakeLists.txt")
    add_subdirectory(${OPENFHE_DIR} openfhe_build)
    set(HAVE_OPENFHE ON)
else()
    message(STATUS "OpenFHE not found at ${OPENFHE_DIR}; Engorgio targets will be skipped.")
endif()

# Remove prebuilt static libs — they may be built for a different CPU.
file(GLOB _PREBUILT "${HE3DB_DIR}/lib/*.a")
if(_PREBUILT)
    message(STATUS "Removing prebuilt HE3DB libs (will rebuild locally): ${_PREBUILT}")
    file(REMOVE ${_PREBUILT})
endif()

# Disable SEAL's bundled zlib — too old for Apple Clang 16+ / modern glibc.
set(SEAL_USE_ZLIB OFF CACHE BOOL "" FORCE)
# Zstd ciphertext compression — smaller blobs to BeeGFS (SEAL_BUILD_DEPS fetches it).
set(SEAL_USE_ZSTD ON CACHE BOOL "" FORCE)
# Intel HEXL — AVX512 NTT acceleration for SEAL (HE3DB backend).
set(SEAL_USE_INTEL_HEXL ON CACHE BOOL "" FORCE)

# TFHEpp FFT backend: the default spqlios uses x86-64 FMA assembly which
# does not assemble on ARM64 (Apple Silicon).  Use FFTW3 on ARM64 instead;
# spqliox_aarch64 requires xbyak_aarch64 which is not bundled.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(USE_FFTW3 ON CACHE BOOL "" FORCE)
    # Randen CSPRNG requires __ARM_FEATURE_CRYPTO which Apple Clang does not
    # enable automatically even with -march=native.  Use /dev/urandom instead.
    set(USE_RANDEN OFF CACHE BOOL "" FORCE)
    # Help CMake find Homebrew-installed FFTW3 on macOS.
    list(APPEND CMAKE_PREFIX_PATH /opt/homebrew)

    # 'uint' is a GNU/glibc extension absent from Apple SDK headers.
    # TFHEpp and HEDB use it; force-include a one-liner compat shim globally.
    set(_UINT_COMPAT "${CMAKE_BINARY_DIR}/uint_compat.h")
    file(WRITE "${_UINT_COMPAT}" "typedef unsigned int uint;\n")
    add_compile_options("-include${_UINT_COMPAT}")
endif()

add_subdirectory(${HE3DB_DIR} he3db_build)

# TFHEpp's fftwproc target never calls find_package — patch in the FFTW3
# headers and library after the subdirectory is configured.
if(USE_FFTW3)
    find_path(FFTW3_INCLUDE_DIR fftw3.h
        HINTS /opt/homebrew/include /usr/local/include /usr/include)
    find_library(FFTW3_LIB fftw3
        HINTS /opt/homebrew/lib /usr/local/lib /usr/lib)
    if(NOT FFTW3_INCLUDE_DIR OR NOT FFTW3_LIB)
        message(FATAL_ERROR
            "FFTW3 not found. Install it:\n"
            "  macOS:  brew install fftw\n"
            "  Ubuntu: apt install libfftw3-dev")
    endif()
    target_include_directories(fftwproc PUBLIC ${FFTW3_INCLUDE_DIR})
    target_link_libraries(fftwproc PUBLIC ${FFTW3_LIB})
    # tfhe++ links fftwproc as INTERFACE, so fftwproc's PUBLIC includes only
    # reach consumers, not tfhe++'s own compilation units — add directly.
    target_include_directories(tfhe++ PUBLIC ${FFTW3_INCLUDE_DIR})
    # tfhe++ also emits -lfftw3 as an INTERFACE link dep; add the lib dir to
    # the linker search path so downstream targets (fecad_bridge, executables)
    # can resolve it.
    get_filename_component(FFTW3_LIB_DIR "${FFTW3_LIB}" DIRECTORY)
    link_directories("${FFTW3_LIB_DIR}")
endif()

# nlohmann/json is header-only; fetch it rather than requiring a system install.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# OpenMP discovered above (before OpenFHE add_subdirectory).

# Expose HE3DB + project headers to all targets via the HEDB interface target.
target_include_directories(HEDB PUBLIC
    ${HE3DB_DIR}/src/HEDB
    ${HE3DB_DIR}/src/HEDB/comparison
    ${HE3DB_DIR}/src/HEDB/utils
    ${CMAKE_SOURCE_DIR}/src
)

# shared library for CGo
add_library(fecad_bridge SHARED
    src/fecad_bridge.cpp
    src/utils/serialize.cpp src/utils/fsync_util.cpp
)
target_link_libraries(fecad_bridge PRIVATE HEDB nlohmann_json::nlohmann_json)
if(OpenMP_FOUND)
    target_link_libraries(fecad_bridge PRIVATE OpenMP::OpenMP_CXX)
endif()
set_target_properties(fecad_bridge PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # Windows DLL location

# standalone executables (subprocess / he3db backend)
add_executable(client src/client.cpp src/utils/serialize.cpp src/utils/fsync_util.cpp)
target_link_libraries(client PRIVATE HEDB nlohmann_json::nlohmann_json)
set_target_properties(client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(he3db_wrapper src/he3db_wrapper.cpp src/utils/serialize.cpp src/utils/fsync_util.cpp)
target_link_libraries(he3db_wrapper PRIVATE HEDB nlohmann_json::nlohmann_json)
if(OpenMP_FOUND)
    target_link_libraries(he3db_wrapper PRIVATE OpenMP::OpenMP_CXX)
endif()
set_target_properties(he3db_wrapper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(server src/server.cpp src/utils/serialize.cpp src/utils/fsync_util.cpp)
target_link_libraries(server PRIVATE HEDB nlohmann_json::nlohmann_json)
set_target_properties(server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Engorgio (OpenFHE) targets
if(HAVE_OPENFHE AND EXISTS "${ENGORGIO_DIR}/src/ENGORGIO/comp.cpp")
    add_library(engorgio_core STATIC
        ${ENGORGIO_DIR}/src/ENGORGIO/comp.cpp
        ${ENGORGIO_DIR}/src/ENGORGIO/quant.cpp
        ${ENGORGIO_DIR}/src/ENGORGIO/utils.cpp
    )
    target_include_directories(engorgio_core
        PUBLIC ${OPENFHE_DIR}/src/core/include
        PUBLIC ${OPENFHE_DIR}/src/pke/include
        PUBLIC ${OPENFHE_DIR}/src/binfhe/include
        PUBLIC ${OPENFHE_DIR}/third-party/cereal/include
        PUBLIC ${CMAKE_BINARY_DIR}/openfhe_build/src/core
        PUBLIC ${ENGORGIO_DIR}/src
    )
    target_link_libraries(engorgio_core
        PUBLIC OPENFHEcore OPENFHEpke OPENFHEbinfhe
    )

    # shared library for CGo (engorgio backend)
    add_library(engorgio_bridge SHARED
        src/engorgio_bridge.cpp
        src/engorgio_io.cpp
        src/utils/fsync_util.cpp
    )
    target_include_directories(engorgio_bridge PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(engorgio_bridge PRIVATE engorgio_core nlohmann_json::nlohmann_json)
    if(OpenMP_FOUND)
        target_link_libraries(engorgio_bridge PRIVATE OpenMP::OpenMP_CXX)
    endif()
    set_target_properties(engorgio_bridge PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

    add_executable(engorgio_client src/engorgio_client.cpp src/engorgio_io.cpp src/utils/fsync_util.cpp)
    target_link_libraries(engorgio_client PRIVATE engorgio_core nlohmann_json::nlohmann_json)
    set_target_properties(engorgio_client PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    add_executable(engorgio_wrapper src/engorgio_wrapper.cpp src/engorgio_io.cpp src/utils/fsync_util.cpp)
    target_link_libraries(engorgio_wrapper PRIVATE engorgio_core nlohmann_json::nlohmann_json)
    set_target_properties(engorgio_wrapper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    add_executable(engorgio_server src/engorgio_server.cpp src/engorgio_io.cpp src/utils/fsync_util.cpp)
    target_link_libraries(engorgio_server PRIVATE engorgio_core nlohmann_json::nlohmann_json)
    set_target_properties(engorgio_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

# PatDiscover (OpenFHE CKKS + PRE) targets
#
# Real upstream binding: we build pat_disc_server_lib's predicate primitives
# (QueryProcessing.cpp + CiphertextPowers.cpp) directly into a static
# patdiscover_core library, then call them from patdiscover_bridge.cpp the same
# way engorgio_bridge calls engorgio_core. NTL is taken from the system
# (apt: libntl-dev). Logger/Timer/Utilities/CommandLineParameters are stubbed
# by the headers under src/patdiscover_shim so the heavy spdlog/boost/protobuf
# transitive dependencies of pat_disc_shared stay out of our build.
if(NOT DEFINED PATDISCOVER_DIR)
    set(PATDISCOVER_DIR "${CMAKE_SOURCE_DIR}/../thirdparty/PatDiscover"
        CACHE PATH "Path to thirdparty/PatDiscover (contains server/lib + data/polynomials)")
endif()

if(HAVE_OPENFHE AND EXISTS "${PATDISCOVER_DIR}/server/lib/src/processing/QueryProcessing.cpp")
    find_library(NTL_LIB ntl)
    find_path(NTL_INCLUDE_DIR NTL/ZZX.h)
    find_library(GMP_LIB gmp)
    if(NOT NTL_LIB OR NOT NTL_INCLUDE_DIR OR NOT GMP_LIB)
        message(FATAL_ERROR
            "PatDiscover binding requires NTL and GMP. Install them:\n"
            "  sudo apt install libntl-dev libgmp-dev\n"
            "or set NTL_LIB, NTL_INCLUDE_DIR, GMP_LIB manually.")
    endif()

    # Static lib carrying the upstream predicate math (CKKS approx + Boolean +
    # BFV precise; only the CKKS approx path is exercised by the bridge but
    # the cpp file builds wholesale to stay byte-identical to upstream).
    add_library(patdiscover_core STATIC
        ${PATDISCOVER_DIR}/server/lib/src/processing/QueryProcessing.cpp
        ${PATDISCOVER_DIR}/server/lib/src/math/CiphertextPowers.cpp
    )
    target_include_directories(patdiscover_core PUBLIC
        ${CMAKE_SOURCE_DIR}/src/patdiscover_shim
        ${PATDISCOVER_DIR}/server/lib/include
        ${PATDISCOVER_DIR}/shared/include
        ${NTL_INCLUDE_DIR}
        ${OPENFHE_DIR}/src/core/include
        ${OPENFHE_DIR}/src/pke/include
        ${OPENFHE_DIR}/src/binfhe/include
        ${OPENFHE_DIR}/third-party/cereal/include
        ${CMAKE_BINARY_DIR}/openfhe_build/src/core
    )
    # Force-include the prelude so upstream headers see OpenFHE + the
    # CryptoContext/Ciphertext aliases that their PCH would normally provide.
    target_compile_options(patdiscover_core PUBLIC
        -include ${CMAKE_SOURCE_DIR}/src/patdiscover_shim/patdiscover_prelude.hpp
    )
    target_link_libraries(patdiscover_core PUBLIC
        OPENFHEcore OPENFHEpke OPENFHEbinfhe ${NTL_LIB} ${GMP_LIB}
    )

    add_library(patdiscover_bridge SHARED
        src/patdiscover_bridge.cpp
        src/patdiscover_io.cpp
        src/utils/fsync_util.cpp
    )
    target_include_directories(patdiscover_bridge PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${OPENFHE_DIR}/src/core/include
        ${OPENFHE_DIR}/src/pke/include
        ${OPENFHE_DIR}/src/binfhe/include
        ${OPENFHE_DIR}/third-party/cereal/include
        ${CMAKE_BINARY_DIR}/openfhe_build/src/core
    )
    target_compile_definitions(patdiscover_bridge PRIVATE
        PATDISCOVER_DEFAULT_POLY_DIR="${PATDISCOVER_DIR}/data/polynomials"
    )
    target_link_libraries(patdiscover_bridge PRIVATE
        patdiscover_core nlohmann_json::nlohmann_json
    )
    if(OpenMP_FOUND)
        target_link_libraries(patdiscover_bridge PRIVATE OpenMP::OpenMP_CXX)
    endif()
    set_target_properties(patdiscover_bridge PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif()
