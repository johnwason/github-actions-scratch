# Examples

Lua ports of Robot Raconteur client examples from RR core's `examples/` directory. Each script
runs with a Lua interpreter that can `require("robotraconteur")` (set `LUA_CPATH` to the built
module), for example `lua reynard_the_robot/reynard_client.lua`.

| Example | Port of | Service |
|---|---|---|
| `reynard_the_robot/reynard_client.lua` | `reynard_the_robot/python/client/reynard_robotraconteur_client.py` | Reynard the Robot |
| `standard_devices/robot/robot_client_jog_freespace.lua` | `standard_devices/robot/python/robot_client_jog_freespace.py` | a standard robot driver |
| `standard_devices/robot/robot_client_position_command.lua` | `standard_devices/robot/python/robot_client_position_command.py` | a standard robot driver |
| `standard_devices/robot/robot_client_trajectory.lua` | `standard_devices/robot/python/robot_client_trajectory.py` | a standard robot driver |
| `standard_devices/camera/camera_client_capture_frame.lua` | `standard_devices/camera/python/camera_client_capture_frame.py` | a standard camera |
| `standard_devices/camera/camera_client_capture_frame_compressed.lua` | `.../camera_client_capture_frame_compressed.py` | a standard camera |
| `standard_devices/camera/camera_client_image.lua` | `.../camera_client_image.py` | a standard camera |
| `standard_devices/camera/camera_client_image_preview.lua` | `.../camera_client_image_preview.py` | a standard camera |

Differences from the Python originals:

- Each script takes the service URL as its first argument, with the same default URL as the
  Python version. `robot_client_trajectory.lua` takes `abb` or `ur` first, as the original does.
- The endless loops (`jog_freespace`, `position_command`) take an optional run time in
  seconds as a second argument; without it they run until interrupted.
- `c:getConstants(service)` and `c:newStructure(type)` stand in for `RRN.GetConstants` and
  `RRN.GetStructureType(...)()`. Enum values can also be given by name
  (`c:setProperty("command_mode", "halt")`).
- Lua has no OpenCV, so the camera examples save images instead of displaying them: raw frames
  as PPM or PGM, compressed frames as the PNG or JPEG bytes they arrive in (`image_util.lua`).
  The streaming examples run for a few seconds (second argument) and save the newest frame.
- Waiting uses `rr.sleep`, which keeps delivering events, listener notifications and callbacks
  while the script waits.

`tests/examples/test_examples.lua` runs every example against stand-in services: a Reynard mock
(`tests/examples/reynard_mock.py`), and the loopback robot and camera services from the pinned
Racket client checkout.
