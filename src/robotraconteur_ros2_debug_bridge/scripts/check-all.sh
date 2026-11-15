#!/usr/bin/env bash
# The local gate: portability, then build + test for each variant
# (default: release asan).
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
variants=("$@")
[[ ${#variants[@]} -eq 0 ]] && variants=("${DEFAULT_VARIANTS[@]}")
"${REPO_DIR}/scripts/check-portability.sh"
for v in "${variants[@]}"; do
  echo "================ variant: $v ================"
  "${REPO_DIR}/scripts/build.sh" "$v"
  "${REPO_DIR}/scripts/test.sh" "$v"
done
echo "check-all: OK (${variants[*]})"
