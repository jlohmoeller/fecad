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

# Modified original file (taken from https://github.com/homenc/HElib/blob/master/dependencies/CMakeLists.txt) to include OpenFHE

# The -isysroot flag needs set.
if ((APPLE OR (CMAKE_CXX_PLATFORM_ID STREQUAL "Darwin")) AND
        # This addresses the problem when CMAKE_OSX_SYSROOT is "" (e.g. when /usr/include/sys/types.h exists)
        NOT CMAKE_OSX_SYSROOT STREQUAL "")
    set(macos_isysroot_flag "-isysroot ${CMAKE_OSX_SYSROOT}")
else()
    set(macos_isysroot_flag "")
endif()

set(DEPENDENCIES_FOLDER "${CMAKE_BINARY_DIR}/dependencies")
set_property(DIRECTORY PROPERTY EP_BASE "${DEPENDENCIES_FOLDER}")

###########################################
#                                         #
#              Loading GMP                #
#                                         #
###########################################
set(FETCHED_GMP_VERSION "6.3.0")
add_subdirectory(gmp)

###########################################
#                                         #
#              Loading NTL                #
#                                         #
###########################################
set(FETCHED_NTL_VERSION "11.6.0")
add_subdirectory(ntl)

###########################################
#                                         #
#            Loading OpenFHE              #
#                                         #
###########################################
add_subdirectory(openfhe)