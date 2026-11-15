#!/usr/bin/env bash
# Generate Go packages for the upstream test service definitions into test/gen
set -euo pipefail
cd /work/robotraconteur_go
rm -rf test/gen && mkdir -p test/gen
build/go/RobotRaconteurGenGo --thunksource --go-import-prefix=github.com/robotraconteur/robotraconteur_go/test/gen \
  -I ../robotraconteur/test/robdef --output-dir test/gen \
  com.robotraconteur.testing.TestService1.robdef com.robotraconteur.testing.TestService2.robdef \
  com.robotraconteur.testing.TestService3.robdef com.robotraconteur.testing.TestService4.robdef \
  com.robotraconteur.testing.TestService5.robdef > /dev/null
gofmt -w test/gen
# examples (same commands as their go:generate directives)
(cd examples/reynard_the_robot && rm -rf gen && PATH=/work/robotraconteur_go/build/go:$PATH go generate generate.go)
cp ../robotraconteur/test/data/test_vectors.bin test/testvectors/test_vectors.bin
