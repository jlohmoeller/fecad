# shellcheck shell=bash
# _common.sh — env shared by every eval_e*.sh
# sourced, not executed; inherits the caller's `set -euo pipefail`
#
# inputs (env, optional, set BEFORE sourcing):
#   SCRATCH_ROOT   scratch root, default /var/tmp (ext4, no cgroup mem charge);
#                  E4 overrides to /tmp (60G tmpfs)
#
# exports/sets:
#   FECAD_DIR      submit dir (holds schemas/)
#   BEEGFS_DIR     shared-FS root for binary, libs, logs, results
#   BIN            staged fecad binary
#   LD_LIBRARY_PATH  staged libs prepended
#   TMPDIR         per-task scratch, cleaned on EXIT/TERM/INT/HUP
#   SCRATCH_DIR    same path as TMPDIR
#
# side effects:
#   - stages BIN + OpenFHE libs to /var/tmp/fecad_stage per node, flock-guarded
#   - installs cleanup traps for the per-task scratch dir
#   - removes stale fecad_<digit>* scratch dirs older than 48h
#   - creates ${BEEGFS_DIR}/logs
#
# requires: SLURM_JOB_ID, SLURM_ARRAY_TASK_ID, SLURM_SUBMIT_DIR

FECAD_DIR="${FECAD_DIR:-${SLURM_SUBMIT_DIR}}"
BEEGFS_DIR=${FECAD_SHARED:-/srv/fecad}

# pre-staging scratch cleanup
: "${SCRATCH_ROOT:=/var/tmp}"
_active_jobs="$(squeue -u "${USER}" -h -o '%A' 2>/dev/null | sort -u | tr '\n' '|' | sed 's/|$//')"
if [[ -n "${_active_jobs}" ]]; then
  for d in "${SCRATCH_ROOT}"/fecad_[0-9]*; do
    [[ -d "${d}" ]] || continue
    _jobid="${d##*/fecad_}"; _jobid="${_jobid%%_*}"
    if [[ ! "${_jobid}" =~ ^(${_active_jobs})$ ]]; then
      rm -rf "${d}" 2>/dev/null || true
    fi
  done
fi

# stage binary per node
LOCAL_STAGE=/var/tmp/fecad_stage
mkdir -p "${LOCAL_STAGE}"
(
  flock -w 120 9
  if [[ ! -x "${LOCAL_STAGE}/bin/fecad" ]] \
     || [[ "${BEEGFS_DIR}/bin/fecad" -nt "${LOCAL_STAGE}/bin/fecad" ]]; then
    mkdir -p "${LOCAL_STAGE}/bin"
    cp "${BEEGFS_DIR}/bin/fecad" "${LOCAL_STAGE}/bin/fecad.new"
    mv "${LOCAL_STAGE}/bin/fecad.new" "${LOCAL_STAGE}/bin/fecad"
    if [[ -d "${BEEGFS_DIR}/lib" ]]; then
      mkdir -p "${LOCAL_STAGE}/lib"
      cp -a "${BEEGFS_DIR}/lib/." "${LOCAL_STAGE}/lib/"
    fi
  fi
) 9>"${LOCAL_STAGE}/.stage.lock"
BIN="${LOCAL_STAGE}/bin/fecad"
if [[ -d "${LOCAL_STAGE}/lib" ]]; then
  export LD_LIBRARY_PATH="${LOCAL_STAGE}/lib:${LD_LIBRARY_PATH:-}"
fi

# per-task scratch
: "${SCRATCH_ROOT:=/var/tmp}"
SCRATCH_DIR="${SCRATCH_ROOT}/fecad_${SLURM_JOB_ID}_${SLURM_ARRAY_TASK_ID}"
_fecad_cleanup() { rm -rf "${SCRATCH_DIR}" 2>/dev/null || true; }
trap _fecad_cleanup EXIT
trap '_fecad_cleanup; exit 143' TERM
trap '_fecad_cleanup; exit 130' INT
trap '_fecad_cleanup; exit 129' HUP
find "${SCRATCH_ROOT}" -maxdepth 1 -name 'fecad_[0-9]*' -type d -mmin +2880 \
  -exec rm -rf {} + 2>/dev/null || true
mkdir -p "${SCRATCH_DIR}" "${BEEGFS_DIR}/logs"
export TMPDIR="${SCRATCH_DIR}"

# prune task .bin files
prune_task_bins() {
  local out_dir="$1" task_id="$2"
  find "${out_dir}/task_${task_id}" -name '*.bin' -delete 2>/dev/null || true
}
