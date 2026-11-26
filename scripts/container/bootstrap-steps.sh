#!/usr/bin/env bash
# Runs inside the project container (see scripts/bootstrap.sh). Fetches and builds the
# pinned dependencies into /work/.deps. Each step writes a stamp containing its pin, so
# a step reruns only when its pin changes or its stamp is removed.
set -euo pipefail
cd /work
source scripts/pins.sh

DEPS=/work/.deps
JOBS=${JOBS:-$(nproc)}
mkdir -p "$DEPS"/{downloads,src,stamps,refs}

echo "container architecture: $(uname -m)"

done_step() { [ -f "$DEPS/stamps/$1" ] && [ "$(cat "$DEPS/stamps/$1")" = "$2" ]; }
mark_step() { echo "$2" > "$DEPS/stamps/$1"; }

# Clone one commit into a directory (shallow fetch by SHA).
fetch_commit() {
  local repo=$1 commit=$2 dir=$3
  if [ -d "$dir/.git" ] && [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" = "$commit" ]; then
    return 0
  fi
  rm -rf "$dir"
  git init -q "$dir"
  git -C "$dir" remote add origin "$repo"
  git -C "$dir" fetch --depth 1 origin "$commit"
  git -C "$dir" -c advice.detachedHead=false checkout FETCH_HEAD
  [ "$(git -C "$dir" rev-parse HEAD)" = "$commit" ]
}

# --- Lua 5.4, built as C (the usual host configuration) -------------------------------
if ! done_step lua54 "$LUA54_VERSION $LUA54_SHA256"; then
  tarball="$DEPS/downloads/lua-$LUA54_VERSION.tar.gz"
  [ -f "$tarball" ] || curl -fsSL --retry 3 -o "$tarball" "$LUA54_URL"
  echo "$LUA54_SHA256  $tarball" | sha256sum -c -
  rm -rf "$DEPS/src/lua-$LUA54_VERSION" "$DEPS/lua54"
  tar -C "$DEPS/src" -xzf "$tarball"
  # -fPIC so liblua.a can also be linked into test executables that load modules.
  make -C "$DEPS/src/lua-$LUA54_VERSION" -j"$JOBS" linux MYCFLAGS="-fPIC -g"
  make -C "$DEPS/src/lua-$LUA54_VERSION" install INSTALL_TOP="$DEPS/lua54"
  mark_step lua54 "$LUA54_VERSION $LUA54_SHA256"
fi

# --- LuaJIT ---------------------------------------------------------------------------
if ! done_step luajit "$LUAJIT_COMMIT"; then
  fetch_commit "$LUAJIT_REPO" "$LUAJIT_COMMIT" "$DEPS/src/luajit"
  rm -rf "$DEPS/luajit"
  # LuaJIT derives its version from git history; a shallow clone has none, so record
  # the commit time as the rolling version (what its Makefile would otherwise compute).
  git -C "$DEPS/src/luajit" show -s --format=%ct HEAD > "$DEPS/src/luajit/.relver"
  make -C "$DEPS/src/luajit" clean
  make -C "$DEPS/src/luajit" -j"$JOBS" PREFIX="$DEPS/luajit" CCDEBUG=-g
  make -C "$DEPS/src/luajit" install PREFIX="$DEPS/luajit"
  ln -sf "$(basename "$(ls "$DEPS"/luajit/bin/luajit-2.1.* | head -1)")" "$DEPS/luajit/bin/luajit"
  mark_step luajit "$LUAJIT_COMMIT"
fi

# --- lua-compat-5.3 (headers and C sources only) ----------------------------------------
if ! done_step compat53 "$COMPAT53_COMMIT"; then
  fetch_commit "$COMPAT53_REPO" "$COMPAT53_COMMIT" "$DEPS/src/lua-compat-5.3"
  mark_step compat53 "$COMPAT53_COMMIT"
fi

# --- Robot Raconteur core, generator and C++ test server ------------------------------
if ! done_step rr "$RR_COMMIT"; then
  fetch_commit "$RR_REPO" "$RR_COMMIT" "$DEPS/src/robotraconteur"
  rm -rf "$DEPS/build/robotraconteur" "$DEPS/rr"
  cmake -S "$DEPS/src/robotraconteur" -B "$DEPS/build/robotraconteur" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_GEN=ON -DBUILD_TESTING=ON -DBUILD_DOCUMENTATION=OFF \
    -DCMAKE_INSTALL_PREFIX="$DEPS/rr"
  cmake --build "$DEPS/build/robotraconteur" -j "$JOBS" --target \
    RobotRaconteurCore RobotRaconteurGen \
    robotraconteur_test_server robotraconteur_test_server_lib
  cmake --install "$DEPS/build/robotraconteur"
  mkdir -p "$DEPS/rr/test"
  cp "$DEPS/build/robotraconteur/test/out/bin/robotraconteur_test_server" "$DEPS/rr/test/"
  cp "$DEPS/build/robotraconteur/test/out/lib/librobotraconteur_test_server_lib.so" "$DEPS/rr/test/"
  mark_step rr "$RR_COMMIT"
fi

# --- Python test peer ----------------------------------------------------------------
if ! done_step venv "$PY_ROBOTRACONTEUR_VERSION $PY_NUMPY_VERSION $PY_RR_COMPANION_VERSION $PY_RR_ABSTRACT_ROBOT_VERSION"; then
  rm -rf "$DEPS/venv"
  python3 -m venv "$DEPS/venv"
  "$DEPS/venv/bin/pip" install "robotraconteur==$PY_ROBOTRACONTEUR_VERSION" "numpy==$PY_NUMPY_VERSION" \
    "robotraconteurcompanion==$PY_RR_COMPANION_VERSION" "robotraconteur-abstract-robot==$PY_RR_ABSTRACT_ROBOT_VERSION"
  "$DEPS/venv/bin/python" -c "import RobotRaconteur, importlib.metadata as m; print('python robotraconteur', m.version('robotraconteur'))"
  mark_step venv "$PY_ROBOTRACONTEUR_VERSION $PY_NUMPY_VERSION $PY_RR_COMPANION_VERSION $PY_RR_ABSTRACT_ROBOT_VERSION"
fi

# --- Reference implementations (clone only) -------------------------------------------
while read -r name repo commit; do
  [ -n "$name" ] || continue
  if ! done_step "ref-$name" "$commit"; then
    fetch_commit "$repo" "$commit" "$DEPS/refs/$name"
    mark_step "ref-$name" "$commit"
  fi
done <<< "$REFS"

echo "bootstrap complete"
