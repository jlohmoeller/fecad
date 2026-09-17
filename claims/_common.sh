# Shared helpers for the claim runners.
#
set -euo pipefail

SCALE="${1:-1}"
case "${SCALE}" in 1|2) ;; *) echo "usage: $0 [1|2]" >&2; exit 2 ;; esac

CLAIM_DIR="$(cd "$(dirname "$0")" && pwd)"
ARTIFACT_DIR="$(cd "${CLAIM_DIR}/../../artifact" && pwd)"
BIN="${ARTIFACT_DIR}/bin/fecad"
OUT_DIR="${OUT_DIR:-${CLAIM_DIR}/output}"
export LD_LIBRARY_PATH="${ARTIFACT_DIR}/cpp/lib:${LD_LIBRARY_PATH:-}"

if [[ ! -x "${BIN}" ]]; then
  echo "fecad binary not found at ${BIN} — run ../../install.sh first" >&2
  exit 1
fi
mkdir -p "${OUT_DIR}"

has_backend() {
  case "$1" in
    null|lattigo) return 0 ;;
    *) [[ -f "${ARTIFACT_DIR}/cpp/lib/libfecad_bridge.so" ]] ;;
  esac
}

report() {
  local label="$1" dir="$2"
  local qct
  qct=$(find "${dir}" -name performance_metrics.csv -exec cat {} + 2>/dev/null \
        | awk -F, '$2=="researcher-query-total"{print $3}' | sort -n | tail -1)
  printf '%-46s %s s\n' "${label}" "${qct:-n/a}" | tee -a "${OUT_DIR}/summary.txt"
}
