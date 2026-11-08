#!/usr/bin/env bash
# Find the newest usable vcpkg cache artifact produced by a trusted workflow run.
#
# Trusted: a completed run of WORKFLOW (a workflow file name) on BRANCH (default: the repository's
# default branch), in this repository, not triggered by a pull request. A pull request, from a fork
# or not, can never supply binaries this way: its runs are skipped even when their branch matches.
#
# env: GH_TOKEN, WORKFLOW, NAME (artifact name), BRANCH (optional), CACHE_REPOSITORY (optional),
#      MAX_RUNS (optional, default 20)
# writes to $GITHUB_OUTPUT: found=true|false, run-id=<id>, repository=<owner/repo>
set -euo pipefail

# gh api prints the error body to stdout on failure, --jq or not: a failed call yields nothing
api() { gh api "$@" 2>/dev/null || true; }

# CACHE_REPOSITORY: owner/repo, 'auto' (this repository, then its parent if it is a fork), or empty
# (this repository). Each candidate repository is searched with its own default branch unless
# BRANCH is given.
case "${CACHE_REPOSITORY:-}" in
  "") repos="$GITHUB_REPOSITORY" ;;
  auto)
    parent=$(api "repos/$GITHUB_REPOSITORY" --jq '.parent.full_name // empty' | grep -v '^{' || true)
    repos="$GITHUB_REPOSITORY${parent:+ $parent}" ;;
  *) repos="$CACHE_REPOSITORY" ;;
esac
max_runs="${MAX_RUNS:-20}"

for repo in $repos; do
  branch="${BRANCH:-}"
  if [ -z "$branch" ]; then
    branch=$(api "repos/$repo" --jq .default_branch | grep -v '^{' || true)
    [ -n "$branch" ] || { echo "cannot read $repo"; continue; }
  fi
  echo "Looking for artifact '$NAME' from $WORKFLOW on $branch in $repo"
  # completed, not only successful: a producer matrix run fails as a whole when one platform
  # fails, but the platforms that succeeded uploaded valid artifacts
  runs=$(api "repos/$repo/actions/workflows/$WORKFLOW/runs?branch=$branch&status=completed&per_page=$max_runs" \
    --jq ".workflow_runs[] | select(.event != \"pull_request\" and .event != \"pull_request_target\" and .head_repository.full_name == \"$repo\") | .id" \
    | grep -E '^[0-9]+$' || true)
  for run in $runs; do
    artifact=$(api "repos/$repo/actions/runs/$run/artifacts?name=$NAME" \
      --jq '.artifacts[] | select(.expired == false) | .id' | grep -E '^[0-9]+$' | head -n 1 || true)
    if [ -n "$artifact" ]; then
      echo "Found artifact $artifact in run $run of $repo"
      echo "found=true" >> "$GITHUB_OUTPUT"
      echo "run-id=$run" >> "$GITHUB_OUTPUT"
      echo "repository=$repo" >> "$GITHUB_OUTPUT"
      exit 0
    fi
  done
done

echo "No usable artifact '$NAME' found; vcpkg builds every package"
echo "found=false" >> "$GITHUB_OUTPUT"
