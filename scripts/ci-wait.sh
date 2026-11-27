#!/usr/bin/env bash
# Wait for a CI run in the scratch repository (see scripts/remote-ci.sh) and print each job's
# result as it completes. Exits 0 only if the run succeeded. With --latest instead of a run ID,
# reports the most recent rr-lua-ci run without waiting.
#
# usage: RRLUA_SCRATCH_TOKEN_FILE=... scripts/ci-wait.sh <run id> | --latest
set -euo pipefail
REPO=johnwason/github-actions-scratch
: "${RRLUA_SCRATCH_TOKEN_FILE:?set RRLUA_SCRATCH_TOKEN_FILE to the token file}"
token=$(tr -d '[:space:]' < "$RRLUA_SCRATCH_TOKEN_FILE")
api() { curl -sS -H "Authorization: Bearer $token" -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/$REPO/$1"; }

if [ "${1:-}" = "--latest" ]; then
  api "actions/runs?per_page=50" | python3 -c '
import json, sys
runs = [r for r in json.load(sys.stdin)["workflow_runs"] if r["name"] == "rr-lua-ci"]
if not runs:
    sys.exit("no rr-lua-ci runs")
r = runs[0]
print(r["id"], r["head_branch"], r["status"], r["conclusion"], r["html_url"])
sys.exit(0 if r["conclusion"] == "success" else 1)'
  exit
fi

run=${1:?run id}
seen=" "
while :; do
  while read -r id name concl; do
    [ -n "$id" ] || continue
    case "$seen" in *" $id "*) ;; *) echo "job $name: $concl"; seen="$seen$id ";; esac
  done < <(api "actions/runs/$run/jobs" | python3 -c '
import json, sys
for j in json.load(sys.stdin).get("jobs", []):
    if j["status"] == "completed":
        print(j["id"], j["name"].replace(" ", "_"), j["conclusion"])')
  st=$(api "actions/runs/$run" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["status"], d["conclusion"])')
  case "$st" in
    "completed success") echo "run $run: success"; exit 0 ;;
    completed*) echo "run $run: ${st#completed }"; exit 1 ;;
  esac
  sleep 60
done
