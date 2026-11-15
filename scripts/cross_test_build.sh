#!/usr/bin/env bash
# Cross-compile the Go bindings, examples and test binaries for windows/amd64
# (scripts/podman.sh mingw-tests, after scripts/podman.sh mingw-build).
# Test thunks are generated with a Linux RobotRaconteurGenGo (GEN, default
# build/go/RobotRaconteurGenGo from scripts/podman.sh build); generator output
# does not depend on the platform.
# Catches compile and link errors; the .exe files run on a Windows CI runner.
set -euo pipefail
GO_DIR=/work/robotraconteur_go
RR_SRC=${RR_SRC:-/work/robotraconteur}
GEN=${GEN:-$GO_DIR/build/go/RobotRaconteurGenGo}
OUT=${OUT:-$GO_DIR/build-mingw/tests}
cd "$GO_DIR"

rm -rf test/gen && mkdir -p test/gen
"$GEN" --thunksource --go-import-prefix=github.com/robotraconteur/robotraconteur_go/test/gen \
  -I "$RR_SRC/test/robdef" --output-dir test/gen \
  com.robotraconteur.testing.TestService1.robdef com.robotraconteur.testing.TestService2.robdef \
  com.robotraconteur.testing.TestService3.robdef com.robotraconteur.testing.TestService4.robdef \
  com.robotraconteur.testing.TestService5.robdef > /dev/null
gofmt -w test/gen
(cd examples/reynard_the_robot && rm -rf gen && mkdir -p /tmp/genbin && ln -sf "$GEN" /tmp/genbin/RobotRaconteurGenGo \
  && PATH=/tmp/genbin:$PATH go generate generate.go)
cp "$RR_SRC/test/data/test_vectors.bin" test/testvectors/test_vectors.bin

export GOOS=windows GOARCH=amd64 CGO_ENABLED=1
export CC=x86_64-w64-mingw32-gcc-posix CXX=x86_64-w64-mingw32-g++-posix
export GOFLAGS="${GOFLAGS:-} -p=2"

go vet -unsafeptr=false ./...
go build ./...
mkdir -p "$OUT" && rm -rf "$OUT"/*.test.exe "$OUT/tests.txt" "$OUT/bin"   # keeps cpp/ (build_mingw.sh)
# tests.txt: "<binary> <package dir>" per line; run each binary in its package
# directory, as go test does (scripts/run_windows_tests.sh)
MOD=$(go list -m)
for pkg in $(go list -f '{{if or .TestGoFiles .XTestGoFiles}}{{.ImportPath}}{{end}}' ./...); do
  rel=${pkg#"$MOD"/}
  [ "$rel" = "$MOD" ] && rel=.
  name=$(echo "$rel" | tr / _)
  go test -c -o "$OUT/$name.test.exe" "$pkg"
  echo "$name.test.exe $rel" >> "$OUT/tests.txt"
done
# example and test programs
for d in $(go list -f '{{if eq .Name "main"}}{{.Dir}}{{end}}' ./...); do
  go build -o "$OUT/bin/" "$d"
done
ls -la "$OUT" "$OUT/bin"; [ ! -d "$OUT/cpp" ] || ls -la "$OUT/cpp"
