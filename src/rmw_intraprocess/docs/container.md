# container_host: a component container configured by a YAML file

`container_host` (package `rmw_intraprocess_container`) loads ROS 2 components
into one process from a simple YAML file and spins them. It replaces
`ros2 launch` plus `component_container` for single-process applications: with
`rmw_intraprocess_cpp`, a launch file in another process cannot reach the
container (its `load_node` service exists only inside the process), so the
process has to load its components itself.

It works with any RMW. It uses `rmw_intraprocess_cpp` unless told otherwise.

## Running it

```
container_host [options] CONFIG.yaml [--ros-args ...]

  -c, --config FILE         the configuration file (or give it as the argument)
  --rmw NAME                the RMW implementation; overrides the file and
                            RMW_IMPLEMENTATION (default: rmw_intraprocess_cpp)
  --check                   validate the file and find every component's library
                            without loading anything, then exit
  --list-components PKG     list the components package PKG registers, then exit
  --shutdown-after SECONDS  stop after this many seconds (smoke tests)
  -h, --help                show this help
```

With a sourced workspace: `ros2 run rmw_intraprocess_container container_host
my_app.yaml`, or run `lib/rmw_intraprocess_container/container_host` from the
install prefix directly (on Windows, after `install\setup.bat`).

It runs until Ctrl+C, until a component calls `rclcpp::shutdown()`, or until
`--shutdown-after` expires, then unloads the components (newest first) and
exits.

| Exit status | Meaning |
|---|---|
| 0 | ran and shut down normally |
| 1 | a component failed to load (or `--check` found a problem) |
| 2 | bad command line or configuration file |

**RMW selection**, highest priority first: `--rmw`, `rmw_implementation` in the
file, the `RMW_IMPLEMENTATION` environment variable, then
`rmw_intraprocess_cpp`. The host sets `RMW_IMPLEMENTATION` for itself before
initializing rclcpp, so the choice applies to the whole process. The first log
line names the RMW in use and where the choice came from.

## The configuration file

Only `components` is required. Unknown keys are errors (so a typo is reported
with its line, not ignored). A complete example with comments is installed as
`share/rmw_intraprocess_container/config/demo.yaml`.

```yaml
rmw_implementation: rmw_intraprocess_cpp   # optional
ros_arguments: [--log-level, info]         # optional, process-wide --ros-args
executor:                                  # optional, default single_threaded
  type: multi_threaded                     # single_threaded | multi_threaded | events
  threads: 4                               # multi_threaded only; 0 = number of cores
  isolated: false                          # true: one executor and thread per component
container:                                 # optional: the container node itself
  name: component_container
  namespace: /
parameter_files: [robot_params.yaml]       # optional, loaded by every component
use_intra_process_comms: false             # optional default for every component
continue_on_error: false                   # optional; true keeps loading after a failure

components:
  - package: composition                   # the package that registers the component
    plugin: composition::Talker            # the registered class name
    name: talker                           # optional: node name
    namespace: /demo                       # optional: node namespace
    remappings: {chatter: /demo/chat}      # optional: {from: to} or ["from:=to", ...]
    parameters:                            # optional, inline
      publish_rate: 10.0
      frame:
        id: base_link                      # nested keys become frame.id
    parameter_files: [talker.yaml]         # optional, this component only
    ros_arguments: [--log-level, "talker:=debug"]   # optional, this node only
    use_intra_process_comms: true          # optional, overrides the default
    enabled: true                          # optional; false skips the entry
```

`executor` also accepts the type alone: `executor: multi_threaded`.

### Components

`package` and `plugin` are what `ros2 component types` lists, and what
`container_host --list-components PACKAGE` prints. The library is found
through the ament index (`AMENT_PREFIX_PATH`), as `component_container` finds
it. Components load in file order. By default the first failure stops the
host with exit status 1. With `continue_on_error: true` the failure is logged
and the remaining components still load (the exit status is 1 either way).

### Parameters

Three sources, later ones win for the same parameter:

1. the top-level `parameter_files`, in order;
2. the component's `parameter_files`, in order;
3. the component's inline `parameters`.

Parameter files use the standard ROS format (`node_name: ros__parameters:` or
`/**: ros__parameters:`), so an existing file, such as a Nav2 parameter file,
can be listed at the top level and each node picks its own section.

Inline values follow the parameter-file typing rules exactly (rcl's own parser
types them). `5` is an integer, `5.0` a double, `true` a bool, and `"5"` (quoted)
a string. Lists must have one element type, and an empty list has no type and is
rejected. Nested mappings flatten to dotted names.

### Paths and substitutions

Relative paths in `parameter_files` resolve against the directory of the
configuration file. Every value may use:

- `$(find-pkg-share PKG)`: the package's share directory;
- `$(env NAME)`: an environment variable (an error if it is unset or empty);
- `$(env NAME DEFAULT)`: the variable, or `DEFAULT`.

### Executors

- `single_threaded` (default), `multi_threaded` (with `threads`), `events`
  (rclcpp's experimental EventsExecutor): one executor spins every component.
- `isolated: true`: each component gets its own executor of `type`
  (`single_threaded` or `multi_threaded`) on its own thread, like
  `component_container_isolated`. Use it when one component's slow callbacks
  must not delay another's.

### Global arguments

`ros_arguments` (and anything after `--ros-args` on the command line) go to
`rclcpp::init`. They are process-wide: logging options and the like. As in
`component_container`, components do not apply global remappings or
parameters. Use each component's `remappings`, `parameters` and
`parameter_files` for that.

## Loading components while running

The container node (`/component_container` by default) serves the standard
`~/_container/load_node`, `unload_node` and `list_nodes` services. With
`rmw_intraprocess_cpp` they can only be called from inside the process (for
example by the Robot Raconteur bridge's component loading, B9). Components
loaded that way are unloaded at shutdown like the configured ones.

## Notes and limitations

- **One thread creates and destroys components.** rcl's per-node state is not
  thread-safe (docs/unsupported.md), so the host loads and unloads from its
  main thread, or from the container's service callbacks.
- **Component libraries stay loaded until the process exits.** Unloading them
  at exit gains nothing. Doing so unmaps type support libraries whose
  rosidl caches are still referenced, and class_loader would close them during
  static destruction otherwise. The host keeps one extra handle per library.
- **Intra-process comms.** rclcpp's intra-process manager can self-deadlock
  when publishers are destroyed concurrently (docs/unsupported.md). The default
  is off; turn it on per component where the zero-copy path matters.
- **No launch features** beyond the above: no conditions, events, includes, or
  per-component processes. Write one file per application.

## Tests

`rmw_intraprocess_container/test/`:

- `test_config`: the parser (every key, typing, errors with line numbers,
  substitutions, path resolution).
- `test_host`: the configured components, running under each executor model
  (single, multi, events, isolated single and multi), checked from a probe node.
  It covers remapped topics, parameter precedence, `load_node` and `list_nodes`
  over the in-process services, unload while spinning, and load errors.
- `test_cli`: the command line and exit codes, a full run with
  `--shutdown-after`, and a run that ends when a component calls
  `rclcpp::shutdown()`.
- `test_container_host_smoke` and `test_container_host_fibonacci`: the
  installed executable, end to end, with the default RMW.
