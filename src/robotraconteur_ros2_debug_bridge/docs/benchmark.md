# Benchmark: large topics and the fast path (B12)

**Result:** with `rmw_intraprocess_cpp`, the fast path (its tap API)
lowers the bridge's latency by up to 1.8 ms (at most 15 %, in some cases not at all). It does not lower
CPU use. In exchange it makes the *application's* `publish()` slower by
0.2 to 0.75 ms per message, because the tap converts the message on the
publishing thread. A debugging bridge should not slow the system it
observes, so **`fast_path` is off by default**. Turn it on
(`fast_path: true`) when bridge latency matters more than publisher time.

## What is measured

`scripts/benchmark.sh` builds nothing. It runs `test/benchmark.py` against
the release build, inside the container (`--network=none`), under
`rmw_intraprocess_cpp` and then `rmw_fastrtps_cpp`.

- **Host.** `test/benchmark_host.cpp` runs the bridge and one publisher
  node in one process, on a single-threaded executor. It publishes:
  - `sensor_msgs/Image`, rgb8: 640x480 (0.9 MB) and 1920x1080 (6.2 MB);
  - `sensor_msgs/PointCloud2`, x y z intensity float32: 65,536 points
    (1 MB) and 262,144 points (4.2 MB).
  It publishes each at 30 Hz and at 200 Hz. `header.stamp` is the system
  time just before `publish()`.
- **Client.** The benchmark script is a Python Robot Raconteur client in a
  second process, over LocalTransport. It subscribes through the pipe
  (every message, queued) and then through the wire (latest value only),
  5 s each after 1 s of warm-up.
- **Columns.**
  - *recv Hz:* the rate the client receives.
  - *p50 ms / p99 ms:* latency, receive time minus `header.stamp`. Both are
    system time on one machine.
  - *host %:* CPU use of the host process (100 % is one core).
  - *pub us:* the publisher's mean time in `publish()`.
  - *none* rows: the same host with no bridge subscriber, the
    publisher's own cost.
- **Machine.** AMD Ryzen 9 9950X3D, 12 CPUs visible to the container.
  Robot Raconteur 7cd1610c. One run, so differences under about 10 % are
  noise.

Run it with `scripts/build.sh release && scripts/benchmark.sh` (about 12
minutes; `--duration S`, `--quick`, `--json FILE`). `test_benchmark_smoke`
runs a 1 s case under both RMWs with the other tests, so the benchmark keeps
working.

## Results, rmw_intraprocess_cpp

| message | rate | fast path | via | recv Hz | p50 ms | p99 ms | host % | pub us |
|---|---:|---|---|---:|---:|---:|---:|---:|
| image 640x480 | 30 | | none | | | | 1.2 | 93 |
| | 30 | off | pipe | 30.2 | 1.96 | 2.89 | 4.6 | 130 |
| | 30 | **on** | pipe | 30.0 | 1.73 | 2.45 | 4.4 | **423** |
| | 30 | off | wire | 30.2 | 1.97 | 2.98 | 4.4 | 121 |
| | 30 | **on** | wire | 30.2 | 1.70 | 2.39 | 4.2 | **405** |
| | 200 | off | pipe | 200.0 | 1.64 | 2.44 | 19.2 | 74 |
| | 200 | **on** | pipe | 200.0 | 1.37 | 2.16 | 18.8 | **255** |
| image 1920x1080 | 30 | | none | | | | 3.0 | 697 |
| | 30 | off | pipe | 30.0 | 9.39 | 12.26 | 19.6 | 872 |
| | 30 | **on** | pipe | 30.0 | 8.19 | 10.00 | 18.8 | **1625** |
| | 200 | off | pipe | 172.2 | 186.41 | 205.26 | 118.2 | 750 |
| | 200 | **on** | pipe | 174.8 | 181.27 | 197.08 | 103.0 | **1247** |
| | 200 | off | wire | 173.8 | 12.03 | 15.37 | 113.6 | 724 |
| | 200 | **on** | wire | 174.2 | 10.19 | 14.88 | 98.6 | **1169** |
| cloud 65,536 points | 30 | off | pipe | 30.0 | 2.14 | 3.09 | 4.6 | 136 |
| | 30 | **on** | pipe | 30.0 | 1.91 | 2.75 | 4.4 | **476** |
| | 200 | off | pipe | 200.0 | 1.62 | 2.36 | 19.6 | 73 |
| | 200 | **on** | pipe | 200.0 | 1.56 | 2.43 | 19.6 | **273** |
| cloud 262,144 points | 30 | off | pipe | 30.0 | 6.50 | 8.22 | 13.0 | 522 |
| | 30 | **on** | pipe | 30.0 | 5.85 | 17.99 | 13.2 | **1209** |
| | 200 | off | wire | 200.0 | 6.47 | 9.50 | 77.8 | 418 |
| | 200 | **on** | wire | 186.6 | 6.54 | 16.46 | 77.8 | **966** |

All rows are in [benchmark_results.txt](benchmark_results.txt).

## Results, rmw_fastrtps_cpp

The tap does not exist, so `fast_path` changes nothing (both settings
measure the same; all rows in the results file).

| message | rate | via | recv Hz | p50 ms | p99 ms | host % | pub us |
|---|---:|---|---:|---:|---:|---:|---:|
| image 640x480 | 30 | pipe | 30.0 | 2.07 | 2.83 | 4.8 | 181 |
| image 1920x1080 | 30 | pipe | 30.0 | 9.65 | 13.11 | 20.4 | 594 |
| image 1920x1080 | 200 | pipe | 160.0 | 200.88 | 211.43 | 120.4 | 649 |
| image 1920x1080 | 200 | wire | 160.2 | 12.81 | 16.94 | 121.6 | 649 |
| cloud 65,536 points | 200 | pipe | 200.0 | 1.88 | 3.04 | 24.4 | 111 |
| cloud 262,144 points | 200 | wire | 200.2 | 6.09 | 7.79 | 75.0 | 332 |

## Findings

- **The fast path moves work; it does not remove it.** `rmw_intraprocess_cpp`
  serializes every publication for its subscriptions anyway (the tap's
  `cdr` is always set). Without the tap, the bridge deserializes on its own
  proxy thread. With the tap, the bridge skips that deserialization and the
  executor hop, but converts to Robot Raconteur on the publisher's thread,
  inside `publish()`. Host CPU is the same either way, within noise.
- **Latency.** The fast path is up to 1.8 ms faster at the median
  (at most 15 %; 262,144 points at 200 Hz: no gain). For a debug client the gain is small next to the transport
  and the client itself.
- **The application pays.** With the fast path, `publish()` takes 0.2 ms
  (0.9 MB image) to 0.75 ms (6.2 MB image) longer for each message the
  bridge subscribes to. With it off, the bridge adds 0.03 to 0.2 ms.
  Copying the native message on the publishing thread instead, to convert
  it elsewhere, would cost about what the deserialization costs now, so
  this cannot be fixed inside the bridge.
- **Wire vs pipe.** When the client cannot keep up (1080p at 200 Hz: about
  175 Hz received here, 160 Hz with Fast DDS), the pipe queues and its
  latency grows to about 200 ms. The wire keeps only the latest value and
  stays near 10 to 13 ms. Below the limit they are the same. The client
  chooses; the bridge offers both for every topic.
- **The limit is the Robot Raconteur path, not ROS.** At 1080p and 200 Hz
  (1.2 GB/s), the host uses about one core, mostly serializing to the
  Robot Raconteur client. The publisher keeps 197 Hz.
- **The two RMWs perform alike through the bridge.** `rmw_intraprocess_cpp`
  with the fast path off and `rmw_fastrtps_cpp` are within about 10 % on
  latency and CPU.

## Limits of this benchmark

- One machine and one run. The Python client converts each value to numpy
  arrays, and its own speed is part of the result; a C++ client would
  receive faster.
- Only LocalTransport. TCP adds the kernel's loopback or network path.
- Latency uses the system clock on both sides of one machine; it is not
  valid across machines.
