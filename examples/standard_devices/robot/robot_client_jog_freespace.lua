-- Command a standard robot (com.robotraconteur.robotics.robot) with the jog_freespace function.
-- A Lua port of RR core's examples/standard_devices/robot/python/robot_client_jog_freespace.py
--
--     lua robot_client_jog_freespace.lua [url] [seconds]
-- Runs until interrupted, or for the given number of seconds.
local rr = require("robotraconteur")

-- URL for connecting to the robot. By default connects to the UR5e simulated robot.
local url = arg[1] or "rr+tcp://localhost:52511?service=robot"
local duration = tonumber(arg[2])

local function hex(v)
  if type(v) == "number" then return string.format("0x%x", v) end
  return tostring(v) -- a 64-bit value LuaJIT cannot hold exactly (an rr.array box)
end

-- Connect to the robot driver
local c = rr.connectService(url)

-- Retrieve the current robot state and print the current command mode
print(c:peekWireInValue("robot_state").command_mode)

-- Change the robot command mode, first to halt, then to jog. Enum values can be given by name.
c:setProperty("command_mode", "halt")
rr.sleep(0.1)
c:setProperty("command_mode", "jog")

-- Get the starting joint positions
local start_joint_pos = c:peekWireInValue("robot_state").joint_position
local n = start_joint_pos:numel()

local t_start = rr.time()
-- Move the robot in a loop
while not duration or rr.time() - t_start < duration do
  local t = rr.time() - t_start
  -- Generate a sin wave on the first joint
  local pos = start_joint_pos:copy()
  pos[1] = pos[1] + 0.2 * math.sin(t / 5)
  local max_vel = rr.array("double", n)
  for i = 1, n do max_vel[i] = 1 end
  c:callFunction("jog_freespace", pos, max_vel, true)
  -- Print the current robot_state_flags
  print(hex(c:peekWireInValue("robot_state").robot_state_flags))
  -- Wait for the next loop iteration
  rr.sleep(0.1)
end
c:close()
