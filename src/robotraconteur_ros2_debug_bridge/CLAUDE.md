# robotraconteur_ros2_debug_bridge — instructions for Claude

This repo is a ROS 2 component that runs **inside** a single-process ROS 2
application and exposes selected topics, services, actions, parameters and the
ROS graph to the outside world over **Robot Raconteur (RR)**. It's the only
external communication path for applications using the intra-process-only RMW
in the sibling repo `../rmw_intraprocess`. It also replaces the `ros2` CLI,
rqt and similar tools for debugging.

The project plan, milestones and acceptance criteria are in [PLAN.md](PLAN.md).
Read it before starting any work, and work milestones in order.

---

## Safety rules (mandatory)

These rules override convenience. If a task seems to need breaking one, stop and
ask the user. Do not look for a workaround.

### S1. All builds and tests run in podman
- Never run `colcon`, `cmake`, `make`, `ninja`, `gcc`/`clang`, `pip install`,
  `apt`, or `rosdep` on the host. The only host-side actions are editing files,
  read-only inspection (`ls`, `cat`, `grep`, `git status/diff/log`), and the
  `podman` commands below.
- Use the scripts in `scripts/` (created in milestone B0) as the only entry
  points: `scripts/build-image.sh`, `scripts/build.sh`, `scripts/test.sh`.
- Use rootless podman only. Never use `sudo`, `--privileged`, `--cap-add`,
  `--pid=host`, `--ipc=host`, `--userns=host`, or `--security-opt label=disable`.
- Bind mounts are limited to this repo and `../rmw_intraprocess`, both
  read-only (`:ro,Z`). Build output goes to named podman volumes. Never mount
  `$HOME`, `/`, `/var/run`, the podman socket, SSH keys, or any credential directory.
- Do not push images, log in to registries, or remove images, containers or
  volumes not created by this project's scripts.

### S2. Never contact the local network
Robot Raconteur and DDS both **discover peers over the LAN by default**
(UDP broadcast/multicast node announce and discovery). Treat this as the main
safety hazard in this repo.
- **Containers that build or test must run with `--network=none`.** Only
  loopback exists inside them.
- Only `podman build` (creating the image) may use the network, and only to
  fetch from public package sources listed in the Containerfile: the
  container registry, Ubuntu/ROS apt mirrors, `github.com/robotraconteur/robotraconteur`
  (master), `github.com/robotraconteur-contrib/robotraconteur_ros2_bridge`
  (master, test reference only), and PyPI for pinned test-only Python packages.
  Note each new source in the commit message.
- **In tests, RR may only use `IntraTransport` or `LocalTransport`.** If a test
  truly needs `TcpTransport`, it must bind to `127.0.0.1` only and must
  not enable node announce or discovery listening. Never use
  `ServerNodeSetup`/`ClientNodeSetup` defaults in tests. Always pass explicit
  flags with announce and discovery disabled. Add a test helper that enforces
  this and use it everywhere.
- **Default runtime config must be safe too:** in the shipped default
  configuration, TCP transport, node announce and discovery listening are
  **off**. The user opts in through configuration.
- Never use `--network=host`, never publish ports, and never run anything that
  scans, pings, multicasts to, or connects to LAN addresses (10/8,
  172.16/12, 192.168/16, 169.254/16, link-local, mDNS, SSDP, DDS multicast
  239.255.0.1, RR discovery ports). No `ssh`, `nmap`, `ping`, or `curl`/`wget` to
  private addresses.
- Never run ROS 2 nodes, the `ros2` CLI, or Robot Raconteur nodes/clients on the
  host. Interactive tests against real external RR clients are done **only by
  the user**, outside Claude.

### S3. Git hygiene
- Work on a branch named after the milestone (`b4-topics`, etc.), never
  directly on `main`.
- Commit locally only when the user asks or approves a completed milestone.
- **Never** `git push`, add remotes, rewrite history, or `git reset --hard`/
  `git clean -fdx` without explicit user approval. The one approved exception
  (2026-11-13) is the cross-platform CI snapshot push to the public scratch repo
  `johnwason/github-actions-scratch`, on `ci/` branches only. It follows the rules in
  S6 of `../rmw_intraprocess/CLAUDE.md`. This repo gets its own `push-ci.sh` when
  it has code.
- Never commit build outputs, credentials, TLS keys/certificates, or vendored
  third-party sources.

### S4. Scope and honesty
- Do not modify anything outside this repo, except `../rmw_intraprocess` when a
  task explicitly spans both.
- Do not install or change anything on the host.
- Report test results faithfully. Never disable, skip, or weaken a test to make
  a milestone pass. Ask the user before excluding anything.
- Do not mark a milestone done until its acceptance criteria in PLAN.md pass
  inside the container, and you've shown the user the command and output.

### S5. Exposure is a security control
- Exposure policy is **deny-by-default**. Nothing is reachable over RR unless
  the configuration allows it. Tests must cover denial paths.
- Features that change the running system (publish, call services, set
  parameters, load/unload components) must be individually enable-able and
  default **off**.
- Don't weaken authentication or TLS settings for convenience. Use dedicated
  test-only credentials generated inside the container at test time.

---

## Engineering rules

- **Target: ROS 2 Jazzy only** (Ubuntu 24.04 container). Robot Raconteur C++
  core and Python bindings built from **GitHub `master`**
  (`github.com/robotraconteur/robotraconteur`), with the resolved commit SHA
  recorded in the image label. If `master` breaks, report the SHA to the user.
  Don't switch to another revision on your own.
- **Conventions:** follow the existing
  `robotraconteur-contrib/robotraconteur_ros2_bridge` naming and object model
  (`rosmsg_`/`rostopic_`/`rosservice_` definitions, `fixname`, subscriber
  pipe and wire, `int32` handle + `varobject{int32}` objrefs). See PLAN.md §2.3.
  Extensions must be supersets that don't break clients of the existing bridge.
- **Wire vs pipe:** always offer both. The client chooses. The bridge never
  picks one per topic.
- **The bridge must not depend on any specific RMW.** It must work with
  `rmw_intraprocess_cpp` and `rmw_fastrtps_cpp`, and tests run against both.
  The `rmw_intraprocess_cpp` tap API (M9 in the sibling plan) is an optional
  fast path that is detected at build time.
- **Windows portability is a hard requirement:** C++17, no POSIX headers, no
  GCC-only extensions, visibility macros on exports, and no Boost types in
  the public headers. Keep RR/Boost behind the component's DLL boundary
  (to avoid Boost/CRT clashes with the ROS Windows environment).
- **Threading:** never block an RR thread pool thread waiting on ROS (use async
  ROS clients and complete RR async handlers from ROS callbacks). Never call RR
  synchronously from inside a ROS callback while holding bridge locks.
  Shutdown order: stop RR → destroy proxies → let the ROS node be destroyed.
- Every behaviour change ships with a test. Run the ASan/UBSan and TSan
  variants before declaring a milestone complete.
