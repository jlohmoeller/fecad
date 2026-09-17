#!/usr/bin/env bash
# build_cpp.sh — C++ bridges + standalone binaries
#
# Outputs:
#   fecad/bin/client              — standalone executables (subprocess backend)
#   fecad/bin/he3db_wrapper
#   fecad/bin/server
#   fecad/bin/engorgio_{client,wrapper,server}
#   fecad/cpp/lib/libfecad_bridge.{so,dylib}        — HE3DB CGo backend
#   fecad/cpp/lib/libengorgio_bridge.{so,dylib}     — Engorgio CGo backend
#   fecad/cpp/lib/libpatdiscover_bridge.{so,dylib}  — PatDiscover CGo backend
#
# Usage:
#   bash scripts/build_cpp.sh [--jobs N] [--he3db-dir /path/to/HE3DB]
#
# Requirements: cmake >=3.16, C++20 compiler (g++-12+ or clang++-15+),
#               OpenMP (optional), nlohmann-json (auto-fetched)
#               PatDiscover backend needs libntl-dev + libgmp-dev
#               (apt: sudo apt install libntl-dev libgmp-dev)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FECAD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CPP_DIR="${FECAD_DIR}/cpp"
BUILD_DIR="${CPP_DIR}/build"
HE3DB_DIR="${FECAD_DIR}/thirdparty/HE3DB"
ENGORGIO_DIR="${FECAD_DIR}/thirdparty/Engorgio"
OPENFHE_DIR="${ENGORGIO_DIR}/thirdparty/openfhe-development"
PATDISCOVER_DIR="${FECAD_DIR}/thirdparty/PatDiscover"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

# argument parsing
while [[ $# -gt 0 ]]; do
    case $1 in
        --jobs|-j) JOBS="$2"; shift 2 ;;
        --he3db-dir) HE3DB_DIR="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

echo "=== fecad C++ build ==="
echo "  fecad:   ${FECAD_DIR}"
echo "  HE3DB:   ${HE3DB_DIR}"
echo "  build:   ${BUILD_DIR}"
echo "  jobs:    ${JOBS}"
echo "  Engorgio:${ENGORGIO_DIR}"
echo "  OpenFHE: ${OPENFHE_DIR}"
echo ""

# engine checkouts (+ their own SEAL / OpenFHE submodules) 
init_engine() {  # init_engine <name> <dir> <nested CMakeLists>
    local name="$1" dir="$2" nested="$3"
    if [[ ! -f "${dir}/CMakeLists.txt" ]]; then
        echo "${name} is missing at ${dir}" >&2
        echo "clone it with --recursive, see install.sh --full" >&2
        exit 1
    fi
    [[ -f "${nested}" ]] && return 0
    echo "${name} nested submodules not initialized — fetching them..."
    git -C "${dir}" submodule update --init --recursive
}
init_engine HE3DB    "${HE3DB_DIR}"    "${HE3DB_DIR}/thirdparty/SEAL/CMakeLists.txt"
init_engine Engorgio "${ENGORGIO_DIR}" "${OPENFHE_DIR}/CMakeLists.txt"

# cmake configure
mkdir -p "${BUILD_DIR}"
cmake -S "${CPP_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DHE3DB_DIR="${HE3DB_DIR}" \
    -DENGORGIO_DIR="${ENGORGIO_DIR}" \
    -DOPENFHE_DIR="${OPENFHE_DIR}" \
    -DPATDISCOVER_DIR="${PATDISCOVER_DIR}" \
    -DCMAKE_CXX_FLAGS="-O3 -march=native" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -Wno-dev

# build
cmake --build "${BUILD_DIR}" --parallel "${JOBS}" \
    --target fecad_bridge engorgio_bridge patdiscover_bridge client he3db_wrapper server engorgio_client engorgio_wrapper engorgio_server

# install into fecad/bin/ and fecad/cpp/lib/
mkdir -p "${FECAD_DIR}/bin" "${CPP_DIR}/lib"

cp "${BUILD_DIR}/bin/client"       "${FECAD_DIR}/bin/"
cp "${BUILD_DIR}/bin/he3db_wrapper" "${FECAD_DIR}/bin/"
cp "${BUILD_DIR}/bin/server"       "${FECAD_DIR}/bin/"
cp "${BUILD_DIR}/bin/engorgio_client"  "${FECAD_DIR}/bin/"
cp "${BUILD_DIR}/bin/engorgio_wrapper" "${FECAD_DIR}/bin/"
cp "${BUILD_DIR}/bin/engorgio_server"  "${FECAD_DIR}/bin/"

if [[ "$(uname)" == "Darwin" ]]; then
    cp "${BUILD_DIR}/lib/libfecad_bridge.dylib"        "${CPP_DIR}/lib/"
    cp "${BUILD_DIR}/lib/libengorgio_bridge.dylib"     "${CPP_DIR}/lib/" 2>/dev/null || true
    cp "${BUILD_DIR}/lib/libpatdiscover_bridge.dylib"  "${CPP_DIR}/lib/" 2>/dev/null || true
else
    cp "${BUILD_DIR}/lib/libfecad_bridge.so"        "${CPP_DIR}/lib/"
    cp "${BUILD_DIR}/lib/libengorgio_bridge.so"     "${CPP_DIR}/lib/" 2>/dev/null || true
    cp "${BUILD_DIR}/lib/libpatdiscover_bridge.so"  "${CPP_DIR}/lib/" 2>/dev/null || true
fi

echo ""
echo "=== Build complete ==="
echo "  Binaries:   ${FECAD_DIR}/bin/{client,he3db_wrapper,server}"
echo "              ${FECAD_DIR}/bin/{engorgio_client,engorgio_wrapper,engorgio_server}"
echo "  Bridge libs: ${CPP_DIR}/lib/libfecad_bridge.{so,dylib}"
echo "               ${CPP_DIR}/lib/libengorgio_bridge.{so,dylib}     (if OpenFHE present)"
echo "               ${CPP_DIR}/lib/libpatdiscover_bridge.{so,dylib}  (if OpenFHE present)"
echo ""
echo "Subprocess backend (default):"
echo "  fecad bench run --backend he3db    --build-dir ${FECAD_DIR}/bin ..."
echo "  fecad bench run --backend engorgio --build-dir ${FECAD_DIR}/bin ..."
echo ""
echo "CGo backend (no subprocess):"
echo "  cd ${FECAD_DIR} && go build -tags he3db_cgo    -o bin/fecad ."
echo "  cd ${FECAD_DIR} && go build -tags engorgio_cgo -o bin/fecad ."
