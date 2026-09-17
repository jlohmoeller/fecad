#!/usr/bin/env bash
# E5: consent participation rate
# drives scalability panel 3 + consent.pdf
# fixed: mimic_iv, 1 provider, 10K records, q2
# grid: backends={null,lattigo,patdiscover,engorgio} × 6 fractions × 30 reps
# array: 4 × 6 × 30 = 720 tasks (0-719)
#SBATCH --job-name=e5_consent
#SBATCH --array=0-719%40
#SBATCH --ntasks=1
#SBATCH --nodes=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=60G
#SBATCH --time=48:00:00
#SBATCH --output=${FECAD_SHARED:-/srv/fecad}/logs/e5_%A_%a.out
#SBATCH --error=${FECAD_SHARED:-/srv/fecad}/logs/e5_%A_%a.err
set -euo pipefail
source "${SLURM_SUBMIT_DIR}/slurm/_common.sh"

BACKENDS=(null lattigo patdiscover engorgio)
FRACTIONS=(0.01 0.10 0.25 0.50 0.75 1.00)
REPS=30
N_FRACS=${#FRACTIONS[@]}
# encoding: TASK_ID = BACKEND_IDX * (N_FRACS*REPS) + FRAC_IDX * REPS + REP

TASK_ID="${SLURM_ARRAY_TASK_ID}"
BACKEND_IDX=$(( TASK_ID / (N_FRACS * REPS) ))
REM=$(( TASK_ID % (N_FRACS * REPS) ))
FRAC_IDX=$(( REM / REPS ))
REP=$(( REM % REPS ))

BACKEND="${BACKENDS[$BACKEND_IDX]}"
FRACTION="${FRACTIONS[$FRAC_IDX]}"
FRAC_TAG="${FRACTION//./_}"

echo "E5 task ${TASK_ID}: backend=${BACKEND} consent_fraction=${FRACTION} rep=${REP}"

OUT_DIR="${BEEGFS_DIR}/results/e5_consent/${BACKEND}/${FRAC_TAG}"
mkdir -p "${OUT_DIR}"
"${BIN}" bench run-task \
  --task-id          "${TASK_ID}" \
  --dataset          mimic_iv \
  --query            q2 \
  --records          10000 \
  --providers        1 \
  --schema           "${FECAD_DIR}/schemas/mimic_iv.json" \
  --out-dir          "${OUT_DIR}" \
  --backend          "${BACKEND}" \
  --consent-fraction "${FRACTION}"

prune_task_bins "${OUT_DIR}" "${TASK_ID}"
echo "E5 task ${TASK_ID} done."
