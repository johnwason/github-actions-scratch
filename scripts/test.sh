#!/usr/bin/env bash
# Runs inside the dev container with --network=none (see scripts/podman.sh).
# Only loopback/intra/local transports are available; nothing can reach the
# local network.
set -euo pipefail
GO_DIR=/work/robotraconteur_go
cd "$GO_DIR"
export CGO_ENABLED=1
# Keep link/compile parallelism bounded on small hosts
export GOMAXPROCS=${GOMAXPROCS:-4}
export GOFLAGS="${GOFLAGS:-} -p=2"
PKGS=${*:-./...}
# -unsafeptr=false: C++ owned memory (RRBaseArray::void_ptr, SWIG handles) is
# reached through uintptr by design; it is never Go heap memory.
go vet -unsafeptr=false $PKGS
go test -count=1 -timeout 600s $PKGS
