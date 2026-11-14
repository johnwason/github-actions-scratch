# Shared settings for the rmw_intraprocess scripts. Source this file; do not run it.
#
# Safety (see CLAUDE.md S1/S2): every container started from here runs rootless,
# with --network=none, all capabilities dropped, the source mounted read-only,
# and build output in named volumes owned by this project.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${RMW_INTRAPROCESS_IMAGE:-localhost/rmw_intraprocess-dev:jazzy}"
VOLUME_PREFIX="rmw_intraprocess"
SRC_MOUNT="/ws/src/rmw_intraprocess"
VARIANTS=(release asan tsan)
# tsan is disabled by user decision (2026-11-13): GCC 13 TSan aborts with
# "unexpected memory mapping" under the host kernel's ASLR entropy, and the
# container cannot disable ASLR. See docs/unsupported.md. Opt in with
# RMW_INTRAPROCESS_ENABLE_TSAN=1.
DEFAULT_VARIANTS=(release asan)

die() { echo "error: $*" >&2; exit 1; }

check_variant() {
  local v="$1"
  if [[ "$v" == tsan && "${RMW_INTRAPROCESS_ENABLE_TSAN:-0}" != 1 ]]; then
    die "tsan variant is disabled for now (see docs/unsupported.md); set RMW_INTRAPROCESS_ENABLE_TSAN=1 to run it anyway"
  fi
  for known in "${VARIANTS[@]}"; do
    [[ "$v" == "$known" ]] && return 0
  done
  die "unknown variant '$v' (expected one of: ${VARIANTS[*]})"
}

ensure_image() {
  podman image exists "$IMAGE" || die "image $IMAGE not found; run scripts/build-image.sh first"
}

# run_in_container <variant|-> <command...>
# With a variant, mounts that variant's build/install/log volumes at /ws/{build,install,log}.
run_in_container() {
  local variant="$1"; shift
  ensure_image
  local args=(
    run --rm
    --network=none
    --cap-drop=all
    --security-opt=no-new-privileges
    --env "RMW_INTRAPROCESS_VARIANT=${variant}"
    -v "${REPO_DIR}:${SRC_MOUNT}:ro,Z"
  )
  if [[ "$variant" != "-" ]]; then
    check_variant "$variant"
    args+=(
      -v "${VOLUME_PREFIX}-build-${variant}:/ws/build"
      -v "${VOLUME_PREFIX}-install-${variant}:/ws/install"
      -v "${VOLUME_PREFIX}-log-${variant}:/ws/log"
    )
  fi
  if [[ -t 0 && -t 1 ]]; then args+=(-it); fi
  podman "${args[@]}" "$IMAGE" "$@"
}
