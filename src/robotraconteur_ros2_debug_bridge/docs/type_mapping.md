# ROS → Robot Raconteur type mapping

This bridge follows the conventions of the existing Python bridge
[robotraconteur-contrib/robotraconteur_ros2_bridge](https://github.com/robotraconteur-contrib/robotraconteur_ros2_bridge)
so that its clients work here with only a URL change (PLAN.md §2.3). This
page records what the upstream bridge does,
as checked against its source, and where this bridge extends it.

## Upstream behavior (surveyed 2026-11-14, upstream master `d587d2cd`)

Source: `robotraconteur_ros2_bridge.py` (`ROSTypeAdapterManager`) and
`experimental.ros2_bridge.robdef`, from the copy in the development image
(`/opt/robotraconteur_ros2_bridge`).

**Root object** (`experimental.ros2_bridge`, no `stdver` line):

```
object ROS2Bridge
    function int32 subscribe(string topic, string msgtype)
    objref varobject{int32} subscribers
    function int32 publish(string topic, string msgtype)
    objref varobject{int32} publishers
    function int32 client(string service, string srvtype)
    objref varobject{int32} clients
    function int32 register_service(string service, string srvtype)
    objref varobject{int32} services
end
```

**Messages**: service definition `rosmsg_<pkg>__<Msg>` with one
`struct <Msg>`. Built from the Python message class's slots
(`get_fields_and_field_types()`), in slot order. Nested message fields import
`rosmsg_<pkg>__<Msg>` and use the type string `rosmsg_<pkg>__<Msg>.<Msg>`.
No `stdver` line, and no constants.

**Topics**: service definition `rostopic_<pkg>__<Msg>` that imports the
message definition:

```
object subscriber
    pipe rosmsg_<pkg>__<Msg>.<Msg> subscriberpipe [readonly]
    wire rosmsg_<pkg>__<Msg>.<Msg> subscriberwire [readonly]
    function void unsubscribe()
end
object publisher
    function void publish(rosmsg_<pkg>__<Msg>.<Msg> m)
end
```

**Services**: one service definition `rosservice_<pkg>__<Srv>` holding the
structs `<Srv>Request` and `<Srv>Response` and two objects:

```
object rosclient
    function <Srv>Response call(<Srv>Request request)
    function bool wait_for_service(double timeout)
end
object rosservice
    callback <Srv>Response servicefunction(<Srv>Request request)
end
```

Note `servicefunction` is a **callback** (the RR client implements the ROS
service), not a function.

**Field types** (the slot type strings rosidl gives Python):

| rosidl slot type | Upstream RR type |
|---|---|
| `int8` … `uint64`, `double` | same name |
| `float` | `single` |
| `boolean` | `bool` |
| `T[N]` (primitive) | `T[N]` (fixed array; length checked both ways) |
| `T[]`, `sequence<T>` (primitive) | `T[]` |
| `string` | `string` |
| `string[]`, `sequence<string>`, `string[N]` | `string{list}` |
| `pkg/Msg` | `rosmsg_pkg__Msg.Msg` |
| `pkg/Msg[]`, `sequence<pkg/Msg>`, `pkg/Msg[N]` | `rosmsg_pkg__Msg.Msg{list}` |

Not handled upstream (the type cannot be bridged): `char`, `octet`
(`byte`), `wstring`, bounded strings `string<N>`, and bounded sequences
`sequence<T, N>` (the length text after `[` is parsed as an integer, which
fails for `<=N`).

**Name fixing** (`fixname`): an RR reserved word (`object`, `end`, `option`,
`service`, `struct`, `import`, `implements`, `field`, `property`, `function`,
`event`, `objref`, `pipe`, `callback`, `wire`, `memory`, `void`, the numeric
type names, `single`, `double`, `varvalue`, `varobject`, `exception`,
`using`, `as`) gets the prefix `ros`; so does any name that starts with `rr`
or `robotraconteur` (case-insensitive). The original name still addresses the
ROS field. Note that `string` and `bool` are not in the list.

## This bridge (B2)

`src/type_mapper.cpp` generates the definitions from the C++ rosidl
introspection type support (fields) and the installed interface file
(`share/<pkg>/{msg,srv,action}/<Name>.*`, constants, parsed like
`rosidl_adapter`). `generate_robdef` (installed in
`lib/robotraconteur_ros2_debug_bridge`) writes them out:

```sh
generate_robdef [--out DIR] [--package PKG]... [pkg/msg/Name | pkg/srv/Name | pkg/action/Name]...
```

It is a superset: every definition upstream can produce comes out
member-compatible (same definition, struct, object and member names, same
member text, same imports). `test_upstream_compat` checks this against the
upstream generator for every msg and srv of the B2 packages (272 upstream
definitions, 940 members, at upstream `d587d2cd`). The extensions only add:

- **Types upstream rejects.** `char`, `octet`/`byte` → `uint8`;
  `wchar` → `uint16`; `wstring` and bounded strings → `string`; bounded
  sequences → `T[]` or `{list}`. Bounds and encodability are checked when
  writing into ROS (B3).
- **Constants** become struct `constant` members, in the struct of their
  message, request, response, goal, result or feedback. Robot Raconteur has
  no bool constants, and master mis-verifies 8-bit ones
  (docs/upstream_issues.md). So `bool` constants become `uint16` 0/1, and
  `byte`, `char`, `uint8` and `int8` constants become `uint16`/`int16`.
  Integer values are normalized to decimal. A float constant without a
  finite value (`inf`, `nan`) is left out, since Robot Raconteur cannot
  express it.
- **More names fixed.** `fixname` covers the union of upstream's list and
  everything Robot Raconteur's `VerifyName` rejects: the extra words
  `string`, `bool`, `constant`, `enum`, `pod`, `namedarray`, `cdouble`,
  `csingle`, `stdver`, `this`, `self` and `Me`, and the prefixes `get_`,
  `set_` and `async_` (case-insensitive), besides `rr` and
  `robotraconteur`. Upstream cannot register a type with such a name, so
  renaming it breaks no existing client. It also applies to struct names.
- **Empty messages** (`std_msgs/Empty`) get an empty struct. rosidl's
  placeholder member `structure_needs_at_least_one_member` is not a field.
- **Writable topic members.** `publisher` gains `pipe publisherpipe [writeonly]`
  and `wire publisherwire [writeonly]` (served in B4).
- **Actions** get `rosaction_<pkg>__<Action>` (served in B8):

  ```
  struct <A>Goal / <A>Result / <A>Feedback   (the action's three parts)
  struct <A>GoalStatus
      constant int16 STATUS_UNKNOWN 0 ... STATUS_ABORTED 6   (action_msgs/GoalStatus)
      field uint8[16] goal_id
      field int8 status
      field <A>Feedback feedback    (null when not present)
      field <A>Result result        (null until done)
  end
  object rosactionclient
      function <A>GoalStatus{generator} send_goal(<A>Goal goal)
      function bool wait_for_server(double timeout)
  end
  ```
- **New root members** (graph, parameters, actions, component loading,
  explicit QoS), only ever added.
- **`stdver 0.10`** in every definition. RR clients that don't use it ignore it.

As upstream does, a nested message is imported as its own `rosmsg_`
definition. A message type yields `rosmsg_` and `rostopic_`; a service
yields `rosservice_`, holding `<Srv>Request` and `<Srv>Response`.

Tests (`test_type_mapper`):
- golden files `test/golden/*.robdef` for every interface of
  `action_tutorials_interfaces`, `example_interfaces`, `geometry_msgs`,
  `nav_msgs`, `sensor_msgs`, `std_msgs`, `test_msgs` and `turtlesim`, plus
  their imports (307 files; regenerate with `scripts/update-golden.sh` and
  review the diff);
- each one parsed and verified with Robot Raconteur;
- every interface installed in the image (281) mapped and verified;
- naming, constants and type edge cases, errors, and concurrent use.

## Values (B3)

`src/value_converter.cpp` converts between ROS message memory and Robot
Raconteur structure values. It works on the C++ and the C introspection
type supports. The Robot Raconteur side is the packed wire form, exactly as
code from RobotRaconteurGen packs these definitions:
- numbers are arrays, with one element for a scalar; `bool` is `rr_bool`;
- strings are UTF-8 char arrays. `wstring` is converted to and from UTF-16:
  a lone surrogate read from ROS becomes U+FFFD, and invalid UTF-8 written
  into ROS is rejected;
- string and message sequences are lists with numbered elements;
- nested messages are `rosmsg_` structures.

Writing into ROS:
- These are rejected with a `ConversionError` that names the field: the
  wrong element type, a scalar without exactly one value, a fixed array of
  the wrong length, a bounded sequence or bounded string over its bound,
  the wrong structure type, and a malformed list.
- Missing fields and null nested structures keep the message's values,
  which for a fresh message are the defaults.

`test_value_converter` covers this:
- randomized round trips for every message, service request/response and
  action goal/result/feedback of the B2 packages (189 types). Each goes
  C++ → RR → wire → C → RR → C++ and is compared field by field;
- the same values unpacked and repacked by RobotRaconteurGen's generated
  code for test_msgs, JointState and Odometry, with identical results;
- each rejection path, for both flavors.

Note for serving: Robot Raconteur's `Message::Write` rewrites list element
numbers into names in place. So a packed value must not be shared between
concurrent sends.
