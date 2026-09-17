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

# Modified original file (taken from https://github.com/homenc/HElib/blob/master/dependencies/ntl/CMakeLists.txt) to
# compile NTL into a static library

include(ExternalProject)

set(fetched_ntl_install_prefix "${CMAKE_CURRENT_BINARY_DIR}")
set(build_threads "off")

file(MAKE_DIRECTORY "${fetched_ntl_install_prefix}/include")

# Wait until the directory structure and links are properly created
set(ntl_superbuild_dependencies "gmp_fetched")

# get the file path of libgmp.(so|dylib) (set beforehand)
get_target_property(gmp_lib_path gmp_external IMPORTED_LOCATION)
# get the dirname of libgmp library (in <prefix>/lib)
get_filename_component(gmp_lib_dir "${gmp_lib_path}" DIRECTORY)
# remove the lib directory to get the prefix
get_filename_component(gmp_prefix "${gmp_lib_dir}" DIRECTORY)

# NOTE: Workaround for the way NTL builds on Linux.
# Without explicitly pointing the linker at the correct GMP, NTL has a bad habit
# of picking up the system's libgmp. This causes erroneous version 'mismatches'.
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    set(ntl_make_libpath_tweak_cmd
            "export LD_LIBRARY_PATH=${gmp_lib_dir}:$LD_LIBRARY_PATH")
else ()
    set(ntl_make_libpath_tweak_cmd ":")
endif ()

# Path of the ntl library file that will be installed (libntl.a)
set(fetched_ntl_lib "${fetched_ntl_install_prefix}/lib/libntl${CMAKE_STATIC_LIBRARY_SUFFIX}")

# Path of the ntl include directory that will be installed
set(fetched_ntl_includedir "${fetched_ntl_install_prefix}/include")

ExternalProject_Add(
        ntl_fetched
        DEPENDS "${ntl_superbuild_dependencies}"
        URL "https://libntl.org/ntl-${FETCHED_NTL_VERSION}.tar.gz"
        "https://www.shoup.net/ntl/ntl-${FETCHED_NTL_VERSION}.tar.gz"
        URL_HASH
        # NOTE: hash of version 11.6.0
        SHA256=bc0ef9aceb075a6a0673ac8d8f47d5f8458c72fe806e4468fbd5d3daff056182
        DOWNLOAD_NO_PROGRESS ON
        UPDATE_COMMAND ""
        # Turn LOG_CONFIGURE ON will log the configure step to a file in the stamp directory
        # LOG_CONFIGURE ON
        # SOURCE_SUBDIR <SOURCE_DIR>/src
        CONFIGURE_COMMAND
        cd src && <SOURCE_DIR>/src/configure
        "PREFIX=${fetched_ntl_install_prefix}"
        "NTL_GMP_LIP=on"
        "NTL_THREADS=${build_threads}"
        "NTL_THREAD_BOOST=${build_threads}"
        "GMP_PREFIX=${gmp_prefix}"
        # Propagate cmake chosen compiler.
        # Note must add the macOS isysroot flag here instead of passing
        # CXXFLAGS as that overrides the external project optimize flags.
        "CXX=${CMAKE_CXX_COMPILER} ${macos_isysroot_flag}"
        BUILD_IN_SOURCE ON
        BUILD_COMMAND
        cd src &&
        # Adding ${gmp_lib_dir} to LD_LIBRARY_PATH (on Linux) to run program
        # testing GMP during make phase (see note at line 41)
        eval "${ntl_make_libpath_tweak_cmd}" &&
        # Using $(MAKE) set this build as sub-task, so all make
        # arguments (including -j) are forwarded
        make
        INSTALL_COMMAND cd src && make install
        TEST_AFTER_INSTALL OFF
        TEST_EXCLUDE_FROM_MAIN ON
        # Turn LOG_BUILD ON will log the build step to a file in the stamp directory
        # LOG_BUILD ON
        # Turn LOG_INSTALL ON will log the install step to a file in the stamp directory
        # LOG_INSTALL ON
        # Turn LOG_TEST ON will log the test step to a file in the stamp directory
        # LOG_TEST ON
        DOWNLOAD_EXTRACT_TIMESTAMP NEW
        BUILD_BYPRODUCTS "${fetched_ntl_lib}"
)

add_dependencies(ntl_external ntl_fetched)

# Add include dirs and library path as properties of ntl_external imported
# target to propagate them above
set_target_properties(
        ntl_external
        PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
        "${fetched_ntl_install_prefix}/include"
        IMPORTED_LOCATION "${fetched_ntl_lib}")

# Unset temporary variables
unset(build_threads)
unset(fetched_ntl_install_prefix)
unset(fetched_ntl_byproducts)
unset(fetched_ntl_lib)
unset(fetched_ntl_includedir)
unset(ntl_make_libpath_tweak_cmd)
unset(ntl_superbuild_dependencies)
unset(gmp_prefix)
unset(gmp_lib_dir)
unset(gmp_lib_path)