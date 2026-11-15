#!/usr/bin/env bash
# Build the development image. This is the ONLY step that uses the network,
# and only for the sources listed at the top of container/Containerfile.
#
# Robot Raconteur and the reference bridge come from GitHub master. Their
# resolved commit SHAs are read back from the image and stamped on it as
# labels (and printed), so every failure can be tied to an RR revision.
# Pass RR_CACHE_BUST=<anything new> to fetch a newer master.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

stage="${IMAGE}-stage"
podman build --pull=missing -t "$stage" \
  --build-arg "RR_CACHE_BUST=${RR_CACHE_BUST:-0}" \
  --build-arg "RR_TSAN_BUILD=${RR_TSAN_BUILD:-0}" \
  -f "${REPO_DIR}/container/Containerfile" "${REPO_DIR}/container"

versions="$(podman run --rm --network=none "$stage" cat /opt/robotraconteur/VERSIONS)"
rr_sha="$(awk '$1 == "robotraconteur" {print $2}' <<<"$versions")"
bridge_sha="$(awk '$1 == "robotraconteur_ros2_bridge" {print $2}' <<<"$versions")"
[[ -n "$rr_sha" && -n "$bridge_sha" ]] || die "could not read the commit SHAs from the image"

# Second step: labels only (no RUN, no network access needed).
printf 'FROM %s\n' "$stage" | podman build -t "$IMAGE" \
  --label "robotraconteur_ros2_debug_bridge.robotraconteur.commit=${rr_sha}" \
  --label "robotraconteur_ros2_debug_bridge.robotraconteur_ros2_bridge.commit=${bridge_sha}" \
  -f - "${REPO_DIR}/container"
podman rmi "$stage" >/dev/null

echo "== image: $IMAGE"
podman image inspect "$IMAGE" --format '{{range $k, $v := .Labels}}{{$k}}={{$v}}{{"\n"}}{{end}}' \
  | grep -E '^(org.opencontainers.image.base.name|robotraconteur_ros2_debug_bridge\.)'
