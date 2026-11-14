# rmw_intraprocess — instructions for Claude

This repo is a ROS 2 middleware (RMW) implementation that only communicates
**within a single process**. It has no networking, no discovery protocol and no
shared memory between processes. External communication is handled separately by
the Robot Raconteur gateway in the sibling repo `../robotraconteur_ros2_debug_bridge`.

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
- Use the scripts in `scripts/` (created in milestone M0) as the only entry
  points: `scripts/build-image.sh`, `scripts/build.sh`, `scripts/test.sh`.
  Prefer adding a script over running a long one-off `podman run` command.
- Use rootless podman only. Never use `sudo`, `--privileged`, `--cap-add`,
  `--pid=host`, `--ipc=host`, `--userns=host`, or `--security-opt label=disable`.
- Bind mounts are limited to this repo and its sibling repo. Mount source
  read-only (`:ro,Z`) and write build output to named podman volumes
  (`rmw_intraprocess-build`, etc.). Never mount `$HOME`, `/`, `/var/run`,
  the podman socket, SSH keys, or any credential directory.
- Do not push images (`podman push`), log in to registries (`podman login`), or
  remove images, containers or volumes not created by this project's scripts.

### S2. Never contact the local network
- **Containers that build or test must run with `--network=none`.** Loopback
  still works inside them, which is all this project needs. An intra-process
  RMW has no reason to open a socket, so every test must pass with no network.
- Only `podman build` (creating the image) may use the network, and only to
  fetch from public package sources listed in the Containerfile: the
  container registry for the base image, Ubuntu/ROS apt mirrors, and pinned
  GitHub release/tag URLs. Each new source added to a Containerfile must be noted
  in the commit message.
- Never use `--network=host`, never publish ports (`-p`/`--publish`), and never
  run anything that scans, pings, multicasts to, or connects to LAN addresses
  (10/8, 172.16/12, 192.168/16, 169.254/16, link-local, mDNS, SSDP, DDS
  multicast 239.255.0.1). No `ssh`, `nmap`, `ping`, `curl`/`wget` to private
  addresses, and no host networking tools.
- Never run ROS 2 nodes, `ros2` CLI commands, or Robot Raconteur nodes on the
  host. They auto-discover over the LAN.
- Code in this repo must not open sockets, call any networking API, or link a
  networking library. CI enforces this (M0 portability/lint check).

### S3. Git hygiene
- Work on a branch named after the milestone (`m2-pubsub`, etc.), never
  directly on `main`.
- Commit locally only when the user asks or approves a completed milestone.
- **Never** `git push` (only exception: S6), add remotes, rewrite published
  history, or `git reset --hard`/`git clean -fdx` without explicit user approval.
- Never commit build outputs, credentials, or vendored third-party sources.

### S4. Scope and honesty
- Do not modify anything outside this repo and `../robotraconteur_ros2_debug_bridge`
  (and only touch the sibling when a task explicitly spans both).
- Do not install or change anything on the host (packages, shell profile,
  system config, podman config).
- Report test results faithfully. Never disable, skip, or weaken a test to make
  a milestone pass. If an upstream test can't pass by design (for example, a
  feature that is intentionally unsupported), record it in
  `docs/unsupported.md` with the reason, and ask the user before excluding it.
- Do not mark a milestone done until its acceptance criteria in PLAN.md pass
  inside the container, and you've shown the user the command and output.

### S6. Cross-platform CI on GitHub (user-approved exception, 2026-11-13)
The user approved one narrow exception to S2/S3 for cross-platform testing:
- The **only** push target is the user's public scratch repo
  `https://github.com/johnwason/github-actions-scratch`, and only through
  `scripts/push-ci.sh`. It pushes a single orphan snapshot commit of the
  committed HEAD to a branch under `ci/` (default `ci/rmw_intraprocess`). Never
  push to any other branch of that repo, to any other repo, or this repo's own
  history, and never add a remote.
- The token is in `../github_ros2_rmw_token.txt` (outside both repos). Only
  `scripts/push-ci.sh` and `scripts/ci-gh.sh` read it, at run time. Never print,
  copy, commit, or mount it into a container, and never put it in argv or the
  git config.
- `scripts/ci-gh.sh` is for reading run status, logs and artifacts, and for
  re-running or cancelling our own `ci/` runs. Nothing else in that repo.
- The snapshot is **public**. It may only contain what is already committed
  here, and never credentials.
- The jobs run on GitHub-hosted runners (not the local network). Local builds and
  tests still follow S1 and S2 unchanged.

---

## Engineering rules

- **Target: ROS 2 Jazzy only** (Ubuntu 24.04 container). Don't add
  compatibility shims for other distros.
- **Single domain:** all contexts share one domain. Accept `domain_id` and
  ignore it for matching (don't fail on it).
- **Windows portability is a hard requirement**, even though Windows can't be
  built in podman here:
  - C++17, standard library threading only (`std::thread`, `std::mutex`,
    `std::condition_variable`, `std::atomic`).
  - No POSIX headers (`unistd.h`, `pthread.h`, `sys/*`), no `__attribute__`
    without a macro, no GCC-only extensions. Use `rcutils`/`rcpputils` helpers
    where they exist.
  - Every exported symbol goes through the package's `visibility_control.h`
    macros (MSVC `__declspec`).
  - No static-initialization-order dependencies across DLLs. The global
    registry is a function-local static or is owned by the RMW context.
  - The portability check script (M0) must pass on every change.
- **No networking** (see S2).
- Prefer correctness and clear locking over cleverness. Document the locking
  order in `docs/design.md` and keep it current.
- Every behaviour change ships with a test. Run the ASan/UBSan and TSan
  build variants (M0) before declaring a milestone complete.
