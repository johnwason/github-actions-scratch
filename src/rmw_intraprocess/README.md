# rmw_intraprocess

ROS 2 (Jazzy) middleware implementation `rmw_intraprocess_cpp` that communicates
only within a single process. See [PLAN.md](PLAN.md) and [CLAUDE.md](CLAUDE.md).

## Building and testing (rootless podman, no network)

```sh
scripts/build-image.sh          # only step that uses the network
scripts/check-portability.sh    # portability + no-networking source check (with self-test)
scripts/build.sh  [release|asan|tsan]
scripts/test.sh   [release|asan|tsan]   # no-network smoke test, colcon test, symbol check
scripts/check-all.sh            # all of the above for release + asan (tsan disabled, see docs/unsupported.md)
scripts/upstream-test.sh <variant> [regex]   # upstream test_rmw_implementation suites
UPSTREAM_RMW=rmw_fastrtps_cpp scripts/upstream-test.sh release   # same suites on the reference RMW
```

Every build/test container runs with `--network=none`, `--cap-drop=all`, the
source mounted read-only, and output in named volumes
`rmw_intraprocess-{build,install,log}-<variant>`.

`scripts/in-container/` holds the scripts that run inside the container.
`scripts/portability/` holds the source checker and its self-test fixtures.

## License
Apache-2.0. See [LICENSE](LICENSE).

## Tests against the reference RMW
`test_rclcpp_pubsub` is registered twice, with `RMW_IMPLEMENTATION=rmw_intraprocess_cpp`
and with `rmw_fastrtps_cpp`, so rclcpp-visible behavior is compared with the
reference RMW in the same container (`--network=none`; Fast DDS only uses
loopback and shared memory there).
