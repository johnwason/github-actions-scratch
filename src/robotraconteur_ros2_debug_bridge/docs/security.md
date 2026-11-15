# Security

The bridge is a way into a running ROS application. Its defaults make it
reachable only from the same process and the same user on the same machine,
and only to read what the configuration lists.

## Defaults (config/default.yaml)

- **Transports.** IntraTransport (same process) and LocalTransport (same
  machine, same user, through a per-user socket directory) are on. TCP is
  off.
- **Discovery.** Node announce and discovery listening are off; they need
  TCP.
- **Capabilities.** Only `read` is on. `publish`, `call_service`,
  `serve_service`, `action`, `set_parameter` and `load_component` are each
  off until enabled.
- **Exposure.** Every `exposure` list is empty, so nothing is visible.
  Deny rules win over everything. A denied topic or service is
  indistinguishable from one that does not exist (docs/topics.md).

## Network access (opt in)

```yaml
robotraconteur:
  transports:
    tcp:
      enabled: true
      port: 34573
      localhost_only: false      # true by default: loopback only
      tls:
        enabled: true            # needs this node's Robot Raconteur certificate
        require: true            # reject clients without TLS
  authentication:
    enabled: true
    password_file: users.txt     # relative to the configuration file
```

### Authentication

- With authentication on, a client must give a valid user name and
  password. A client that connects without credentials, or with wrong
  ones, is rejected; `test_authentication` checks all three cases.
- The password file is Robot Raconteur's format: one user per line,
  `name md5(password) privileges`, for example
  `operator 5f4dcc3b5aa765d61d8327deb882cf99 objectlock`.
- **MD5 is a weak hash.** Keep the file readable only by the account that
  runs the bridge, and use long random passwords. Over a network, use TLS
  as well, since the password is sent at connect time.
- Authentication applies to every transport, including LocalTransport and
  IntraTransport.

### TLS

- TLS uses Robot Raconteur's TLS support: the node loads its certificate
  with `TcpTransport::LoadTlsNodeCertificate`, from Robot Raconteur's
  certificate store for the node's NodeID. Robot Raconteur's CA issues
  those certificates.
- The bridge only switches TLS on; `require` rejects plain connections.
- **Not tested here.** The test environment cannot obtain a node
  certificate, so TLS is checked only as configuration. The user tests it
  on a real network (PLAN.md, cross-testing).

## What clients can send

- **Values.** Values written into ROS (publish, service requests, served
  responses, action goals, parameters, component requests) go through the
  value converter. It checks every field: types, scalar versus array, fixed
  lengths, bounds on sequences and strings, structure types, list
  numbering, and UTF-8 for wstrings. Anything else is rejected with an
  error that names the field (docs/type_mapping.md, Values).
- **Fuzzing.** `test_converter_fuzz` mutates valid values of every
  message-shaped type of the B2 packages for 20 s: element types, lengths,
  names, numbering, structure types, nesting, nulls, and missing and extra
  elements. It writes them into both introspection flavors. It runs in
  every variant, including ASan/UBSan and TSan. It has found no crashes:
  about 1.3 million cases per release run, about 100,000 under ASan.
- **Names.** Topic, service, action, node and container names are resolved
  like ROS names. They are checked against the exposure rules before
  anything that looks at the ROS graph.

## Isolation in the process

- **Robot Raconteur's symbols.** The component hides the Robot Raconteur
  core it links (`-Wl,--exclude-libs,ALL`), so another Robot Raconteur copy
  in the process cannot share its globals. `test_symbols` checks this.
- **Proxy node and thread.** The bridge's ROS entities (subscriptions,
  publishers, service and action clients, served services) live on a node
  of their own, `<bridge>_proxies`, run by a thread of the bridge
  (docs/topics.md).
- **Exceptions.** No exception leaves a bridge callback in the host's
  executor.
