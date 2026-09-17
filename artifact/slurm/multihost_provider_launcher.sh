#!/usr/bin/env bash
# start a batch of provider services on one host and announce them
#
# usage (one single-node job per provider host, slurm/e9_provider_job.sh):
#   multihost_provider_launcher.sh <run_dir> <first_index> <count>
#
# Independent jobs, not job steps: this cluster reserves no MPI port range
# (MpiParams=none), so even `srun hostname` fails with "Requires more ports than
# can be reserved" — all coordination goes through RUN_DIR on the shared FS
#
# one service pair (data holder + evaluator) per provider, out of
# <run_dir>/provider_<i>; addresses land in <run_dir>/endpoints/provider_<i>.json
# once /health answers, then idle until the orchestrator drops <run_dir>/DONE
set -euo pipefail

RUN_DIR="$1"; FIRST="$2"; COUNT="$3"
BEEGFS_DIR=${FECAD_SHARED:-/srv/fecad}
BIN="${BIN:-${BEEGFS_DIR}/bin/fecad}"
SCHEMA="${SCHEMA:-${RUN_DIR}/schema.json}"
BACKEND="${BACKEND:-lattigo}"
export LD_LIBRARY_PATH="${BEEGFS_DIR}/lib:${LD_LIBRARY_PATH:-}"
HOST="$(hostname)"
# node-local scratch for backend spills; durable state lives in RUN_DIR
export TMPDIR="/var/tmp/fecad_e9_${SLURM_JOB_ID:-0}_${HOST}"
mkdir -p "${TMPDIR}"

pids=()
cleanup() { kill "${pids[@]}" 2>/dev/null || true; rm -rf "${TMPDIR}"; }
trap cleanup EXIT TERM INT HUP

for (( k = 0; k < COUNT; k++ )); do
  idx=$(( FIRST + k ))
  # ports derive from the global provider index so two provider jobs sharing a
  # machine cannot collide
  dh_port=$(( 20000 + 2 * idx ))
  ev_port=$(( 20001 + 2 * idx ))
  data_dir="${RUN_DIR}/provider_${idx}"
  mkdir -p "${data_dir}"
  "${BIN}" provider serve \
      --data-dir           "${data_dir}" \
      --schema             "${SCHEMA}" \
      --backend            "${BACKEND}" \
      --data-holder-addr   "0.0.0.0:${dh_port}" \
      --evaluator-addr     "0.0.0.0:${ev_port}" \
      >> "${RUN_DIR}/logs/provider_${idx}.log" 2>&1 &
  pids+=($!)
done

# publish only after both halves answer /health, so the orchestrator never
# dials a socket that is not listening yet
for (( k = 0; k < COUNT; k++ )); do
  idx=$(( FIRST + k ))
  dh="http://${HOST}:$(( 20000 + 2 * idx ))"
  ev="http://${HOST}:$(( 20001 + 2 * idx ))"
  for _ in $(seq 1 120); do
    if curl -sf "${dh}/health" >/dev/null && curl -sf "${ev}/health" >/dev/null; then
      printf '{"data_holder":"%s","evaluator":"%s"}\n' "${dh}" "${ev}" \
        > "${RUN_DIR}/endpoints/provider_${idx}.json.tmp"
      mv "${RUN_DIR}/endpoints/provider_${idx}.json.tmp" \
         "${RUN_DIR}/endpoints/provider_${idx}.json"
      break
    fi
    sleep 1
  done
  if [[ ! -f "${RUN_DIR}/endpoints/provider_${idx}.json" ]]; then
    echo "provider ${idx} on ${HOST} never became healthy" >&2
    exit 1
  fi
done

echo "${HOST}: ${COUNT} providers ready (indices ${FIRST}..$(( FIRST + COUNT - 1 )))"
waited=0
while [[ ! -f "${RUN_DIR}/DONE" ]]; do
  sleep 5
  waited=$(( waited + 5 ))
  if (( waited > 7200 )); then
    echo "${HOST}: no DONE marker after 2h — orchestrator gone, shutting down" >&2
    exit 1
  fi
done
echo "${HOST}: orchestrator finished, shutting down providers"
