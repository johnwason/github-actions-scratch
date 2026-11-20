#!/usr/bin/env bash
# Runs inside the project container (see scripts/verify.sh).
set -euo pipefail
cd /work
source scripts/pins.sh
DEPS=/work/.deps
JOBS=${JOBS:-$(nproc)}

echo "== architecture: $(uname -m)"

for stamp in lua54 luajit compat53 rr venv; do
  [ -f "$DEPS/stamps/$stamp" ] || { echo "missing .deps step '$stamp': run scripts/bootstrap.sh" >&2; exit 1; }
done

echo "== static checks"
bash scripts/container/static-checks.sh

configure_and_test() {
  local flavor=$1 incdir=$2 lib=$3 interp=$4
  local bdir=/work/build/$flavor
  echo "== $flavor: configure"
  cmake -S /work -B "$bdir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DRRLUA_LUA_FLAVOR="$flavor" \
    -DRRLUA_LUA_INCLUDE_DIR="$incdir" \
    -DRRLUA_LUA_LIBRARY="$lib" \
    -DRRLUA_LUA_INTERPRETER="$interp" \
    -DRRLUA_COMPAT53_DIR="$DEPS/src/lua-compat-5.3" \
    -DCMAKE_PREFIX_PATH="$DEPS/rr"
  echo "== $flavor: build"
  cmake --build "$bdir" -j "$JOBS"
  echo "== $flavor: test"
  ctest --test-dir "$bdir" --output-on-failure -j "$JOBS"
}

configure_and_test lua54 "$DEPS/lua54/include" "$DEPS/lua54/lib/liblua.a" "$DEPS/lua54/bin/lua"
configure_and_test luajit "$DEPS/luajit/include/luajit-2.1" "$DEPS/luajit/lib/libluajit-5.1.a" \
  "$DEPS/luajit/bin/luajit"

echo "== verify: all passed"
