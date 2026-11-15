#!/usr/bin/env bash
# Checks gen/upstream_merge: applies the RobotRaconteurGen --lang=go patch to
# a temporary copy of the upstream generator, adds GoServiceLangGen, builds
# it against the installed core and compares its output with test/golden.
# Runs inside the dev container after scripts/build.sh.
set -euo pipefail
GO_DIR=/work/robotraconteur_go
RR_DIR=/work/robotraconteur
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cp -r "$RR_DIR/RobotRaconteurGen" "$T/"
git -C "$T" apply "$GO_DIR/gen/upstream_merge/RobotRaconteurGen-lang-go.patch"
cp "$GO_DIR/gen/GoServiceLangGen.cpp" "$GO_DIR/gen/GoServiceLangGen.h" "$T/RobotRaconteurGen/"
SRCS=$(cd "$T/RobotRaconteurGen" && sed -n '/^set(RobotRaconteurGen_src/,/)/p' CMakeLists.txt | sed 's/#.*//' | tr ' ()' '\n\n\n' | grep '\.cpp$')
(cd "$T/RobotRaconteurGen" && g++ -std=c++11 -O0 -w -I "$GO_DIR/build/install/include" $SRCS -o "$T/RobotRaconteurGen.bin" \
  -Wl,--start-group "$GO_DIR/build/install/lib/libRobotRaconteurCore.a" -lboost_date_time -lboost_filesystem -lboost_atomic \
  -lboost_regex -lboost_chrono -lboost_thread -lboost_random -lboost_program_options -lssl -lcrypto -ldl -Wl,--end-group -lpthread)
mkdir "$T/out"
"$T/RobotRaconteurGen.bin" --thunksource --lang=go --go-import-prefix=github.com/robotraconteur/robotraconteur_go/test/gen \
  -I "$RR_DIR/test/robdef" --output-dir "$T/out" com.robotraconteur.testing.TestService1.robdef \
  com.robotraconteur.testing.TestService2.robdef com.robotraconteur.testing.TestService3.robdef > /dev/null
n=0
for f in "$T"/out/*/*.go; do
  cmp "$f" "$GO_DIR/test/golden/$(basename "$f").golden"
  n=$((n + 1))
done
[ "$n" = 3 ] || { echo "expected 3 generated files, got $n"; exit 1; }
# the other languages still work
mkdir "$T/cs"
"$T/RobotRaconteurGen.bin" --thunksource --lang=csharp -I "$RR_DIR/test/robdef" --output-dir "$T/cs" \
  com.robotraconteur.testing.TestService1.robdef com.robotraconteur.testing.TestService2.robdef > /dev/null
ls "$T/cs"/*.cs > /dev/null
echo "upstream merge patch OK"
