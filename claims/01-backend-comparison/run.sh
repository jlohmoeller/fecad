#!/usr/bin/env bash
source "$(dirname "$0")/../_common.sh"

case "${SCALE}" in
  1) RECORDS=1000  ; BACKENDS="null lattigo he3db" ;;
  2) RECORDS=10000 ; BACKENDS="null lattigo he3db patdiscover engorgio" ;;
esac

echo "backend comparison, ${RECORDS} records, one provider, one run each"
for BK in ${BACKENDS}; do
  has_backend "${BK}" || { echo "skipping ${BK}: C++ bridges not built"; continue; }
  "${BIN}" bench run-task --task-id 0 --backend "${BK}" \
    --dataset mimic_iv --query q1 --records "${RECORDS}" --providers 1 \
    --schema "${ARTIFACT_DIR}/schemas/mimic_iv.json" \
    --out-dir "${OUT_DIR}/${BK}" >"${OUT_DIR}/${BK}.log" 2>&1
  report "${BK}" "${OUT_DIR}/${BK}"
done