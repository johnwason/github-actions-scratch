#!/usr/bin/env bash
# Source checks that need no build. Fails on any finding.
set -euo pipefail
cd /work
status=0

src_dirs=()
for d in rr_lua_core module tests tesseract_lua; do
  [ -d "$d" ] && src_dirs+=("$d")
done

# Never use the RR singleton (CLAUDE.md, "No singleton").
if [ ${#src_dirs[@]} -gt 0 ]; then
  if grep -rnE '::(s|sp)\(\)' --include='*.[ch]' --include='*.[ch]pp' --include='*.cc' \
       --include='*.i' "${src_dirs[@]}"; then
    echo "error: RobotRaconteurNode singleton (::s() / ::sp()) used in project sources" >&2
    status=1
  fi
fi

# No absolute home or machine paths in tracked files.
if git -c safe.directory=/work ls-files -z | xargs -0 grep -nIE '(/home/[a-z_][a-z0-9_-]*/|/Users/[A-Za-z]|C:\\Users\\)' -- 2>/dev/null \
     | grep -v '^scripts/container/static-checks.sh:'; then
  echo "error: absolute user paths in tracked files" >&2
  status=1
fi

exit $status
