#!/usr/bin/env bash
# Find the newest usable vcpkg cache artifact produced by a trusted workflow run.
#
# Trusted: a completed run of WORKFLOW (a workflow file name) on BRANCH (default: the repository's
# default branch), in this repository, not triggered by a pull request. A pull request, from a fork
# or not, can never supply binaries this way: its runs are skipped even when their branch matches.
#
# env: GH_TOKEN, WORKFLOW, NAME (artifact name), BRANCH (optional), MAX_RUNS (optional, default 20)
# writes to $GITHUB_OUTPUT: found=true|false, run-id=<id>
set -euo pipefail

repo="$GITHUB_REPOSITORY"
branch="${BRANCH:-}"
if [ -z "$branch" ]; then
  branch=$(gh api "repos/$repo" --jq .default_branch)
fi
max_runs="${MAX_RUNS:-20}"

echo "Looking for artifact '$NAME' from $WORKFLOW on $branch in $repo"
# completed, not only successful: a producer matrix run fails as a whole when one platform fails,
# but the platforms that succeeded uploaded valid artifacts
runs=$(gh api "repos/$repo/actions/workflows/$WORKFLOW/runs?branch=$branch&status=completed&per_page=$max_runs" \
  --jq ".workflow_runs[] | select(.event != \"pull_request\" and .event != \"pull_request_target\" and .head_repository.full_name == \"$repo\") | .id")

for run in $runs; do
  artifact=$(gh api "repos/$repo/actions/runs/$run/artifacts?name=$NAME" \
    --jq '.artifacts[] | select(.expired == false) | .id' | head -n 1)
  if [ -n "$artifact" ]; then
    echo "Found artifact $artifact in run $run"
    echo "found=true" >> "$GITHUB_OUTPUT"
    echo "run-id=$run" >> "$GITHUB_OUTPUT"
    exit 0
  fi
done

echo "No usable artifact '$NAME' found; vcpkg builds every package"
echo "found=false" >> "$GITHUB_OUTPUT"
