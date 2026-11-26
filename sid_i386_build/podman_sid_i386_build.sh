#!/bin/sh

# Run the i386 Debian sid build at full speed, then the tests on one core limited to a quarter of a CPU with the Java
# JIT disabled. Run from the parent dir of the Robot Raconteur source tree. CPU_QUOTA overrides the quota (cores).

set -e

if [ ! -f `pwd`/src/robotraconteur/tools/build_files/sid_i386_build/sid_i386_build.sh ]; then
    echo "This script must be run from the parent dir of the Robot Raconteur source tree"
    exit 1
fi

SCRIPT=/work/src/robotraconteur/tools/build_files/sid_i386_build/sid_i386_build.sh
CPU_QUOTA=${CPU_QUOTA:-0.25}

podman rm -f rr-sid-i386-build >/dev/null 2>&1 || true
podman run --platform linux/386 --name rr-sid-i386-build -v `pwd`:/work -w /work docker.io/i386/debian:sid \
    $SCRIPT build
podman commit rr-sid-i386-build rr-sid-i386-build-image
podman rm rr-sid-i386-build

podman run --platform linux/386 --rm --cpuset-cpus=0 --cpus=$CPU_QUOTA --cpu-period=10000 \
    -e JAVA_TOOL_OPTIONS=-Xint -v `pwd`:/work -w /work rr-sid-i386-build-image $SCRIPT test
