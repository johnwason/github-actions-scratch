-- Reynard the Robot client: a Lua port of RR core's
-- examples/reynard_the_robot/python/client/reynard_robotraconteur_client.py
--
--     lua reynard_client.lua [url]
local rr = require("robotraconteur")

-- Connect to the Reynard service using a URL
local url = arg[1] or "rr+tcp://localhost:29200?service=reynard"
local c = rr.connectService(url)

-- Listen for new messages (listeners run while the script waits in rr.sleep or a call)
c:addEventListener("new_message", function(msg) print("New message: " .. msg) end)

-- Read the current state using a wire "peek". Can also "connect" to receive streaming updates.
local state = c:peekWireInValue("state")
print(string.format("time %.3f, robot position (%g, %g)", state.time, state.robot_position[1],
  state.robot_position[2]))

-- Teleport the robot
c:callFunction("teleport", 0.1, -0.2)

-- Drive the robot with no timeout
c:callFunction("drive_robot", 0.5, -0.2, -1, false)

-- Wait for one second
rr.sleep(1)

-- Stop the robot
c:callFunction("drive_robot", 0, 0, -1, false)

-- Set the arm position
c:callFunction("setf_arm_position", math.rad(100), math.rad(-30), math.rad(-70))

-- Drive the arm using timeout and wait
c:callFunction("drive_arm", math.rad(10), math.rad(-30), math.rad(-15), 1.5, true)

-- Set the color to red
c:setProperty("color", { 1, 0, 0 })

-- Read the color
print("Color: " .. tostring(c:getProperty("color")))

rr.sleep(1)

-- Reset the color
c:setProperty("color", { 0.929, 0.49, 0.192 })

-- Say hello
c:callFunction("say", "Hello, World From Robot Raconteur!")

-- Give the new_message event time to arrive, then disconnect
rr.sleep(0.5)
c:close()
