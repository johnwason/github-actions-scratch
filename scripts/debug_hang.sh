#!/usr/bin/env bash
# Reproduces the local transport connector deadlock (gen/upstream_merge/README.md)
# and saves native backtraces. Run with
#   RRGO_PODMAN_ARGS="--cap-add=SYS_PTRACE --security-opt seccomp=unconfined" scripts/podman.sh shell bash scripts/debug_hang.sh
set -u
cd /work/robotraconteur_go
go test -c -o build/interop.test ./test/interop
go test -c -o build/crosscheck.test ./test/crosscheck
for i in 1 2 3 4 5 6 7 8; do
  (cd test/interop && ../../build/interop.test > /tmp/io.log 2>&1) & p1=$!
  (cd test/crosscheck && ../../build/crosscheck.test > /tmp/cc.log 2>&1) & p2=$!
  sleep 30
  hung=0
  for p in $p1 $p2; do
    if kill -0 $p 2>/dev/null; then
      hung=1
      t=$(pgrep -P $p -f '\.test$' | head -1)
      echo "=== run $i: hung pid $t ($(cat /proc/$t/cmdline | tr '\0' ' '))"
      gdb -p $t -batch -ex "thread apply all bt 25" 2>/dev/null | grep -vE "^\[New LWP|^$" > build/hang_bt_$t.txt
      wc -l build/hang_bt_$t.txt
      kill -9 $t
    fi
  done
  wait
  [ $hung = 1 ] && break
  echo "run $i ok"
done
