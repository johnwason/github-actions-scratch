# robotraconteur_ros2_debug_bridge

A ROS 2 (Jazzy) component that runs inside a single-process ROS 2 application
and exposes selected topics, services, actions, parameters and the ROS graph
over Robot Raconteur, with a deny-by-default exposure policy. It is the only
external path for applications on `rmw_intraprocess_cpp` (sibling repo
`../rmw_intraprocess`) and the replacement for the `ros2` CLI and rqt there.
See [PLAN.md](PLAN.md) and [CLAUDE.md](CLAUDE.md).

## Building and testing (rootless podman, no network)

```sh
scripts/build-image.sh          # only step that uses the network (RR master is built here)
scripts/check-portability.sh    # portability + no-networking source check, public-header check
scripts/build.sh  [release|asan|tsan]
scripts/test.sh   [release|asan|tsan]   # no-network smoke test, then this repo's tests
scripts/check-all.sh            # all of the above for release + asan
scripts/update-golden.sh        # regenerate the type-mapper golden files (after build.sh release)
```

Robot Raconteur comes from GitHub `master`, built in the image into
`/opt/robotraconteur` (core, RobotRaconteurGen, Python 3 bindings). The
resolved commit SHAs of RR and of the reference
`robotraconteur_ros2_bridge` are image labels, printed by
`scripts/build-image.sh` and by every `scripts/test.sh` run. Rebuild with
`RR_CACHE_BUST=<new value> scripts/build-image.sh` to fetch a newer master.

The build workspace mounts `../rmw_intraprocess` read-only and builds
`rmw_intraprocess_cpp` (without its tests) alongside this repo, so the
bridge's tests can run against both RMWs. Containers run with
`--network=none`, `--cap-drop=all`, both repos mounted read-only, and output in
named volumes `robotraconteur_ros2_debug_bridge-{build,install,log}-<variant>`.
The tsan variant is disabled locally (host ASLR limitation, as in the sibling
repo).

## Debug CLI

`tools/robotraconteur_ros2_debug_cli.py` (installed in
`lib/robotraconteur_ros2_debug_bridge`) works like the ros2 CLI, through the
bridge. It speaks JSON in and out:

```sh
CLI=tools/robotraconteur_ros2_debug_cli.py
URL="rr+local:///?nodename=ros2_debug_bridge&service=ros2_debug_bridge"
$CLI "$URL" graph
$CLI "$URL" echo /chatter --type std_msgs/String -n 5     # --wire for latest values
$CLI "$URL" pub /cmd std_msgs/String '{"data": "hi"}' --via pipe
$CLI "$URL" call /add_two_ints example_interfaces/AddTwoInts '{"a": 1, "b": 2}'
$CLI "$URL" param get /my_node speed
$CLI "$URL" action /fibonacci action_tutorials_interfaces/Fibonacci '{"order": 5}'
$CLI "$URL" load /my_container demo_nodes_cpp demo_nodes_cpp::Talker
```

It starts only the transport the URL needs (local or TCP) and never runs
node discovery. What it may do is what the bridge's exposure rules and
capabilities allow; see docs/topics.md and docs/services_parameters.md.

## Performance

`scripts/benchmark.sh` measures large topics (images, point clouds) through
the pipe and the wire, under both RMWs. docs/benchmark.md has the results,
and why the `rmw_intraprocess_cpp` fast path (`fast_path`) is off by default.

## Robot Raconteur in tests

Robot Raconteur discovers peers over the LAN by default. Every test creates
its nodes through `rr_test_safety` (`test/rr_test_safety.{hpp,cpp,py}`):
LocalTransport/IntraTransport only, no announce or discovery, no command-line
overrides, TCP only on request and then only on localhost.
`test_safety_helper_usage.py` fails if a test or tool bypasses it.

## License
Apache-2.0. See [LICENSE](LICENSE).
