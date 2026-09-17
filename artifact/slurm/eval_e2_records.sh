#!/usr/bin/env bash
# E2: record-count scalability
# drives scalability panel 1 (records × backend, q1) and panel 4
# (records × query, lattigo)
# fixed: mimic_iv, 1 provider
#
# flat condition list: the 40 cells the plots need, not all 5×4×4 = 80
# engorgio/patdiscover/he3db stop at 50k (linear cost, slot caps); null and
# lattigo extend to 1M for the right edge of panel 1
#
# array: 40 conditions × 30 reps = 1200 tasks (0-1199)
#SBATCH --job-name=e2_records
#SBATCH --array=0-1199%40
#SBATCH --ntasks=1
#SBATCH --nodes=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=180G
#SBATCH --time=48:00:00
#SBATCH --output=${FECAD_SHARED:-/srv/fecad}/logs/e2_%A_%a.out
#SBATCH --error=${FECAD_SHARED:-/srv/fecad}/logs/e2_%A_%a.err
set -euo pipefail
source "${SLURM_SUBMIT_DIR}/slurm/_common.sh"

CONDS=(
  # panel 1 main series
  null:q1:1000        null:q1:10000        null:q1:30000        null:q1:50000
  lattigo:q1:1000     lattigo:q1:10000     lattigo:q1:30000     lattigo:q1:50000
  he3db:q1:1000       he3db:q1:10000       he3db:q1:30000       he3db:q1:50000
  engorgio:q1:1000    engorgio:q1:10000    engorgio:q1:30000    engorgio:q1:50000
  patdiscover:q1:1000 patdiscover:q1:10000 patdiscover:q1:30000 patdiscover:q1:50000
  # panel 1 extended
  null:q1:100000     null:q1:250000     null:q1:500000     null:q1:1000000
  lattigo:q1:100000  lattigo:q1:250000  lattigo:q1:500000  lattigo:q1:1000000
  # panel 4 per-query lattigo
  lattigo:q2:1000 lattigo:q2:10000 lattigo:q2:30000 lattigo:q2:50000
  lattigo:q3:1000 lattigo:q3:10000 lattigo:q3:30000 lattigo:q3:50000
  lattigo:q4:1000 lattigo:q4:10000 lattigo:q4:30000 lattigo:q4:50000
)
REPS=30
N_CONDS=${#CONDS[@]}
# encoding: TASK_ID = COND_IDX*REPS + REP

TASK_ID="${SLURM_ARRAY_TASK_ID}"
COND_IDX=$(( TASK_ID / REPS ))
REP=$(( TASK_ID % REPS ))
COND="${CONDS[$COND_IDX]}"
BACKEND="${COND%%:*}"
REST="${COND#*:}"
QUERY="${REST%%:*}"
RECORDS="${REST##*:}"

MAX_CONSENTING=250000
CONSENT_FRACTION=1.00
if (( RECORDS > MAX_CONSENTING )); then
  CONSENT_FRACTION=$(awk -v m="${MAX_CONSENTING}" -v r="${RECORDS}" 'BEGIN{printf "%.2f", m/r}')
fi

echo "E2 task ${TASK_ID}: backend=${BACKEND} records=${RECORDS} query=${QUERY} rep=${REP} consent=${CONSENT_FRACTION}"

OUT_DIR="${BEEGFS_DIR}/results/e2_records/${BACKEND}/${RECORDS}/${QUERY}"
mkdir -p "${OUT_DIR}"
"${BIN}" bench run-task \
  --task-id   "${TASK_ID}" \
  --dataset   mimic_iv \
  --query     "${QUERY}" \
  --records   "${RECORDS}" \
  --providers 1 \
  --schema    "${FECAD_DIR}/schemas/mimic_iv.json" \
  --out-dir   "${OUT_DIR}" \
  --backend   "${BACKEND}" \
  --consent-fraction "${CONSENT_FRACTION}"

prune_task_bins "${OUT_DIR}" "${TASK_ID}"
echo "E2 task ${TASK_ID} done."
