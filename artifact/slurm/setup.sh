#!/usr/bin/env bash
# usage:
#   bash slurm/setup.sh [--out-dir RESULTS_DIR]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FECAD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
RESULTS_DIR="${RESULTS_DIR:-${FECAD_DIR}/results}"
SCHEMAS_DIR="${FECAD_DIR}/schemas"
BEEGFS_DIR="${BEEGFS_DIR:-${FECAD_SHARED:-/srv/fecad}}"
BIN_SRC="${FECAD_DIR}/bin/fecad"
BIN_BEEGFS="${BEEGFS_DIR}/bin/fecad"
LIB_SRC_BRIDGE="${FECAD_DIR}/cpp/lib"
LIB_SRC_OPENFHE="${FECAD_DIR}/cpp/build/lib"
LIB_BEEGFS="${BEEGFS_DIR}/lib"

echo "=== Building C++ bridge libs (writes ${LIB_SRC_BRIDGE}/*.so) ==="
bash "${FECAD_DIR}/scripts/build_cpp.sh"

echo "=== Building fecad binary (writes ${BIN_SRC}) ==="
cd "${FECAD_DIR}"
BUILD_TIME="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
go build \
  -ldflags="-s -w -X github.com/fecad/cmd.BuildTime=${BUILD_TIME}" \
  -trimpath -o "${BIN_SRC}" .
echo "Binary (local): ${BIN_SRC}  [build_time=${BUILD_TIME}]"

echo "=== Deploying binary to BeeGFS (compute nodes cannot exec /home) ==="
mkdir -p "${BEEGFS_DIR}/bin"
cp "${BIN_SRC}" "${BIN_BEEGFS}"
echo "Binary (beegfs): ${BIN_BEEGFS}"

# Bridge and OpenFHE libs are RUNPATH-resolved against /home (see `readelf -d`),
# which holds only while /home stays mounted exec on compute nodes; stage them
# next to the BeeGFS binary so LD_LIBRARY_PATH can drop the /home dependency
echo "=== Deploying shared libs to BeeGFS (${LIB_BEEGFS}) ==="
mkdir -p "${LIB_BEEGFS}"
cp "${LIB_SRC_BRIDGE}"/lib{fecad,engorgio,patdiscover}_bridge.so "${LIB_BEEGFS}/"
# OpenFHE libs: versioned .so.1 names plus symlinks so dlopen finds the SONAME
cp -P "${LIB_SRC_OPENFHE}"/libOPENFHE{core,pke,binfhe}.so* "${LIB_BEEGFS}/"
# patdiscover_bridge needs NTL plus its libgf2x and libgmp deps, and compute
# nodes ship no libntl-dev — stage the system copies so _common.sh's
# LD_LIBRARY_PATH resolves them
for lib in libntl.so.44 libgf2x.so.3 libgmp.so.10; do
    src="/lib/x86_64-linux-gnu/${lib}"
    # system layout is symlink → versioned real file; copy both so the compute
    # node cannot resolve the SONAME to a dangling link
    if [[ -L "${src}" ]]; then
        cp -P "${src}" "${LIB_BEEGFS}/"
        real="$(readlink -f "${src}")"
        cp "${real}" "${LIB_BEEGFS}/"
    elif [[ -f "${src}" ]]; then
        cp "${src}" "${LIB_BEEGFS}/"
    else
        echo "WARNING: ${src} not found — patdiscover may fail on compute nodes"
    fi
done
echo "Libs (beegfs): $(ls "${LIB_BEEGFS}" | tr '\n' ' ')"

echo "=== Creating output directories ==="
mkdir -p "${RESULTS_DIR}" "${BEEGFS_DIR}/results" "${BEEGFS_DIR}/logs"

echo "=== Checking schemas ==="
for ds in nuclear_medicine mimic_iv; do
  schema="${SCHEMAS_DIR}/${ds}.json"
  if [[ ! -f "${schema}" ]]; then
    echo "WARNING: Schema not found: ${schema}"
    echo "  Copy the schema from disquery/schemas/ or create it manually."
  else
    echo "  OK: ${schema}"
  fi
done

echo ""
echo "Setup complete"