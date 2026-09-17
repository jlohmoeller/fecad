include(ExternalProject)

set(openfhe_install_prefix "${CMAKE_CURRENT_BINARY_DIR}")

file(MAKE_DIRECTORY "${openfhe_install_prefix}/include")
file(MAKE_DIRECTORY "${openfhe_install_prefix}/include/openfhe/core")
file(MAKE_DIRECTORY "${openfhe_install_prefix}/include/openfhe/core/include")
file(MAKE_DIRECTORY "${openfhe_install_prefix}/include/openfhe/pke")
file(MAKE_DIRECTORY "${openfhe_install_prefix}/include/openfhe/binfhe")
file(MAKE_DIRECTORY "${openfhe_install_prefix}/include/hexl")
file(TOUCH "${openfhe_install_prefix}/include/openfhe/pke/openfhe.h")

# See https://openfhe-development.readthedocs.io/en/latest/sphinx_rsts/intro/installation/macos.html
if ((APPLE OR (CMAKE_CXX_PLATFORM_ID STREQUAL "Darwin")))
    set(cmake_flags "-DWITH_OPENMP=OFF -DWITH_NATIVEOPT=ON -DCMAKE_CROSSCOMPILING=1 -DRUN_HAVE_STD_REGEX=0 -DRUN_HAVE_POSIX_REGEX=0")
else()
    set(cmake_flags "-DWITH_OPENMP=OFF -DWITH_NATIVEOPT=ON")
endif()

if (OPENFHE_USE_HEXL)
    set(stage_script "stage-openfhe-development-hexl")
else ()
    set(stage_script "stage-openfhe-development")
endif ()

set(fetched_core_lib "${openfhe_install_prefix}/lib/libOPENFHEcore${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(fetched_pke_lib "${openfhe_install_prefix}/lib/libOPENFHEpke${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(fetched_binfhe_lib "${openfhe_install_prefix}/lib/libOPENFHEbinfhe${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(hexl_lib "${openfhe_install_prefix}/lib/libhexl${CMAKE_SHARED_LIBRARY_SUFFIX}")

if (OPENFHE_USE_HEXL)
    set(fetched_openfhe_byproducts "${fetched_core_lib}" "${fetched_pke_lib}" "${fetched_binfhe_lib}" "${hexl_lib}")
else ()
    set(fetched_openfhe_byproducts "${fetched_core_lib}" "${fetched_pke_lib}" "${fetched_binfhe_lib}")
endif ()

ExternalProject_Add(
        openfhe_fetched
        GIT_REPOSITORY "https://github.com/openfheorg/openfhe-configurator.git"
        GIT_TAG "main"
        DOWNLOAD_NO_PROGRESS ON
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND
        OPENFHE_INSTALL_DIR=${openfhe_install_prefix} <SOURCE_DIR>/scripts/${stage_script}.sh
        BUILD_IN_SOURCE ON
        BUILD_COMMAND
        ${CMAKE_COMMAND} -E env OPENFHE_INSTALL_DIR=${openfhe_install_prefix} CMAKE_FLAGS=${cmake_flags}
        CC="${CMAKE_C_COMPILER}" CXX="${CMAKE_CXX_COMPILER}" -- <SOURCE_DIR>/scripts/build-openfhe-development.sh
        INSTALL_COMMAND ""
        TEST_AFTER_INSTALL OFF
        TEST_EXCLUDE_FROM_MAIN ON
        BUILD_BYPRODUCTS "${fetched_openfhe_byproducts}"
)

add_dependencies(openfhe_external_core openfhe_fetched)
add_dependencies(openfhe_external_pke openfhe_fetched)
add_dependencies(openfhe_external_binfhe openfhe_fetched)
add_dependencies(hexl_external openfhe_fetched)

set(core_lib_includes
        "${openfhe_install_prefix}/include"
        "${openfhe_install_prefix}/include/openfhe"
        "${openfhe_install_prefix}/include/openfhe/core"
        "${openfhe_install_prefix}/include/openfhe/core/include"
)

set_target_properties(
        openfhe_external_core
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
        "${core_lib_includes}"
        IMPORTED_LOCATION "${fetched_core_lib}")

set_target_properties(
        openfhe_external_pke
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
        "${openfhe_install_prefix}/include/openfhe/pke"
        IMPORTED_LOCATION "${fetched_pke_lib}")

set_target_properties(
        openfhe_external_binfhe
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
        "${openfhe_install_prefix}/include/openfhe/binfhe"
        IMPORTED_LOCATION "${fetched_binfhe_lib}")

set_target_properties(
        hexl_external
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
        "${openfhe_install_prefix}/include/hexl"
        IMPORTED_LOCATION "${hexl_lib}"
)

# Unset temporary variables
unset(openfhe_install_prefix)
unset(cmake_flags)
unset(stage_script)
unset(fetched_core_lib)
unset(fetched_pke_lib)
unset(fetched_binfhe_lib)
unset(fetched_openfhe_byproducts)
unset(core_lib_includes)
