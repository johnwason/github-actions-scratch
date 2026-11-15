#!/usr/bin/env bash
# Public headers (include/ of every package in this repo) must not include
# Robot Raconteur or Boost headers, nor name their types: RR/Boost stay behind
# the component's DLL boundary (CLAUDE.md, Windows portability).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
fail=0
count=0
while IFS= read -r -d '' h; do
  count=$((count + 1))
  if grep -nE '#[[:space:]]*include[[:space:]]*[<"](RobotRaconteur|boost)[/.]|\bRobotRaconteur::|\bboost::|\bRR_SHARED_PTR\b|\bRR_INTRUSIVE_PTR\b' "$h"; then
    echo "public header exposes Robot Raconteur/Boost: ${h#"$root"/}" >&2
    fail=1
  fi
done < <(find "$root" -path '*/include/*' \( -name '*.h' -o -name '*.hpp' \) -not -path '*/test/*' -print0)
if [[ $fail -ne 0 ]]; then
  echo "public header check: FAILED" >&2
  exit 1
fi
echo "public header check: OK (${count} headers)"
