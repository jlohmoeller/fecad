# Copyright (C) 2019-2020 IBM Corp.
#
# This program is Licensed under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#   http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License. See accompanying LICENSE file.

# Modified original file (taken from https://github.com/homenc/HElib/blob/master/dependencies/gmp/CMakeLists.txt) to
# compile GMP into a static library

include(ExternalProject)

set(fetched_gmp_install_prefix "${CMAKE_CURRENT_BINARY_DIR}")

file(MAKE_DIRECTORY "${fetched_gmp_install_prefix}/include")

# Path of the gmp library file that will be installed (libgmp.so/dylib)
set(fetched_gmp_lib "${fetched_gmp_install_prefix}/lib/libgmp${CMAKE_STATIC_LIBRARY_SUFFIX}")

# Path of the gmp include directory that will be installed
set(fetched_gmp_includedir "${fetched_gmp_install_prefix}/include")

ExternalProject_Add(
        gmp_fetched
        URL "https://gmplib.org/download/gmp/gmp-${FETCHED_GMP_VERSION}.tar.bz2"
        "https://ftp.gnu.org/gnu/gmp/gmp-${FETCHED_GMP_VERSION}.tar.bz2"
        URL_HASH
        # NOTE: hash of version 6.3.0
        SHA256=ac28211a7cfb609bae2e2c8d6058d66c8fe96434f740cf6fe2e47b000d1c20cb
        DOWNLOAD_NO_PROGRESS 1
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND
        <SOURCE_DIR>/configure
        "--prefix=${fetched_gmp_install_prefix}"
        "--disable-shared"
        "--with-readline=no"
        # Propagate cmake chosen compiler.
        # Note must add the macOS isysroot flag here instead of passing
        # CXXFLAGS as that overrides the external project optimize flags.
        "CXX=${CMAKE_CXX_COMPILER} ${macos_isysroot_flag}"
        "CC=${CMAKE_C_COMPILER} ${macos_isysroot_flag}"
        BUILD_IN_SOURCE ON
        TEST_AFTER_INSTALL OFF
        TEST_EXCLUDE_FROM_MAIN ON
        # TEST_COMMAND
        #     ${CMAKE_MAKE_PROGRAM} check
        # Turn LOG_CONFIGURE ON will log the configure step to a file in the stamp directory
        # LOG_CONFIGURE ON
        # Turn LOG_BUILD ON will log the build step to a file in the stamp directory
        # LOG_BUILD ON
        # No build command specified ==> use the default one
        # BUILD_COMMAND
        # No install command specified ==> build the install target (e.g. make install)
        # INSTALL_COMMAND
        # Turn LOG_INSTALL ON will log the install step to a file in the stamp directory
        # LOG_INSTALL ON
        # Turn LOG_TEST ON will log the test step to a file in the stamp directory
        # LOG_TEST ON
        DOWNLOAD_EXTRACT_TIMESTAMP NEW
        BUILD_BYPRODUCTS "${fetched_gmp_lib}"
)

add_dependencies(gmp_external gmp_fetched)

# Add include dirs and library path as properties of gmp_external imported
# target to propagate them above
set_target_properties(
        gmp_external
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
        "${fetched_gmp_install_prefix}/include"
        IMPORTED_LOCATION "${fetched_gmp_lib}")

# Unset temporary variables
unset(fetched_gmp_install_prefix)
unset(fetched_gmp_byproducts)
unset(fetched_gmp_lib)
unset(fetched_gmp_includedir)