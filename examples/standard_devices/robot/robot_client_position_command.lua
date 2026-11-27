-- Command a standard robot using the position_command wire.
-- A Lua port of RR core's examples/standard_devices/robot/python/robot_client_position_command.py
--
--     lua robot_client_position_command.lua [url] [seconds]
-- Runs until interrupted, or for the given number of seconds.
local rr = require("robotraconteur")

-- URL for connecting to the robot. By default connects to the UR5e simulated robot.
local url = arg[1] or "rr+tcp://localhost:52511?service=robot"
local duration = tonumber(arg[2])

-- Connect to the robot driver
local c = rr.connectService(url)

-- Retrieve the current robot state and print the current command mode
print(c:peekWireInValue("robot_state").command_mode)

-- Retrieve the constants for the com.robotraconteur.robotics.robot service definition, and the
-- "position_command" value of the RobotCommandMode enum
local robot_const = c:getConstants("com.robotraconteur.robotics.robot")
local position_mode = robot_const.RobotCommandMode.position_command

-- Change the robot command mode, first to halt, then to position_command
c:setProperty("command_mode", "halt")
rr.sleep(0.1)
c:setProperty("command_mode", "position_command")
rr.sleep(0.1)

-- Connect to the position_command and robot_state wires for real-time data streaming
local cmd_w = c:connectWire("position_command")
local state_w = c:connectWire("robot_state")

-- Set a lifespan of 0.5 seconds for the robot state. If new packets are not received within
-- 0.5 seconds, reading the in value raises an error.
state_w:setInValueLifespan(0.5)

-- Wait for the state_w wire to receive valid data
while not state_w:inValueValid() do rr.sleep(0.01) end

-- command_seqno must increment for each position command
local command_seqno = 1

-- Retrieve the current joint position
local init_joint_pos = state_w:getInValue().joint_position

local t0 = rr.time()
while not duration or rr.time() - t0 < duration do
  local t = rr.time() - t0
  -- Retrieve the current robot state
  local robot_state = state_w:getInValue()
  -- Make sure the robot is still in position mode
  if robot_state.command_mode ~= position_mode then error("Robot is not in position mode") end
  command_seqno = command_seqno + 1
  -- Generate a joint command, in this case a sin wave on the first joint
  local cmd = init_joint_pos:copy()
  cmd[1] = cmd[1] + 0.2 * math.sin(t)
  -- Fill a new RobotJointCommand structure and send it to the robot
  local joint_cmd = c:newStructure("com.robotraconteur.robotics.robot.RobotJointCommand")
  joint_cmd.seqno = command_seqno           -- strictly increasing
  joint_cmd.state_seqno = robot_state.seqno -- the current robot_state.seqno, as a failsafe
  joint_cmd.command = cmd
  cmd_w:setOutValue(joint_cmd)
  -- Wait for the next iteration
  rr.sleep(0.01)
end
cmd_w:close()
state_w:close()
c:close()
