#!/usr/bin/env bash
source "$(dirname "$0")/../_common.sh"

case "${SCALE}" in
  1) RECORDS=1000  ;;
  2) RECORDS=10000 ;;
esac

echo "reference query across cohorts on Lattigo BGV, ${RECORDS} records, one run each"
for DS in mimic_iv nuclear_medicine hcup_nis; do
  "${BIN}" bench run-task --task-id 0 --backend lattigo \
    --dataset "${DS}" --query q2 --records "${RECORDS}" --providers 1 \
    --schema "${ARTIFACT_DIR}/schemas/${DS}.json" \
    --out-dir "${OUT_DIR}/${DS}" >"${OUT_DIR}/${DS}.log" 2>&1
  report "${DS}" "${OUT_DIR}/${DS}"
done
