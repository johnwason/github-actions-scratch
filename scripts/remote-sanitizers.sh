#!/usr/bin/env bash
# Run the sanitizer suite (and verify.sh) on GitHub Actions in the scratch repository
# johnwason/github-actions-scratch, at the project owner's direction: TSan does not run on
# local hosts (see PROGRESS.md). Pushes a snapshot of HEAD's tree, with no history, as one
# orphan commit on branch rr-lua-sanitizers/<short sha>, with .github/scratch/sanitizers.yml
# installed as its workflow. Prints the run's URL.
#
# The token comes from the file named by RRLUA_SCRATCH_TOKEN_FILE. It is passed to git only
# through environment config for the one push, never written to any git config, and never
# printed.
#
# usage: RRLUA_SCRATCH_TOKEN_FILE=/path/to/token scripts/remote-sanitizers.sh
set -euo pipefail

REPO=johnwason/github-actions-scratch
root=$(git rev-parse --show-toplevel)
cd "$root"
: "${RRLUA_SCRATCH_TOKEN_FILE:?set RRLUA_SCRATCH_TOKEN_FILE to the token file}"
[ -z "$(git status --porcelain)" ] || { echo "commit or stash changes first" >&2; exit 1; }

sha=$(git rev-parse HEAD)
branch="rr-lua-sanitizers/$(git rev-parse --short=12 HEAD)"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

git archive HEAD | tar -x -C "$work"
rm -rf "$work/.github/workflows"
mkdir -p "$work/.github/workflows"
cp .github/scratch/sanitizers.yml "$work/.github/workflows/rr-lua-sanitizers.yml"

git -C "$work" init -q -b "$branch"
git -C "$work" add -A
git -C "$work" -c user.name="$(git config user.name)" -c user.email="$(git config user.email)" \
  commit -q -m "robotraconteur_lua sanitizers at $sha"

token=$(tr -d '[:space:]' < "$RRLUA_SCRATCH_TOKEN_FILE")
auth=$(printf 'x-access-token:%s' "$token" | base64 -w0)
GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=http.https://github.com/.extraheader \
  GIT_CONFIG_VALUE_0="AUTHORIZATION: basic $auth" \
  git -C "$work" push -q "https://github.com/$REPO.git" "HEAD:refs/heads/$branch"

echo "pushed $branch ($sha)"
echo "runs: https://github.com/$REPO/actions?query=branch%3A${branch//\//%2F}"
