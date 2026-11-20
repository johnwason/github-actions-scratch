#!/usr/bin/env bash
# Run the project's CI on GitHub Actions in the scratch repository
# johnwason/github-actions-scratch, where all CI runs at the project owner's direction (TSan
# cannot run on local hosts; see PROGRESS.md). Pushes a snapshot of HEAD's tree, with no
# history, as one orphan commit with .github/scratch/ci.yml installed as its workflow, then
# prints the run's URL and ID.
#
#   scripts/remote-ci.sh           branch rr-lua-ci/<sha>: secret scan, verify.sh, sanitizers
#   scripts/remote-ci.sh --quick   branch rr-lua-ci-quick/<sha>: secret scan and verify.sh
#
# The token comes from the file named by RRLUA_SCRATCH_TOKEN_FILE. It is passed to git and
# curl only through the environment or a header for one command, never written to any git
# config, and never printed.
set -euo pipefail

REPO=johnwason/github-actions-scratch
prefix=rr-lua-ci
[ "${1:-}" = "--quick" ] && prefix=rr-lua-ci-quick

root=$(git rev-parse --show-toplevel)
cd "$root"
: "${RRLUA_SCRATCH_TOKEN_FILE:?set RRLUA_SCRATCH_TOKEN_FILE to the token file}"
[ -z "$(git status --porcelain)" ] || { echo "commit or stash changes first" >&2; exit 1; }

sha=$(git rev-parse HEAD)
branch="$prefix/$(git rev-parse --short=12 HEAD)"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

git archive HEAD | tar -x -C "$work"
rm -rf "$work/.github/workflows"
mkdir -p "$work/.github/workflows"
cp .github/scratch/ci.yml "$work/.github/workflows/rr-lua-ci.yml"

git -C "$work" init -q -b "$branch"
git -C "$work" add -A
git -C "$work" -c user.name="$(git config user.name)" -c user.email="$(git config user.email)" \
  commit -q -m "robotraconteur_lua CI at $sha"

token=$(tr -d '[:space:]' < "$RRLUA_SCRATCH_TOKEN_FILE")
auth=$(printf 'x-access-token:%s' "$token" | base64 -w0)
GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=http.https://github.com/.extraheader \
  GIT_CONFIG_VALUE_0="AUTHORIZATION: basic $auth" \
  git -C "$work" push -q "https://github.com/$REPO.git" "HEAD:refs/heads/$branch"
echo "pushed $branch ($sha)"

# Find the run (it can take a few seconds to appear).
for _ in $(seq 1 30); do
  run=$(curl -sS -H "Authorization: Bearer $token" \
    "https://api.github.com/repos/$REPO/actions/runs?branch=$branch&per_page=1" |
    python3 -c "import json,sys; r=json.load(sys.stdin).get('workflow_runs',[]); print(r[0]['id'] if r else '')")
  [ -n "$run" ] && break
  sleep 2
done
if [ -n "${run:-}" ]; then
  echo "run id: $run"
  echo "run: https://github.com/$REPO/actions/runs/$run"
else
  echo "run not found yet: https://github.com/$REPO/actions?query=branch%3A${branch//\//%2F}"
fi
