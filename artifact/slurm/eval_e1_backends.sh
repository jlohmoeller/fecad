#!/usr/bin/env bash
# E1: backend reference point
# drives backend_comparison, setup_comparison, system figures
# fixed: mimic_iv, 1 provider, 10K records, q1
# array: 5 backends × 30 reps = 150 tasks (0-149)
#SBATCH --job-name=e1_backends
#SBATCH --array=0-149%40
#SBATCH --ntasks=1
#SBATCH --nodes=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=60G
#SBATCH --time=48:00:00
#SBATCH --output=${FECAD_SHARED:-/srv/fecad}/logs/e1_%A_%a.out
#SBATCH --error=${FECAD_SHARED:-/srv/fecad}/logs/e1_%A_%a.err
set -euo pipefail
source "${SLURM_SUBMIT_DIR}/slurm/_common.sh"

BACKENDS=(lattigo he3db engorgio null patdiscover)
QUERY=q1
REPS=30
# encoding: TASK_ID = BACKEND_IDX*REPS + REP

TASK_ID="${SLURM_ARRAY_TASK_ID}"
BACKEND_IDX=$(( TASK_ID / REPS ))
REP=$(( TASK_ID % REPS ))
BACKEND="${BACKENDS[$BACKEND_IDX]}"

echo "E1 task ${TASK_ID}: backend=${BACKEND} query=${QUERY} rep=${REP}"

OUT_DIR="${BEEGFS_DIR}/results/e1_backends/${BACKEND}/${QUERY}"
mkdir -p "${OUT_DIR}"
"${BIN}" bench run-task \
  --task-id   "${TASK_ID}" \
  --dataset   mimic_iv \
  --query     "${QUERY}" \
  --records   10000 \
  --providers 1 \
  --schema    "${FECAD_DIR}/schemas/mimic_iv.json" \
  --out-dir   "${OUT_DIR}" \
  --backend   "${BACKEND}"

prune_task_bins "${OUT_DIR}" "${TASK_ID}"
echo "E1 task ${TASK_ID} done."
