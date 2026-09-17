#!/usr/bin/env bash
source "$(dirname "$0")/../_common.sh"

case "${SCALE}" in
  1) PROVIDERS="1 5 10"  ; TOTAL=1000  ;;
  2) PROVIDERS="1 10 25" ; TOTAL=10000 ;;
esac

echo "federation size on Lattigo BGV, ${TOTAL} records split across providers, one run each"
for N in ${PROVIDERS}; do
  REC=$(( TOTAL / N )); (( REC < 1 )) && REC=1
  "${BIN}" bench run-task --task-id 0 --backend lattigo \
    --dataset mimic_iv --query q2 --records "${REC}" --providers "${N}" \
    --schema "${ARTIFACT_DIR}/schemas/mimic_iv.json" \
    --out-dir "${OUT_DIR}/n${N}" >"${OUT_DIR}/n${N}.log" 2>&1
  report "${N} providers" "${OUT_DIR}/n${N}"
done
