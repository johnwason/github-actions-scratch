#!/usr/bin/env bash
# Runs the Windows test binaries made by scripts/cross_test_build.sh, each in its
# package directory (as go test does). Runs on Windows (Git Bash), from the
# repository root:
#   scripts/run_windows_tests.sh [binary dir, default build-mingw/tests]
# The C++ test programs in <binary dir>/cpp are used when present.
# Only loopback, intra and local transports are used by the tests.
set -uo pipefail
ROOT=$(pwd)
BIN=$(cd "${1:-build-mingw/tests}" && pwd)
# C++ test programs (build_mingw.sh with RR_TESTING=ON) for test/interop and
# test/crosscheck; without them those tests skip.
if [ -d "$BIN/cpp" ]; then
  winpath() { if command -v cygpath >/dev/null; then cygpath -w "$1"; else echo "$1"; fi; }
  export RRGO_CPP_TEST_BIN_DIR="$(winpath "$BIN/cpp")"
  export RRGO_CPP_TEST_SERVER="$(winpath "$BIN/cpp/robotraconteur_test_server.exe")"
fi
failed=()
while read -r exe dir; do
  [ -n "$exe" ] || continue
  echo "=== $dir"
  if (cd "$ROOT/$dir" && "$BIN/$exe" -test.count=1 -test.timeout=600s); then
    echo "ok   $dir"
  else
    echo "FAIL $dir"
    failed+=("$dir")
  fi
done < "$BIN/tests.txt"
if [ ${#failed[@]} -ne 0 ]; then
  echo "failed packages: ${failed[*]}"
  exit 1
fi
