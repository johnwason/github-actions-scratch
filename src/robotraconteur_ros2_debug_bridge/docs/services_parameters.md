# Services (B6) and parameters (B7)

Both are deny by default. See [topics.md](topics.md) for the rule syntax.

```yaml
capabilities:
  call_service: true       # rosclient.call
  serve_service: true      # register_service
  set_parameter: true      # Parameters.set
exposure:
  services:
    allow: ['/robot/**']           # visible in the graph
    call: ['/robot/reset']         # callable
    serve: ['/planner/plan']       # served by a Robot Raconteur client
    deny: ['/robot/secret*']
  parameters:                      # by fully qualified node name
    allow: ['/robot/**']           # list, describe, get, events
    set: ['/robot/tuning']
```

## Services

As in the upstream bridge:

```python
cl = bridge.get_clients(bridge.client('/add_two_ints', 'example_interfaces/AddTwoInts'))
cl.wait_for_service(5.0)            # True or False after the timeout
res = cl.call(req)                  # rosservice_example_interfaces__AddTwoInts.AddTwoIntsRequest

srv = bridge.get_services(bridge.register_service('/plan', 'my_pkg/Plan'))
srv.servicefunction.Function = lambda req: response   # answers ROS clients
```

- `client()` takes the service type from the graph when `srvtype` is empty.
- A call fails at once if the service has no server, so use
  `wait_for_service` first. After 60 s without a response it fails with a
  timeout; the client's own request timeout is usually shorter.
- Calls and waits complete asynchronously on the proxy thread, so no
  Robot Raconteur thread waits for ROS. `test_services` checks this with
  40 concurrent waits plus slow calls.
- **ROS services queue at most 10 requests.** That is the `ServicesQoS`
  depth, and further requests are dropped by ROS: keep fewer calls in
  flight per service.
- A served request goes to the client that called `register_service`.
  If that callback fails, ROS gets no response and its client times out,
  as with a failing ROS server.
- Robot Raconteur master's asynchronous callback request is broken
  (docs/upstream_issues.md), so each served service has a worker thread
  that makes the blocking call.

## Parameters

`bridge.get_parameters()` returns an
`experimental.ros2_debug_bridge_parameters.Parameters` object. It uses the
rcl_interfaces message types, mapped like any other message type:

| Member | Returns |
|---|---|
| `nodes()` | nodes whose parameters are readable |
| `list(node, prefixes, depth)` | `rosmsg_rcl_interfaces__ListParametersResult.ListParametersResult` |
| `describe(node, names)` | `ParameterDescriptor{list}` |
| `get(node, names)` | `ParameterValue{list}`; `type` is an rcl_interfaces `ParameterType` value |
| `set(node, parameters)` | `SetParametersResult{list}`, one per parameter; `Parameter{list}` in |
| `events` (pipe) | `ParameterEvent` of readable nodes, from `/parameter_events` |

- The verbs are those of `ros2 param`. Robot Raconteur rejects member
  names that start with `get_` or `set_`.
- Node names may be relative: `test_params` means `/test_params`.
- Calls go to the node's parameter services, asynchronously on the proxy
  thread, and fail if the node has none or does not answer within 10 s.

## Actions (B8)

```yaml
capabilities: {action: true}
exposure:
  actions:
    allow: ['/nav/**']                 # visible in the graph
    send: ['/nav/navigate_to_pose']    # goals
```

```python
ac = bridge.get_action_clients(bridge.action_client('/fibonacci', 'action_tutorials_interfaces/Fibonacci'))
ac.wait_for_server(5.0)
goal_type = node.GetStructureType('rosaction_action_tutorials_interfaces__Fibonacci.FibonacciGoal', ac)
gen = ac.send_goal(goal)
s = gen.Next()     # FibonacciGoalStatus: goal_id, status, feedback, result
gen.Abort()        # cancels the goal; Close() does too
```

**What `Next()` gives, in order:**
- `ACCEPTED`;
- `EXECUTING`, with feedback for each feedback message, and on status
  changes (`EXECUTING`, `CANCELING`);
- the final status (`SUCCEEDED`, `ABORTED` or `CANCELED`) with the result.

After that, `Next()` ends the generator (StopIteration).

**Other behavior:**
- A rejected goal raises "goal rejected" at the first `Next()`.
- Updates wait in a queue until the client asks for them.
- `action_client()` takes the type from the action's feedback topic when
  `actiontype` is empty.

**How it is implemented.** rclcpp_action (Jazzy) has no client for types
known only at run time. So the bridge uses the action protocol directly, on
the proxy thread:
- `<action>/_action/send_goal` and `get_result`, through generic clients;
- `cancel_goal`;
- the `feedback` topic, and `status` (reliable, transient local).

## Callbacks in the host's executor

Bridge callbacks in the host's executor never let an exception escape:
the graph poll timer and subscription callbacks catch them, and so do tasks
on the proxy thread. Graph queries throw once the ROS context shuts down
(SIGINT). Before this, a poll that fell into that window ended the host
process with `std::terminate`. The test host's terminate handler, which
prints all threads with gdb, found it.

## Components (B9)

```yaml
capabilities: {load_component: true}
exposure:
  components:
    allow: ['/my_container']     # containers, by node name
```

```python
comps = bridge.get_components()
comps.containers()                                # ['/my_container']
req = node.GetStructureType('rosservice_composition_interfaces__LoadNode.LoadNodeRequest', comps)()
req.package_name, req.plugin_name, req.node_name = 'demo_nodes_cpp', 'demo_nodes_cpp::Talker', 'talker'
res = comps.load('/my_container', req)            # LoadNodeResponse: success, unique_id, ...
comps.list('/my_container')                       # ListNodesResponse
comps.unload('/my_container', res.unique_id)      # UnloadNodeResponse
```

- The bridge calls the container's own composition services
  (`<container>/_container/load_node`, `unload_node` and `list_nodes`).
  Components therefore load into whatever container runs them:
  rclcpp_components' `component_container`, or rmw_intraprocess's
  `container_host`.
- `containers()` lists the usable containers, found in the ROS graph by
  their `load_node` service.
- Results, including failures (`success` false with `error_message`), are
  the container's own.
