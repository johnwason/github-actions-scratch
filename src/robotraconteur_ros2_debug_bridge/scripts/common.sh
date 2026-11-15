# Shared settings for the robotraconteur_ros2_debug_bridge scripts. Source this
# file; do not run it.
#
# Safety (see CLAUDE.md S1/S2): every container started from here runs
# rootless, with --network=none, all capabilities dropped, both source repos
# mounted read-only, and build output in named volumes owned by this project.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RMW_REPO_DIR="$(cd "${REPO_DIR}/../rmw_intraprocess" && pwd)"
IMAGE="${RR_BRIDGE_IMAGE:-localhost/robotraconteur_ros2_debug_bridge-dev:jazzy}"
VOLUME_PREFIX="robotraconteur_ros2_debug_bridge"
SRC_MOUNT="/ws/src/robotraconteur_ros2_debug_bridge"
RMW_SRC_MOUNT="/ws/src/rmw_intraprocess"
VARIANTS=(release asan tsan)
# tsan: same host limitation as ../rmw_intraprocess (GCC 13 TSan aborts with
# "unexpected memory mapping" under the host kernel's ASLR entropy). It runs on
# the GitHub runner instead. Opt in locally with RR_BRIDGE_ENABLE_TSAN=1.
DEFAULT_VARIANTS=(release asan)

die() { echo "error: $*" >&2; exit 1; }

check_variant() {
  local v="$1"
  if [[ "$v" == tsan && "${RR_BRIDGE_ENABLE_TSAN:-0}" != 1 ]]; then
    die "tsan variant is disabled locally (see scripts/common.sh); set RR_BRIDGE_ENABLE_TSAN=1 to run it anyway"
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
    --env "RR_BRIDGE_VARIANT=${variant}"
    -v "${REPO_DIR}:${SRC_MOUNT}:ro,Z"
    -v "${RMW_REPO_DIR}:${RMW_SRC_MOUNT}:ro,Z"
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
