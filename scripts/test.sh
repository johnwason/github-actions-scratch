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
# generate Go packages for the upstream test service definitions
scripts/gen_test.sh
PKGS=${*:-./...}
# -unsafeptr=false: C++ owned memory (RRBaseArray::void_ptr, SWIG handles) is
# reached through uintptr by design; it is never Go heap memory.
(cd build/go && ctest --output-on-failure)
go vet -unsafeptr=false $PKGS
if [ $# -eq 0 ]; then
  # test/interop and test/crosscheck each run C++ test programs against a
  # node; run them one package at a time so they do not interfere.
  go test -count=1 -timeout 600s $(go list ./... | grep -vE '/test/(interop|crosscheck)$')
  go test -count=1 -timeout 600s -p 1 ./test/interop ./test/crosscheck
else
  go test -count=1 -timeout 600s $PKGS
fi
