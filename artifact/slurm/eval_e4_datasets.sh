#!/usr/bin/env bash
# E4: dataset diversity
# drives scalability panel 5; lattigo only, the panel plots one backend
# datasets follow the plot's DATASET_ORDER (mimic_iv, nuclear_medicine,
# hcup_nis)
# fixed: 1 provider, 10K records
# array: 11 (dataset,query) pairs × 30 reps = 330 tasks (0-329)
#SBATCH --job-name=e4_datasets
#SBATCH --array=0-329%40
#SBATCH --ntasks=1
#SBATCH --nodes=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=60G
#SBATCH --time=48:00:00
#SBATCH --output=${FECAD_SHARED:-/srv/fecad}/logs/e4_%A_%a.out
#SBATCH --error=${FECAD_SHARED:-/srv/fecad}/logs/e4_%A_%a.err
set -euo pipefail
SCRATCH_ROOT=/tmp
source "${SLURM_SUBMIT_DIR}/slurm/_common.sh"

BACKEND=lattigo
# ragged: hcup_nis has no q4
PAIRS=(
  mimic_iv:q1 mimic_iv:q2 mimic_iv:q3 mimic_iv:q4
  nuclear_medicine:q1 nuclear_medicine:q2 nuclear_medicine:q3 nuclear_medicine:q4
  hcup_nis:q1 hcup_nis:q2 hcup_nis:q3
)
REPS=30
# encoding: TASK_ID = PAIR_IDX * REPS + REP

TASK_ID="${SLURM_ARRAY_TASK_ID}"
PAIR_IDX=$(( TASK_ID / REPS ))
REP=$(( TASK_ID % REPS ))
PAIR="${PAIRS[$PAIR_IDX]}"
DATASET="${PAIR%%:*}"
QUERY="${PAIR##*:}"

echo "E4 task ${TASK_ID}: backend=${BACKEND} dataset=${DATASET} query=${QUERY} rep=${REP}"

OUT_DIR="${BEEGFS_DIR}/results/e4_datasets/${BACKEND}/${DATASET}/${QUERY}"
mkdir -p "${OUT_DIR}"
"${BIN}" bench run-task \
  --task-id   "${TASK_ID}" \
  --dataset   "${DATASET}" \
  --query     "${QUERY}" \
  --records   10000 \
  --providers 1 \
  --schema    "${FECAD_DIR}/schemas/${DATASET}.json" \
  --out-dir   "${OUT_DIR}" \
  --backend   "${BACKEND}"

prune_task_bins "${OUT_DIR}" "${TASK_ID}"
echo "E4 task ${TASK_ID} done."
