#!/usr/bin/env bash
source "$(dirname "$0")/../_common.sh"

case "${SCALE}" in
  1) SIZES="1000 5000"        ;;
  2) SIZES="1000 10000 30000" ;;
esac

echo "records per provider on Lattigo BGV, one provider, one run each"
for N in ${SIZES}; do
  "${BIN}" bench run-task --task-id 0 --backend lattigo \
    --dataset mimic_iv --query q1 --records "${N}" --providers 1 \
    --schema "${ARTIFACT_DIR}/schemas/mimic_iv.json" \
    --out-dir "${OUT_DIR}/${N}" >"${OUT_DIR}/${N}.log" 2>&1
  report "${N} records" "${OUT_DIR}/${N}"
done
