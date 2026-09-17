#!/usr/bin/env bash
# Build the artifact, run this from the repository root
#
#   ./install.sh          Go only. The plaintext baseline and the Lattigo BGV
#                         backend. Enough for every claim except the three CGO
#                         backends of claim 01.
#   ./install.sh --full   Additionally builds the C++ bridges for HE3DB,
#                         Engorgio and PatDiscover. Needs cmake, a C++17
#                         compiler, OpenMP, NTL and GMP. It builds SEAL and
#                         OpenFHE from source, so the time scales with the core
#                         count.
set -euo pipefail
cd "$(dirname "$0")/artifact"

command -v go >/dev/null || { echo "go toolchain not found (need 1.22+)" >&2; exit 1; }
echo "building the Go binary"
mkdir -p bin && go build -o bin/fecad .        # plaintext baseline + Lattigo BGV
echo "  bin/fecad"

if [[ "${1:-}" == "--full" ]]; then
  command -v cmake >/dev/null || { echo "cmake not found" >&2; exit 1; }
  if [[ ! -d thirdparty/HE3DB || ! -d thirdparty/Engorgio ]]; then
    cat >&2 <<MSG
the upstream engines are not vendored in this archive. Fetch them from the
repository root, next to the PatDiscover checkout that is already there:
  git clone --recursive https://github.com/zhouzhangwalker/HE3DB artifact/thirdparty/HE3DB
  git clone --recursive https://github.com/Errantry73/Engorgio artifact/thirdparty/Engorgio
MSG
    exit 1
  fi
  echo "building the C++ bridges (this takes a while)"
  bash scripts/build_cpp.sh
  go build -tags cgo_backends -o bin/fecad .
fi

echo
echo "checking the build with a one-provider run"
./bin/fecad bench run-task --task-id 0 --backend lattigo --dataset mimic_iv \
  --query q1 --records 200 --providers 1 --schema schemas/mimic_iv.json \
  --out-dir /tmp/fecad-install-check >/dev/null
grep -rho 'acc=[0-9.]*' /tmp/fecad-install-check | head -1
rm -rf /tmp/fecad-install-check
echo "install complete"
