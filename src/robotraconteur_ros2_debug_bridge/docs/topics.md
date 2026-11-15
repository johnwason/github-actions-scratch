# Topics and exposure (B4)

## Exposure rules

Nothing is reachable unless a rule allows it (deny by default), and `deny`
always wins. Rules go in the configuration file (`config_file` parameter;
`config/default.yaml` has none):

```yaml
capabilities:
  publish: true            # needed for any publishing (off by default)
exposure:
  topics:
    allow: ['/turtle1/**', '/chatter']    # visible and subscribable (read capability)
    publish: ['/turtle1/cmd_vel']         # publishable (also needs the publish capability)
    deny: ['/turtle1/secret']
```

Patterns match fully qualified names:
- `*` matches within one name segment;
- `**` matches across segments;
- `?` matches one character other than `/`;
- a pattern starts with `/` or `**`.

Topic names given to the bridge are expanded with its node's namespace
first. Subscribing to a topic that is denied fails with the same message
("Topic '…' is not available") as one that exists nowhere. The check
happens before the bridge looks at the ROS graph.

## Using topics

The root object works as in the upstream bridge:

```python
h = bridge.subscribe('/turtle1/pose', 'turtlesim/Pose')   # or 'turtlesim/msg/Pose', or '' for the graph's type
sub = bridge.get_subscribers(h)        # rostopic_turtlesim__Pose.subscriber
ep = sub.subscriberpipe.Connect(-1)    # every message
w = sub.subscriberwire.Connect()       # the latest value
sub.unsubscribe()

h = bridge.publish('/turtle1/cmd_vel', 'geometry_msgs/Twist')
pub = bridge.get_publishers(h)         # rostopic_geometry_msgs__Twist.publisher
pub.publish(msg)                       # or pub.publisherpipe / pub.publisherwire
```

The client chooses pipe, wire or function. Every member is always offered,
and a subscriber feeds its pipe and its wire from the same ROS subscription.

**Subscriber QoS.** The bridge matches the topic's publishers where they all
agree: reliable or best effort, and transient local or volatile. Otherwise
it uses the defaults. The depth is 10. A late wire connection gets the last
value, so a transient-local topic's latched value reaches it.

**Publisher QoS.** The defaults with depth 10. `publish_qos` and
`subscribe_qos` come later.

**Ownership.** A handle belongs to the client that created it. When that
client disconnects, or the handle is unsubscribed, the ROS entity goes, and
clients are told to release the object.

## Implementation notes

- **Data path.** Generic (serialized) subscriptions and publishers work with
  any RMW. Messages are deserialized into C++ message memory
  (`rosidl_typesupport_cpp` handle) and converted by `value_converter`.
- **Server layer.** `src/generic_service.*` serves the generated definitions
  through Robot Raconteur core's public interfaces. That is a
  `ServiceFactory`, a `ServiceSkel`, pipe and wire servers, and fan-outs.
  The design follows the SWIG bindings' Wrapped layer.
- **Fan-outs instead of RR's broadcasters.** Robot Raconteur's
  serialization writes into the elements it sends: sizes, flags, and list
  numbers turned into names in the v2 format. So each endpoint gets its own
  structural copy, with the array data shared.
- **Late wire connections.** RR's client creates its wire connection only
  when the connect response arrives, and drops wire packets that come before
  it. The connect callback runs before that response is sent. So the bridge
  sends the current value again 50 ms and 1 s after a connection.
  docs/upstream_issues.md has the details.
- **The proxies have their own node and thread.** rcl (Jazzy) keeps a
  per-node type description cache, an rcutils hash map. Creating and
  destroying entities write it, and the node's `~/get_type_description`
  service reads it, all without locking. ThreadSanitizer found two races
  on it while the proxies were on the bridge node:
  - an executor thread destroying a subscription while a Robot Raconteur
    thread created one;
  - the host's executor reading the cache while the bridge's thread
    destroyed an entity.

  So the proxies' entities (subscriptions, publishers, service clients and
  servers) belong to a separate node, `<bridge name>_proxies`, in the
  bridge's namespace. Only one bridge thread (`src/proxy_executor.*`)
  creates, spins and destroys that node and its entities, so everything
  that touches its cache runs there. Message callbacks run on it too.
  `subscribe`/`publish` wait for it, but never for the host's executor.
  The node shows in the ROS graph; it is in the bridge's graph view only
  if the exposure rules list it.
- **Function calls.** They complete asynchronously
  (`EndAsyncCallFunction`), so no Robot Raconteur thread waits on ROS.

## Tests

`test_topics__<rmw>` and `test_upstream_examples__<rmw>` run under both
RMWs. They use `test/topic_test_host.cpp`, which runs the bridge with a
talker, an echo node and a stand-in turtlesim in one process.

`test_topics` covers:
- JointState and String through the wire and the pipe at once;
- the message type taken from the graph;
- publishing through `publish()`, the pipe and the wire, checked by echo;
- denied topics looking like nonexistent ones;
- reading-only versus publishing rules;
- a transient-local value reaching `PeekInValue` and a late wire
  connection;
- unsubscribe and owner-disconnect cleanup.

`test_upstream_examples` runs the upstream `turtlesim_subscriber.py` and
`turtlesim_publisher.py` with two substitutions:
- the URL;
- the star import of RR's client module, which sets up TCP and discovery,
  replaced by `test/upstream_example_preamble.py`. The preamble provides the
  same names with local transports only, through the safety helper
  (CLAUDE.md S2).
