#!/usr/bin/env bash
# array: 1 backend × 2 storage modes × 30 reps = 60 tasks (0-59)
#SBATCH --job-name=e6_split
#SBATCH --array=0-59%40
#SBATCH --ntasks=1
#SBATCH --nodes=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=60G
#SBATCH --time=48:00:00
#SBATCH --output=${FECAD_SHARED:-/srv/fecad}/logs/e6_%A_%a.out
#SBATCH --error=${FECAD_SHARED:-/srv/fecad}/logs/e6_%A_%a.err
set -euo pipefail
source "${SLURM_SUBMIT_DIR}/slurm/_common.sh"

BACKEND=lattigo
STORAGE_MODES=(merged split)
QUERY=q1
REPS=30
# encoding: TASK_ID = STORAGE_IDX*REPS + REP

TASK_ID="${SLURM_ARRAY_TASK_ID}"
STORAGE_IDX=$(( TASK_ID / REPS ))
REP=$(( TASK_ID % REPS ))
STORAGE="${STORAGE_MODES[$STORAGE_IDX]}"

SPLIT_FLAG=()
if [[ "${STORAGE}" == "split" ]]; then
  SPLIT_FLAG=(--split-storage)
fi

echo "E6 task ${TASK_ID}: backend=${BACKEND} storage=${STORAGE} query=${QUERY} rep=${REP}"

OUT_DIR="${BEEGFS_DIR}/results/e6_split/${BACKEND}/${STORAGE}"
mkdir -p "${OUT_DIR}"
"${BIN}" bench run-task \
  --task-id   "${TASK_ID}" \
  --dataset   mimic_iv \
  --query     "${QUERY}" \
  --records   10000 \
  --providers 1 \
  --schema    "${FECAD_DIR}/schemas/mimic_iv.json" \
  --out-dir   "${OUT_DIR}" \
  --backend   "${BACKEND}" \
  "${SPLIT_FLAG[@]}"

prune_task_bins "${OUT_DIR}" "${TASK_ID}"
echo "E6 task ${TASK_ID} done."
