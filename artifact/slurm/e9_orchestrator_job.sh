#!/usr/bin/env bash
# E9 orchestrating host: user, privacy proxy, key manager, patient client
# providers run as separate jobs elsewhere; this job waits for their endpoints
# env: RUN_DIR, N_PROVIDERS, RECORDS, TASK_ID, BACKEND, OUT_DIR
#SBATCH --job-name=e9_orch
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --exclusive
#SBATCH --cpus-per-task=16
#SBATCH --mem=60G
#SBATCH --time=02:00:00
#SBATCH --output=${FECAD_SHARED:-/srv/fecad}/logs/e9orch_%A.out
#SBATCH --error=${FECAD_SHARED:-/srv/fecad}/logs/e9orch_%A.err
set -euo pipefail
# _common.sh names scratch after the array index; these are plain jobs
export SLURM_ARRAY_TASK_ID="${SLURM_ARRAY_TASK_ID:-0}"
source "${SLURM_SUBMIT_DIR}/slurm/_common.sh"

# release the provider jobs whatever happens, so a failure here cannot leave
# them holding nodes for their full walltime
trap 'touch "${RUN_DIR}/DONE"; _fecad_cleanup' EXIT

mkdir -p "${OUT_DIR}"
echo "E9 orchestrator on $(hostname): n_providers=${N_PROVIDERS} records_per=${RECORDS} task=${TASK_ID}"
"${BIN}" bench run-task \
  --task-id            "${TASK_ID}" \
  --dataset            mimic_iv \
  --query              q2 \
  --records            "${RECORDS}" \
  --providers          "${N_PROVIDERS}" \
  --schema             "${RUN_DIR}/schema.json" \
  --out-dir            "${OUT_DIR}" \
  --backend            "${BACKEND:-lattigo}" \
  --provider-endpoints "${RUN_DIR}/endpoints" \
  --task-dir           "${RUN_DIR}" \
  --bind-host          0.0.0.0

touch "${RUN_DIR}/DONE"
prune_task_bins "${OUT_DIR}" "${TASK_ID}"
echo "E9 task ${TASK_ID} done."
