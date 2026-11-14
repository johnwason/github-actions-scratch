#!/usr/bin/env bash
# Build the development image. This is the ONLY step that uses the network,
# and only for the sources listed at the top of container/Containerfile.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

podman build --pull=missing -t "$IMAGE" -f "${REPO_DIR}/container/Containerfile" "${REPO_DIR}/container"

echo "== image: $IMAGE"
podman image inspect "$IMAGE" --format '{{range $k, $v := .Labels}}{{$k}}={{$v}}{{"\n"}}{{end}}' \
  | grep -E '^(org.opencontainers.image.base.name|rmw_intraprocess\.)'
