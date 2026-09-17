#!/usr/bin/env bash
# E8: federation series with one core per provider
# same configuration as E3 (mimic_iv, q2, 10K records over N providers, lattigo)
# but a whole 96-core node per task instead of E3's --cpus-per-task=4
#
# array: 6 provider counts × 30 reps = 180 tasks (0-179)
#SBATCH --job-name=e8_cpuwide
#SBATCH --array=0-179%40
#SBATCH --ntasks=1
#SBATCH --nodes=1
#SBATCH --exclusive
#SBATCH --cpus-per-task=96
#SBATCH --mem=0
#SBATCH --time=48:00:00
#SBATCH --output=${FECAD_SHARED:-/srv/fecad}/logs/e8_%A_%a.out
#SBATCH --error=${FECAD_SHARED:-/srv/fecad}/logs/e8_%A_%a.err
set -euo pipefail
source "${SLURM_SUBMIT_DIR}/slurm/_common.sh"

BACKEND=lattigo
N_VALS=(1 10 25 50 75 100)
TOTAL_RECORDS=10000
REPS=30
N_NVALS=${#N_VALS[@]}
# encoding: TASK_ID = N_IDX*REPS + REP

TASK_ID="${SLURM_ARRAY_TASK_ID}"
N_IDX=$(( TASK_ID / REPS ))
REP=$(( TASK_ID % REPS ))
N_PROVIDERS="${N_VALS[$N_IDX]}"
RECORDS=$(( TOTAL_RECORDS / N_PROVIDERS ))
if [[ "${RECORDS}" -lt 1 ]]; then RECORDS=1; fi

echo "E8 task ${TASK_ID}: backend=${BACKEND} n_providers=${N_PROVIDERS} records_per=${RECORDS} rep=${REP} cpus=$(nproc)"

OUT_DIR="${BEEGFS_DIR}/results/e8_cpuwide/${BACKEND}/${N_PROVIDERS}"
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
echo "E8 task ${TASK_ID} done."
