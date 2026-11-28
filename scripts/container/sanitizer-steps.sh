#!/usr/bin/env bash
# Runs inside the project container (see scripts/verify-sanitizers.sh).
#
# RR core is rebuilt with each sanitizer, because TSan is unreliable unless RR core is
# instrumented too. Lua itself and the Boost shared libraries are not instrumented.
# RR core's C++ test server and test server library are rebuilt with the same flags. Each
# links its own static RR core; an uninstrumented copy hides its atomics (shared_ptr reference
# counts) from TSan, which can then report races that are not real.
# Lua scripts run in an embedding test executable, not the stock interpreter, so the
# sanitizer runtime is linked into the executable rather than preloaded.
set -euo pipefail
cd /work
source scripts/pins.sh
DEPS=/work/.deps
JOBS=${JOBS:-$(nproc)}
# Sanitizer builds of RR core need about 3 GB per compiler process; cap the jobs by
# memory so small machines do not OOM-kill the compiler.
mem_kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
mem_jobs=$(( mem_kb / 3000000 )); [ "$mem_jobs" -ge 1 ] || mem_jobs=1
[ "$JOBS" -le "$mem_jobs" ] || JOBS=$mem_jobs
echo "== jobs: $JOBS"
sanitizers=("${@:-asan tsan}")
read -r -a sanitizers <<< "${sanitizers[*]}"

echo "== architecture: $(uname -m)"
[ -f "$DEPS/stamps/rr" ] || { echo "run scripts/bootstrap.sh first" >&2; exit 1; }

flags_for() {
  case "$1" in
    asan) echo "-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1" ;;
    tsan) echo "-fsanitize=thread -fno-omit-frame-pointer -g -O1" ;;
    *) echo "unknown sanitizer $1" >&2; return 1 ;;
  esac
}

for san in "${sanitizers[@]}"; do
  flags=$(flags_for "$san")
  rrb=$DEPS/build/robotraconteur-$san
  rri=$DEPS/rr-$san
  stamp="$RR_COMMIT $flags testserver"
  if [ ! -f "$DEPS/stamps/rr-$san" ] || [ "$(cat "$DEPS/stamps/rr-$san")" != "$stamp" ]; then
    echo "== $san: build RR core"
    rm -rf "$rrb" "$rri"
    cmake -S "$DEPS/src/robotraconteur" -B "$rrb" -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
      -DCMAKE_EXE_LINKER_FLAGS="$flags" -DCMAKE_SHARED_LINKER_FLAGS="$flags" \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF \
      -DBUILD_GEN=ON -DBUILD_TESTING=ON -DBUILD_DOCUMENTATION=OFF \
      -DCMAKE_INSTALL_PREFIX="$rri"
    cmake --build "$rrb" -j "$JOBS" --target RobotRaconteurCore RobotRaconteurGen \
      robotraconteur_test_server robotraconteur_test_server_lib
    cmake --install "$rrb"
    mkdir -p "$rri/test"
    for f in robotraconteur_test_server librobotraconteur_test_server_lib.so; do
      src=$(find "$rrb" -name "$f" -type f -print -quit)
      [ -n "$src" ] || { echo "$f not built" >&2; exit 1; }
      cp "$src" "$rri/test/"
    done
    echo "$stamp" > "$DEPS/stamps/rr-$san"
  fi

  bdir=/work/build/san-$san
  echo "== $san: build binding"
  cmake -S /work -B "$bdir" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
    -DCMAKE_EXE_LINKER_FLAGS="$flags" -DCMAKE_MODULE_LINKER_FLAGS="$flags" \
    -DRRLUA_LUA_FLAVOR=lua54 \
    -DRRLUA_LUA_INCLUDE_DIR="$DEPS/lua54/include" \
    -DRRLUA_LUA_LIBRARY="$DEPS/lua54/lib/liblua.a" \
    -DRRLUA_LUA_INTERPRETER="$DEPS/lua54/bin/lua" \
    -DRRLUA_COMPAT53_DIR="$DEPS/src/lua-compat-5.3" \
    -DRRLUA_TEST_SERVER="$rri/test/robotraconteur_test_server" \
    -DRRLUA_TEST_VECTORS="$DEPS/src/robotraconteur/test/data/test_vectors.bin" \
    -DRRLUA_TEST_SERVER_LIB="$rri/test/librobotraconteur_test_server_lib.so" \
    -DRRLUA_PYTHON="$DEPS/venv/bin/python" \
    -DRRLUA_SANITIZER_BUILD=ON \
    -DCMAKE_PREFIX_PATH="$rri"
  cmake --build "$bdir" -j "$JOBS"

  echo "== $san: run"
  export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:strict_string_checks=1
  export LSAN_OPTIONS=suppressions=/work/tests/sanitizers/lsan.supp:print_suppressions=1
  export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
  export TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1:suppressions=/work/tests/sanitizers/tsan.supp:print_suppressions=1
  ctest --test-dir "$bdir" --output-on-failure -L sanitizer
done

echo "== sanitizers: all passed"
