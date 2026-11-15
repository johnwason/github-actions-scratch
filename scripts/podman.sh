#!/usr/bin/env bash
# Build/run helper. All builds run in podman.
#   scripts/podman.sh image          build the dev image
#   scripts/podman.sh build          configure+build upstream core and Go bindings
#   scripts/podman.sh test           run tests (no network)
#   scripts/podman.sh shell [cmd..]  shell in the container (no network)
# Upstream robotraconteur/ is mounted read-only (RR_SRC_DIR overrides its location).
# Caches (ccache, Go build/module cache) live in the rr-go-cache volume, or in
# $RRGO_CACHE_DIR when set (CI).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
RR_SRC_DIR="${RR_SRC_DIR:-$ROOT/robotraconteur}"
IMAGE="${RRGO_IMAGE:-localhost/rr-go-dev:latest}"

image() {
  podman build -t "$IMAGE" -f "$HERE/docker/Dockerfile.dev" "$HERE/docker"
}

run() {
  local net="$1"; shift
  podman image exists "$IMAGE" || image
  local cache_mount
  if [ -n "${RRGO_CACHE_DIR:-}" ]; then
    # CI: host directory persisted with actions/cache
    mkdir -p "$RRGO_CACHE_DIR"
    cache_mount="$RRGO_CACHE_DIR:/cache:Z"
  else
    podman volume exists rr-go-cache || podman volume create rr-go-cache >/dev/null
    cache_mount="rr-go-cache:/cache"
  fi
  podman run --rm -i $( [ -t 0 ] && echo -t ) \
    --network="$net" \
    --userns=keep-id ${RRGO_PODMAN_ARGS:-} \
    -e HOME=/tmp/home \
    $(for v in JOBS BUILD_TYPE RR_TESTING; do [ -n "${!v:-}" ] && echo "-e $v=${!v}"; done) \
    -v "$RR_SRC_DIR:/work/robotraconteur:ro,Z" \
    -v "$HERE:/work/robotraconteur_go:Z" \
    -v "$cache_mount" \
    "$IMAGE" bash -c 'mkdir -p $HOME /cache/go-build /cache/go-mod /cache/ccache; '"$*"
}

cmd="${1:-build}"; shift || true
case "$cmd" in
  image) image ;;
  build) run none "scripts/build.sh $*" ;;
  test)  run none "scripts/test.sh $*" ;;
  # Network only for fetching Go modules into the cache volume.
  fetch) run slirp4netns "go mod download" ;;
  shell) run none "${*:-bash}" ;;
  *) echo "unknown command $cmd" >&2; exit 2 ;;
esac
