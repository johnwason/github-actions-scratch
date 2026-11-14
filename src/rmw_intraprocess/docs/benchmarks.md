# Benchmarks (M10)

`scripts/benchmark.sh [variant]` runs `bench_pubsub` (rclcpp, one process,
rclcpp intra-process comms off so the RMW carries every message) against this
RMW and the reference `rmw_fastrtps_cpp`, in the same container
(`--network=none`):

- **latency:** ping-pong between two nodes on one SingleThreadedExecutor
  (the round trip includes two publishes, two takes and the executor);
- **throughput:** one publisher bursts messages (RELIABLE, KEEP_LAST deep
  enough to lose none) to a subscription spun on another thread, timed until
  the last one arrives.

Payload: `std_msgs/UInt8MultiArray` with `data` of the given size.

## Results (2026-11-14, release build, development host, one run)

Absolute numbers depend on the machine and vary from run to run (the host was
not otherwise idle); compare the two RMWs within one run.

| Payload | RTT median, this RMW | RTT median, fastrtps | RTT p99, this RMW | RTT p99, fastrtps |
|---|---|---|---|---|
| 64 B | 3.3 µs | 4.6 µs | 6.8 µs | 19.9 µs |
| 4 KB | 3.6 µs | 4.8 µs | 7.4 µs | 17.6 µs |
| 256 KB | 20.0 µs | 21.9 µs | 109.9 µs | 132.3 µs |
| 1 MB | 888 µs | 859 µs | 1294 µs | 1274 µs |

| Payload | Throughput, this RMW | Throughput, fastrtps |
|---|---|---|
| 64 B | 639 k msg/s | 166 k msg/s |
| 4 KB | 224 k msg/s (916 MB/s) | 153 k msg/s (627 MB/s) |
| 256 KB | 67 k msg/s (17.6 GB/s) | 29 k msg/s (7.7 GB/s) |
| 1 MB | 8.3 k msg/s (8.7 GB/s) | 9.7 k msg/s (10.1 GB/s) |

## Reading the numbers

- Up to 256 KB this RMW has lower latency (a shorter, lock-light delivery path
  and no DDS history/writer machinery) and up to 3.8x the message rate.
- At 1 MB both are dominated by CDR serialization and deserialization (each
  message is serialized once per publish and deserialized once per take, with
  the same rosidl_typesupport_fastrtps code in both RMWs), and fastrtps is
  slightly ahead on throughput. The deferred deep-copy fast path for a
  publisher and subscription with the same type support (docs/design.md,
  "Message delivery") is the lever if large messages ever matter; rclcpp's
  intra-process comms already avoid the copy entirely for rclcpp-to-rclcpp.
- Not measured here: rclcpp intra-process comms (zero copy, RMW-independent),
  and Windows (the CI runner is not a benchmarking machine).
