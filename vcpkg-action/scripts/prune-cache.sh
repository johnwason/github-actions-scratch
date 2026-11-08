#!/usr/bin/env bash
# Delete binary cache archives the current install does not use.
#
# vcpkg's files binary cache stores each package as <cache>/<abi[0:2]>/<abi>.zip, named by the
# package's ABI hash; the status database records the ABI of every installed package (host tools
# included). Anything else in the cache is from an older package set or vcpkg version.
#
# env: VCPKG_ROOT, CACHE_DIR
set -euo pipefail

status="$VCPKG_ROOT/installed/vcpkg/status"
# paragraphs of the status file; keep the Abi of packages currently installed
abis=$(awk 'BEGIN { RS = ""; FS = "\n" }
  /\nStatus: install ok installed/ {
    for (i = 1; i <= NF; i++) if ($i ~ /^Abi: /) print substr($i, 6)
  }' "$status" | sort -u)
echo "$(echo "$abis" | grep -c .) installed package ABIs"

kept=0
removed=0
while IFS= read -r -d '' archive; do
  abi=$(basename "$archive" .zip)
  if grep -qxF "$abi" <<< "$abis"; then
    kept=$((kept + 1))
  else
    rm -f "$archive"
    removed=$((removed + 1))
  fi
done < <(find "$CACHE_DIR" -type f -name '*.zip' -print0)
find "$CACHE_DIR" -mindepth 1 -type d -empty -delete
echo "kept $kept archives, removed $removed"
