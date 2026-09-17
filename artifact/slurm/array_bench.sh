#!/usr/bin/env bash
# bench grid: backends × datasets × reps, in parallel
# REPS: repetition count (default 30)
# array size must match: REPS * N_BACKENDS * N_DATASETS - 1
# submit: sbatch --array=0-$((REPS*8-1)) slurm/array_bench.sh
#
#SBATCH --job-name=fecad_bench
#SBATCH --array=0-239
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=32G
#SBATCH --time=08:00:00
#SBATCH --output=logs/bench_%A_%a.out
#SBATCH --error=logs/bench_%A_%a.err
#
set -euo pipefail

# submit from fecad/: SLURM_SUBMIT_DIR is that dir
FECAD_DIR="${FECAD_DIR:-${SLURM_SUBMIT_DIR}}"
BIN="${FECAD_DIR}/bin/fecad"
RECORDS=1000
PROVIDERS=1
REPS="${REPS:-30}"

BACKENDS=(lattigo he3db engorgio null)
DATASETS=(nuclear_medicine mimic_iv hcup_nis)
declare -A QUERIES=(
  [nuclear_medicine]="q1 q2 q3 q4"
  [mimic_iv]="q1 q2 q3 q4"
)

N_BACKENDS=${#BACKENDS[@]}
N_DATASETS=${#DATASETS[@]}
N_COMBO=$(( N_BACKENDS * N_DATASETS ))

TASK_ID="${SLURM_ARRAY_TASK_ID}"
REP=$(( TASK_ID / N_COMBO ))
COMBO=$(( TASK_ID % N_COMBO ))
BACKEND_IDX=$(( COMBO / N_DATASETS ))
DATASET_IDX=$(( COMBO % N_DATASETS ))

BACKEND="${BACKENDS[$BACKEND_IDX]}"
DATASET="${DATASETS[$DATASET_IDX]}"
SCHEMA="${FECAD_DIR}/schemas/${DATASET}.json"
RESULTS_DIR="${FECAD_DIR}/results/${DATASET}/${BACKEND}"

mkdir -p "${RESULTS_DIR}" logs

echo "Task ${TASK_ID}: backend=${BACKEND} dataset=${DATASET} rep=${REP}"

for QUERY in ${QUERIES[$DATASET]}; do
  echo "  Query ${QUERY}"
  "${BIN}" bench run-task \
    --task-id   "${TASK_ID}" \
    --dataset   "${DATASET}" \
    --query     "${QUERY}" \
    --records   "${RECORDS}" \
    --providers "${PROVIDERS}" \
    --schema    "${SCHEMA}" \
    --out-dir   "${RESULTS_DIR}/${QUERY}" \
    --backend   "${BACKEND}"
done

echo "Task ${TASK_ID} complete."
