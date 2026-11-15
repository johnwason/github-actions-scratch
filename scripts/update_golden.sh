#!/usr/bin/env bash
# Regenerate test/golden from the current generator (run inside the dev container).
set -euo pipefail
cd /work/robotraconteur_go
cmake -DUPDATE=ON -DGEN=$PWD/build/go/RobotRaconteurGenGo -DROBDEF_DIR=/work/robotraconteur/test/robdef \
  -DOUT=$PWD/build/go/gen_golden -DGOLDEN=$PWD/test/golden -P cmake/check_golden.cmake
