#!/usr/bin/env bash
# Regenerates the B2 golden files (robotraconteur_ros2_debug_bridge/test/golden)
# from the release build: scripts/build.sh release first. Review the diff.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

golden="${REPO_DIR}/robotraconteur_ros2_debug_bridge/test/golden"
# Keep in sync with kGoldenPackages in test/test_type_mapper.cpp.
packages=(action_tutorials_interfaces example_interfaces geometry_msgs nav_msgs
          sensor_msgs std_msgs test_msgs turtlesim)

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
pkg_args=()
for p in "${packages[@]}"; do pkg_args+=(--package "$p"); done
# stdin closed so the container runs without a tty and the tar stream stays binary.
run_in_container release bash -c '
  set -e; set +u; source /ws/install/setup.bash; set -u
  out=$(mktemp -d)
  /ws/install/robotraconteur_ros2_debug_bridge/lib/robotraconteur_ros2_debug_bridge/generate_robdef \
    --out "$out" "$@" >&2
  tar -C "$out" -cf - .' bash "${pkg_args[@]}" < /dev/null > "$tmp/golden.tar"
mkdir -p "$golden"
rm -f "$golden"/*.robdef
tar -C "$golden" -xf "$tmp/golden.tar"
echo "$(ls "$golden"/*.robdef | wc -l) golden files in $golden"
