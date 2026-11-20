# Helpers for running commands in the project container. Sourced by host-side scripts.
# Only the repository is mounted, at /work.

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

rrlua_platform() {
  case "$(uname -m)" in
    x86_64) echo linux/amd64 ;;
    aarch64 | arm64) echo linux/arm64 ;;
    *) echo "unsupported host architecture: $(uname -m)" >&2; return 1 ;;
  esac
}

# Image tag is derived from the Containerfile so edits rebuild it.
rrlua_image() {
  local h
  h=$(sha256sum "$REPO_ROOT/Containerfile" | cut -c1-12)
  echo "localhost/robotraconteur-lua-dev:$h"
}

rrlua_ensure_image() {
  command -v podman >/dev/null || { echo "podman not found on host" >&2; return 1; }
  local img plat
  img=$(rrlua_image); plat=$(rrlua_platform)
  if ! podman image exists "$img"; then
    podman build --platform "$plat" -t "$img" -f "$REPO_ROOT/Containerfile" "$REPO_ROOT"
  fi
  # Refuse an image of another architecture (it would run under emulation).
  local host_arch img_arch
  host_arch=${plat#linux/}
  img_arch=$(podman image inspect --format '{{.Architecture}}' "$img")
  if [ "$img_arch" != "$host_arch" ]; then
    echo "image $img is $img_arch but host is $host_arch" >&2; return 1
  fi
}

# rrlua_run [podman run options...] -- command...
rrlua_run() {
  local opts=()
  while [ $# -gt 0 ] && [ "$1" != "--" ]; do opts+=("$1"); shift; done
  [ "${1:-}" = "--" ] && shift
  local tty=()
  [ -t 0 ] && [ -t 1 ] && tty=(-t)
  podman run --rm -i "${tty[@]}" --platform "$(rrlua_platform)" \
    -v "$REPO_ROOT:/work:Z" -w /work \
    -e JOBS="${JOBS:-$(nproc)}" \
    "${opts[@]}" "$(rrlua_image)" "$@"
}
