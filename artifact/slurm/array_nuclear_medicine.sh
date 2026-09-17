#!/usr/bin/env bash
#SBATCH --job-name=fecad_nm
#SBATCH --array=0-29
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=16G
#SBATCH --time=04:00:00
#SBATCH --output=logs/nm_%A_%a.out
#SBATCH --error=logs/nm_%A_%a.err
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FECAD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN="${FECAD_DIR}/bin/fecad"
RESULTS_DIR="${FECAD_DIR}/results/nuclear_medicine"
SCHEMA="${FECAD_DIR}/schemas/nuclear_medicine.json"
RECORDS=1000
PROVIDERS=1

TASK_ID="${SLURM_ARRAY_TASK_ID}"

mkdir -p "${RESULTS_DIR}" logs

echo "Task ${TASK_ID}"

for QUERY in q1 q2 q3 q4; do
  echo "  Query ${QUERY}"
  "${BIN}" bench run-task \
    --task-id   "${TASK_ID}" \
    --dataset   nuclear_medicine \
    --query     "${QUERY}" \
    --records   "${RECORDS}" \
    --providers "${PROVIDERS}" \
    --schema    "${SCHEMA}" \
    --out-dir   "${RESULTS_DIR}/${QUERY}" \
    --backend   lattigo
done

echo "Task ${TASK_ID} complete."
