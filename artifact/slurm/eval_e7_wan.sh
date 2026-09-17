#!/usr/bin/env bash
# needs root (tc wants CAP_NET_ADMIN) and --exclusive: the qdisc is node-global
#
# array: 3 profiles × 2 provider counts × 30 reps = 180 tasks (0-179)
#SBATCH --job-name=e7_wan
#SBATCH --array=0-179%40
#SBATCH --ntasks=1
#SBATCH --nodes=1
#SBATCH --exclusive
#SBATCH --cpus-per-task=4
#SBATCH --mem=60G
#SBATCH --time=48:00:00
#SBATCH --output=${FECAD_SHARED:-/srv/fecad}/logs/e7_%A_%a.out
#SBATCH --error=${FECAD_SHARED:-/srv/fecad}/logs/e7_%A_%a.err
set -euo pipefail
source "${SLURM_SUBMIT_DIR}/slurm/_common.sh"

BACKEND=lattigo
PROFILES=(none rtt20_1gbit rtt50_100mbit)
# one-way delay (RTT/2) and rate cap per profile, index-aligned
DELAYS_MS=(0 10 25)
RATES=(0 1gbit 100mbit)
N_VALS=(1 100)
TOTAL_RECORDS=10000
REPS=30
N_NVALS=${#N_VALS[@]}
# encoding: TASK_ID = PROFILE_IDX*(N_NVALS*REPS) + N_IDX*REPS + REP

TASK_ID="${SLURM_ARRAY_TASK_ID}"
PROFILE_IDX=$(( TASK_ID / (N_NVALS * REPS) ))
REM=$(( TASK_ID % (N_NVALS * REPS) ))
N_IDX=$(( REM / REPS ))
REP=$(( REM % REPS ))

PROFILE="${PROFILES[$PROFILE_IDX]}"
DELAY_MS="${DELAYS_MS[$PROFILE_IDX]}"
RATE="${RATES[$PROFILE_IDX]}"
N_PROVIDERS="${N_VALS[$N_IDX]}"
RECORDS=$(( TOTAL_RECORDS / N_PROVIDERS ))
if [[ "${RECORDS}" -lt 1 ]]; then RECORDS=1; fi

# --- netem control ------------------------------------------------------------
# wan_down on every exit path: a cancelled or crashed task must not leave the
# node's loopback throttled for whoever gets it next
wan_down() { tc qdisc del dev lo root 2>/dev/null || true; }
wan_up() {
  tc qdisc replace dev lo root netem delay "${DELAY_MS}ms" rate "${RATE}"
  echo "netem on lo: delay ${DELAY_MS}ms (RTT $(( 2 * DELAY_MS ))ms), rate ${RATE}"
  tc qdisc show dev lo
}

if [[ "${PROFILE}" != "none" ]]; then
  if [[ "${EUID}" -ne 0 ]]; then
    echo "E7 profile ${PROFILE} needs root for tc/netem (running as uid ${EUID})" >&2
    exit 1
  fi
  trap 'wan_down; _fecad_cleanup' EXIT
  trap 'wan_down; _fecad_cleanup; exit 143' TERM
  trap 'wan_down; _fecad_cleanup; exit 130' INT
  trap 'wan_down; _fecad_cleanup; exit 129' HUP
  wan_down   # clear a qdisc left behind by a killed predecessor
  wan_up
fi

echo "E7 task ${TASK_ID}: backend=${BACKEND} profile=${PROFILE} n_providers=${N_PROVIDERS} records_per=${RECORDS} rep=${REP}"

OUT_DIR="${BEEGFS_DIR}/results/e7_wan/${BACKEND}/${PROFILE}/${N_PROVIDERS}"
mkdir -p "${OUT_DIR}"
"${BIN}" bench run-task \
  --task-id   "${TASK_ID}" \
  --dataset   mimic_iv \
  --query     q2 \
  --records   "${RECORDS}" \
  --providers "${N_PROVIDERS}" \
  --schema    "${FECAD_DIR}/schemas/mimic_iv.json" \
  --out-dir   "${OUT_DIR}" \
  --backend   "${BACKEND}"

prune_task_bins "${OUT_DIR}" "${TASK_ID}"
echo "E7 task ${TASK_ID} done."
