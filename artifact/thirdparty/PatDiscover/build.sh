#! /bin/bash

cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DCMAKE_MAKE_PROGRAM=ninja -G Ninja -S . -B cmake-build-release
cmake --build cmake-build-release --target all -j 24
