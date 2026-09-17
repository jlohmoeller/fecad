#!/usr/bin/env bash
# E9 provider host, an independent single-node job
# env: RUN_DIR, FIRST_IDX, COUNT, BACKEND (set via sbatch --export)
#SBATCH --job-name=e9_prov
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --exclusive
#SBATCH --cpus-per-task=16
#SBATCH --mem=60G
#SBATCH --time=02:00:00
#SBATCH --output=${FECAD_SHARED:-/srv/fecad}/logs/e9prov_%A.out
#SBATCH --error=${FECAD_SHARED:-/srv/fecad}/logs/e9prov_%A.err
set -euo pipefail
# _common.sh names scratch after the array index; these are plain jobs
export SLURM_ARRAY_TASK_ID="${SLURM_ARRAY_TASK_ID:-0}"
source "${SLURM_SUBMIT_DIR}/slurm/_common.sh"
BIN="${BIN}" BACKEND="${BACKEND:-lattigo}" SCHEMA="${RUN_DIR}/schema.json" \
  bash "${SLURM_SUBMIT_DIR}/slurm/multihost_provider_launcher.sh" \
  "${RUN_DIR}" "${FIRST_IDX}" "${COUNT}"
