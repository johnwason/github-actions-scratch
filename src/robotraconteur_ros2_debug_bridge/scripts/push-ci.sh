#!/usr/bin/env bash
# Push a snapshot of committed HEAD (this repo and ../rmw_intraprocess) to the
# CI scratch repo (CLAUDE.md S3; the rules of S6 in ../rmw_intraprocess/CLAUDE.md).
# Runs on the host: git plumbing plus one push, nothing else.
#
# The snapshot is a single orphan commit (no history) with this layout:
#   .github/workflows/                  <- ci/github/workflows/
#   src/robotraconteur_ros2_debug_bridge/  <- this repo's HEAD tree
#   src/rmw_intraprocess/               <- ../rmw_intraprocess HEAD tree
# It is built with a temporary index; the sibling's objects are read through
# git alternates (nothing is copied into either repository). The working
# trees, real indexes and local branches are untouched, and no remote is added.
#
# Usage: scripts/push-ci.sh [branch-suffix]   (default: robotraconteur_ros2_debug_bridge)
# Env:   CI_TOKEN_FILE (default: <repo>/../github_ros2_rmw_token.txt)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RMW_ROOT="$(cd "${REPO_ROOT}/../rmw_intraprocess" && pwd)"
CI_URL="https://github.com/johnwason/github-actions-scratch.git"
SUFFIX="${1:-robotraconteur_ros2_debug_bridge}"
if [[ ! "${SUFFIX}" =~ ^[A-Za-z0-9_.-]+$ ]]; then
  echo "invalid branch suffix: ${SUFFIX}" >&2
  exit 2
fi
BRANCH="ci/${SUFFIX}"
export CI_TOKEN_FILE="${CI_TOKEN_FILE:-${REPO_ROOT}/../github_ros2_rmw_token.txt}"
if [[ ! -r "${CI_TOKEN_FILE}" ]]; then
  echo "token file not readable: ${CI_TOKEN_FILE}" >&2
  exit 2
fi

for r in "${REPO_ROOT}" "${RMW_ROOT}"; do
  if [[ -n "$(git -C "$r" status --porcelain --untracked-files=no)" ]]; then
    echo "note: uncommitted changes in $r are NOT included (snapshot is HEAD)" >&2
  fi
done
cd "${REPO_ROOT}"
HEAD_SHA="$(git rev-parse HEAD)"
RMW_SHA="$(git -C "${RMW_ROOT}" rev-parse HEAD)"
RMW_TREE="$(git -C "${RMW_ROOT}" rev-parse 'HEAD^{tree}')"

export GIT_ALTERNATE_OBJECT_DIRECTORIES="$(git -C "${RMW_ROOT}" rev-parse --absolute-git-dir)/objects"
TMP_INDEX="$(mktemp)"
trap 'rm -f "${TMP_INDEX}"' EXIT
export GIT_INDEX_FILE="${TMP_INDEX}"
rm -f "${TMP_INDEX}"
git read-tree --empty
git read-tree --prefix=src/robotraconteur_ros2_debug_bridge/ HEAD
git read-tree --prefix=src/rmw_intraprocess/ "${RMW_TREE}"
while IFS= read -r wf; do
  git update-index --add --cacheinfo "100644,$(git rev-parse "HEAD:${wf}"),.github/workflows/${wf##*/}"
done < <(git ls-tree --name-only HEAD ci/github/workflows/)
TREE="$(git write-tree)"
unset GIT_INDEX_FILE

COMMIT="$(git -c user.name="robotraconteur_ros2_debug_bridge CI" -c user.email="ci@invalid" \
  commit-tree "${TREE}" \
  -m "CI snapshot of robotraconteur_ros2_debug_bridge ${HEAD_SHA}" \
  -m "rmw_intraprocess ${RMW_SHA}")"

echo "pushing ${COMMIT} (snapshot of ${HEAD_SHA} + rmw_intraprocess ${RMW_SHA}) to ${BRANCH}"
# The token is read by the credential helper at push time; it never appears in
# argv, the git config or the commit.
# shellcheck disable=SC2016
git -c credential.helper= \
  -c 'credential.helper=!f() { echo username=x-access-token; printf "password=%s\n" "$(tr -d "[:space:]" < "${CI_TOKEN_FILE}")"; }; f' \
  push --no-verify "${CI_URL}" "+${COMMIT}:refs/heads/${BRANCH}"
echo "pushed. Status: scripts/ci-gh.sh run list -b ${BRANCH}"
