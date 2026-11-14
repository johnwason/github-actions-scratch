#!/usr/bin/env bash
# Push a snapshot of committed HEAD to the cross-platform CI scratch repo
# (CLAUDE.md S6). Runs on the host: git plumbing plus one push, nothing else.
#
# The snapshot is a single orphan commit (no history) with this layout:
#   .github/workflows/  <- ci/github/workflows/
#   pixi.toml           <- ci/pixi.toml
#   src/rmw_intraprocess/  <- the HEAD tree
# It is built with a temporary index, so the working tree, the real index and
# the local branches are untouched, and no remote is added.
#
# Usage: scripts/push-ci.sh [branch-suffix]   (default: rmw_intraprocess)
# Env:   CI_TOKEN_FILE (default: <repo>/../github_ros2_rmw_token.txt)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CI_URL="https://github.com/johnwason/github-actions-scratch.git"
SUFFIX="${1:-rmw_intraprocess}"
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

cd "${REPO_ROOT}"
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "note: uncommitted changes are NOT included (snapshot is HEAD)" >&2
fi
HEAD_SHA="$(git rev-parse HEAD)"

TMP_INDEX="$(mktemp)"
trap 'rm -f "${TMP_INDEX}"' EXIT
export GIT_INDEX_FILE="${TMP_INDEX}"
rm -f "${TMP_INDEX}"
git read-tree --empty
git read-tree --prefix=src/rmw_intraprocess/ HEAD
add_from_head() {  # <path in HEAD> <path in snapshot>
  local blob
  blob="$(git rev-parse "HEAD:$1")"
  git update-index --add --cacheinfo "100644,${blob},$2"
}
add_from_head ci/pixi.toml pixi.toml
while IFS= read -r wf; do
  add_from_head "${wf}" ".github/workflows/${wf##*/}"
done < <(git ls-tree --name-only HEAD ci/github/workflows/)
TREE="$(git write-tree)"
unset GIT_INDEX_FILE

COMMIT="$(git -c user.name="rmw_intraprocess CI" -c user.email="ci@invalid" \
  commit-tree "${TREE}" -m "CI snapshot of rmw_intraprocess ${HEAD_SHA}")"

echo "pushing ${COMMIT} (snapshot of ${HEAD_SHA}) to ${BRANCH}"
# The token is read by the credential helper at push time; it never appears in
# argv, the git config or the commit.
# shellcheck disable=SC2016
git -c credential.helper= \
  -c 'credential.helper=!f() { echo username=x-access-token; printf "password=%s\n" "$(tr -d "[:space:]" < "${CI_TOKEN_FILE}")"; }; f' \
  push --no-verify "${CI_URL}" "+${COMMIT}:refs/heads/${BRANCH}"
echo "pushed. Status: scripts/ci-gh.sh run list -b ${BRANCH}"
