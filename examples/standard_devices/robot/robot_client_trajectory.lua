-- Command a standard robot with joint trajectories (the execute_trajectory generator).
-- A Lua port of RR core's examples/standard_devices/robot/python/robot_client_trajectory.py
--
--     lua robot_client_trajectory.lua abb|ur [url]
local rr = require("robotraconteur")

local robot_type = arg[1]
-- URL for connecting to the robot. By default connects to the UR5e simulated robot.
local url = arg[2] or "rr+tcp://localhost:52511?service=robot"

local j_end
if robot_type == "abb" then
  j_end = { 0, -0.1, 0.25, 0, 0, 0 }
elseif robot_type == "ur" then
  j_end = { 0, -1, -1.5, 0, 0, 0 }
else
  error("usage: lua robot_client_trajectory.lua abb|ur [url]")
end

-- Connect to the robot
local c = rr.connectService(url)

-- Get the joint names from the robot_info data
local robot_info = c:getProperty("robot_info")
local joint_names = {}
for i, j in ipairs(robot_info.joint_info) do joint_names[i] = j.joint_identifier.name end

-- Retrieve the current robot state and print the current command mode
print(c:peekWireInValue("robot_state").command_mode)

-- Change the robot command mode, first to halt, then to trajectory mode
c:setProperty("command_mode", "halt")
rr.sleep(0.1)
c:setProperty("command_mode", "trajectory")

-- Connect to the robot_state wire to get real-time streaming state data
local state_w = c:connectWire("robot_state")
while not state_w:inValueValid() do rr.sleep(0.01) end
local j_start = state_w:getInValue().joint_position

-- New JointTrajectoryWaypoint and JointTrajectory structures, every field at its default
local function waypoint(joint_position, time_from_start)
  local wp = c:newStructure("com.robotraconteur.robotics.trajectory.JointTrajectoryWaypoint")
  wp.joint_position = joint_position
  wp.time_from_start = time_from_start
  return wp
end

local function trajectory(waypoints)
  local traj = c:newStructure("com.robotraconteur.robotics.trajectory.JointTrajectory")
  traj.joint_names = joint_names
  traj.waypoints = waypoints
  return traj
end

-- Build waypoints that move the robot to j_end in 10 seconds
local waypoints = {}
for i = 0, 250 do
  local p = rr.array("double", #j_end)
  for k = 1, #j_end do p[k] = (j_end[k] - j_start[k]) * (i / 250.0) + j_start[k] end
  waypoints[#waypoints + 1] = waypoint(p, i / 25.0)
end

c:setProperty("speed_ratio", 1)

-- Execute the trajectory: execute_trajectory returns a generator of TrajectoryStatus
local traj_gen = c:callGenerator("execute_trajectory", trajectory(waypoints))

-- Monitor the motion until the trajectory is complete
while true do
  local res, gen_state = traj_gen:tryNext()
  if not res then break end
  print(string.format("action_status %d, waypoint %d, time %.2f", gen_state.action_status,
    gen_state.current_waypoint, gen_state.trajectory_time))
end

-- Execute more trajectories. Trajectories are queued if executed concurrently.
waypoints = {}
for i = 0, 250 do
  local t = i / 25.0
  local p = rr.array("double", #j_end)
  local s = math.rad(15) * math.sin(2 * math.pi * (t / 10.0))
  local shape = { 1, 0, 0, 0, 0.5, -1 }
  for k = 1, #j_end do p[k] = s * shape[k] + j_end[k] end
  waypoints[#waypoints + 1] = waypoint(p, t)
end

c:setProperty("speed_ratio", 0.5)
traj_gen = c:callGenerator("execute_trajectory", trajectory(waypoints))
c:setProperty("speed_ratio", 2)
local traj_gen2 = c:callGenerator("execute_trajectory", trajectory(waypoints))
print(traj_gen2:next().action_status)

for _, g in ipairs({ traj_gen, traj_gen2 }) do
  while true do
    local res, gen_state = g:tryNext()
    if not res then break end
    print(gen_state.action_status)
  end
end

state_w:close()
c:close()
