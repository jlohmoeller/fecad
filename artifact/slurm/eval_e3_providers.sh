#!/usr/bin/env bash
# E3: multi-institution provider scaling
# fixed: mimic_iv, q2, 10K records split evenly across N providers
# grid: backends={lattigo,null,patdiscover,he3db,engorgio} × N_providers={1,10,25,50,75,100} × 30 reps
# array: 5 × 6 × 30 = 900 tasks (0-899)
#SBATCH --job-name=e3_providers
#SBATCH --array=0-899%40
#SBATCH --ntasks=1
#SBATCH --nodes=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=180G
#SBATCH --time=48:00:00
#SBATCH --output=${FECAD_SHARED:-/srv/fecad}/logs/e3_%A_%a.out
#SBATCH --error=${FECAD_SHARED:-/srv/fecad}/logs/e3_%A_%a.err
set -euo pipefail
source "${SLURM_SUBMIT_DIR}/slurm/_common.sh"

BACKENDS=(lattigo null patdiscover he3db engorgio)
N_VALS=(1 10 25 50 75 100)
TOTAL_RECORDS=10000
REPS=30
N_NVALS=${#N_VALS[@]}
# encoding: TASK_ID = BACKEND_IDX * (N_NVALS*REPS) + N_IDX * REPS + REP

TASK_ID="${SLURM_ARRAY_TASK_ID}"
BACKEND_IDX=$(( TASK_ID / (N_NVALS * REPS) ))
REM=$(( TASK_ID % (N_NVALS * REPS) ))
N_IDX=$(( REM / REPS ))
REP=$(( REM % REPS ))

BACKEND="${BACKENDS[$BACKEND_IDX]}"
N_PROVIDERS="${N_VALS[$N_IDX]}"
RECORDS=$(( TOTAL_RECORDS / N_PROVIDERS ))
if [[ "${RECORDS}" -lt 1 ]]; then RECORDS=1; fi

echo "E3 task ${TASK_ID}: backend=${BACKEND} n_providers=${N_PROVIDERS} records_per=${RECORDS} rep=${REP}"

OUT_DIR="${BEEGFS_DIR}/results/e3_providers/${BACKEND}/${N_PROVIDERS}"
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
echo "E3 task ${TASK_ID} done."
