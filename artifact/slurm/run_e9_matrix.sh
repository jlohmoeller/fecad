#!/usr/bin/env bash
# drive the full E9 matrix, one run at a time
# usage (from fecad/): bash slurm/run_e9_matrix.sh [reps]
#
# Serialized deliberately: a run needs ceil(N/6)+1 exclusive machines of ~23 and
# an orchestrator starts only once its own providers run, so concurrent runs
# deadlock until the providers' 2 h no-DONE timeout fires
#
# no `set -e`: one failed submission must not abandon the rest of the matrix
set -uo pipefail
REPS="${1:-30}"
failed=0
N_VALS=(25 50 100)
FECAD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for (( rep = 0; rep < REPS; rep++ )); do
  for n in "${N_VALS[@]}"; do
    echo "=== $(date -u +%H:%M:%S) E9 n=${n} rep=${rep}"
    if ! out="$(bash "${FECAD_DIR}/slurm/submit_e9.sh" "${n}" "${rep}" 2>&1)"; then
      echo "    submission failed, skipping this run:"
      sed 's/^/      /' <<< "${out}"
      failed=$(( failed + 1 ))
      continue
    fi
    echo "${out}"
    orch=$(sed -n 's/.*orchestrator: job \([0-9]*\).*/\1/p' <<< "${out}")
    if [[ -z "${orch}" ]]; then
      echo "    no orchestrator job id in submission output, skipping"
      failed=$(( failed + 1 ))
      continue
    fi
    provs=$(sed -n 's/.*: job \([0-9]*\)$/\1/p' <<< "${out}" | tr '\n' ',' | sed 's/,$//')
    while squeue -h -j "${orch}" >/dev/null 2>&1 && [[ -n "$(squeue -h -j "${orch}" 2>/dev/null)" ]]; do
      sleep 20
    done
    state=$(sacct -n -j "${orch}" --format=State | head -1 | tr -d ' ')
    echo "    orchestrator ${orch}: ${state}"
    # providers exit on the DONE marker; scancel catches stragglers
    scancel "${provs//,/ }" 2>/dev/null || true
  done
done
if (( failed > 0 )); then
  echo "E9 matrix complete, ${failed} run(s) skipped after submission failures."
else
  echo "E9 matrix complete."
fi
