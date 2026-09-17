#!/usr/bin/env bash
#
# netem on `lo` via sudo: delay is per packet and per direction (hence RTT/2),
# the rate cap is ONE shared token bucket, i.e. a proxy uplink shared by all
# providers rather than N independent links; setup traffic is emulated too
#
# reps are the OUTER loop, so a truncated run still has one sample per cell
#
# usage (from the fecad/ directory):
#   bash slurm/run_e7_wan_local.sh                 # full matrix, 30 reps
#   REPS=3 bash slurm/run_e7_wan_local.sh          # shorter run
#   PROFILES=none REPS=1 N_VALS=1 bash slurm/run_e7_wan_local.sh   # harness check
#
# env overrides: REPS, PROFILES, N_VALS, RESULTS_DIR, BIN
set -euo pipefail

FECAD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BEEGFS_DIR=${FECAD_SHARED:-/srv/fecad}
BIN="${BIN:-${FECAD_DIR}/bin/fecad}"
RESULTS_DIR="${RESULTS_DIR:-${BEEGFS_DIR}/results}"
export LD_LIBRARY_PATH="${BEEGFS_DIR}/lib:${FECAD_DIR}/cpp/lib:${LD_LIBRARY_PATH:-}"

BACKEND=lattigo
read -r -a PROFILE_LIST <<< "${PROFILES:-none rtt20_1gbit rtt50_100mbit}"
read -r -a N_LIST <<< "${N_VALS:-1 100}"
TOTAL_RECORDS=10000
REPS="${REPS:-30}"

# one-way delay (RTT/2) and rate cap per profile name
profile_delay_ms() { case "$1" in none) echo 0 ;; rtt20_1gbit) echo 10 ;; rtt50_100mbit) echo 25 ;; esac; }
profile_rate()     { case "$1" in none) echo 0 ;; rtt20_1gbit) echo 1gbit ;; rtt50_100mbit) echo 100mbit ;; esac; }
# canonical profile order, so task ids match the SLURM variant
profile_idx()      { case "$1" in none) echo 0 ;; rtt20_1gbit) echo 1 ;; rtt50_100mbit) echo 2 ;; esac; }
n_idx()            { case "$1" in 1) echo 0 ;; 100) echo 1 ;; *) echo 0 ;; esac; }

# per-task scratchs
SCRATCH_DIR="/var/tmp/fecad_e7local_$$"
mkdir -p "${SCRATCH_DIR}"
export TMPDIR="${SCRATCH_DIR}"

# Removal must succeed or the next profile silently inherits the previous
# emulation and its samples are mislabeled. "del" on a clean lo fails with
# "handle of zero", the one error we tolerate
wan_down() {
  local out
  if ! out="$(sudo -n tc qdisc del dev lo root 2>&1)"; then
    case "${out}" in
      *"handle of zero"*|*"No such file or directory"*|*"Invalid argument"*) return 0 ;;
      *) echo "ERROR: cannot clear netem on lo: ${out}" >&2; exit 1 ;;
    esac
  fi
}
cleanup()  { sudo -n tc qdisc del dev lo root >/dev/null 2>&1 || true; rm -rf "${SCRATCH_DIR}"; }
trap cleanup EXIT
trap 'cleanup; exit 143' TERM
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 129' HUP

wan_up() {
  local delay_ms="$1" rate="$2"
  sudo -n tc qdisc replace dev lo root netem delay "${delay_ms}ms" rate "${rate}"
  echo "  netem on lo: delay ${delay_ms}ms (RTT $(( 2 * delay_ms ))ms), rate ${rate}"
}

# preflight: fail before burning hours on an uninstallable profile
needs_root=0
for p in "${PROFILE_LIST[@]}"; do [[ "${p}" != "none" ]] && needs_root=1; done
if [[ "${needs_root}" -eq 1 ]] && ! sudo -n true 2>/dev/null; then
  echo "ERROR: passwordless sudo is required for tc/netem on $(hostname)." >&2
  echo "       'sudo -n -l' currently ends in an (ALL) ALL rule that overrides the" >&2
  echo "       NOPASSWD grant — sudoers is last-match-wins. Fix that, or run only" >&2
  echo "       the baseline: PROFILES=none bash slurm/run_e7_wan_local.sh" >&2
  exit 1
fi

echo "E7 local: backend=${BACKEND} profiles='${PROFILE_LIST[*]}' providers='${N_LIST[*]}' reps=${REPS}"
echo "         binary=${BIN}  results=${RESULTS_DIR}/e7_wan"

for (( rep = 0; rep < REPS; rep++ )); do
  for PROFILE in "${PROFILE_LIST[@]}"; do
    DELAY_MS="$(profile_delay_ms "${PROFILE}")"
    RATE="$(profile_rate "${PROFILE}")"
    wan_down
    if [[ "${PROFILE}" != "none" ]]; then
      wan_up "${DELAY_MS}" "${RATE}"
    fi
    for N_PROVIDERS in "${N_LIST[@]}"; do
      RECORDS=$(( TOTAL_RECORDS / N_PROVIDERS ))
      (( RECORDS < 1 )) && RECORDS=1
      TASK_ID=$(( $(profile_idx "${PROFILE}") * ${#N_LIST[@]} * REPS \
                  + $(n_idx "${N_PROVIDERS}") * REPS + rep ))
      OUT_DIR="${RESULTS_DIR}/e7_wan/${BACKEND}/${PROFILE}/${N_PROVIDERS}"
      mkdir -p "${OUT_DIR}"
      echo "[$(date -u +%H:%M:%S)] rep=${rep} profile=${PROFILE} providers=${N_PROVIDERS} task=${TASK_ID}"
      "${BIN}" bench run-task \
        --task-id   "${TASK_ID}" \
        --dataset   mimic_iv \
        --query     q2 \
        --records   "${RECORDS}" \
        --providers "${N_PROVIDERS}" \
        --schema    "${FECAD_DIR}/schemas/mimic_iv.json" \
        --out-dir   "${OUT_DIR}" \
        --backend   "${BACKEND}"
      find "${OUT_DIR}/task_${TASK_ID}" -name '*.bin' -delete 2>/dev/null || true
    done
  done
done
wan_down
echo "E7 local run done."
