-- Runs the Lua ports of RR core's Reynard, standard robot and standard camera client examples
-- (examples/) against stand-in services: a Reynard mock (reynard_mock.py) and the loopback
-- robot and camera services from the pinned Racket client checkout (.deps/refs).
local t = require("testutil")
local rr = require("robotraconteur")

io.write("runtime: ", jit and jit.version or _VERSION, "\n")
local python = os.getenv("RRLUA_PYTHON")
assert(python and python ~= "", "RRLUA_PYTHON not set")
local lua = arg[-1]
local root = "../.."
local refs = root .. "/.deps/refs/robotraconteur_racket/test"
local base = tonumber(os.getenv("RRLUA_TEST_PORT") or "22231") + 200
local tmp = os.tmpname()
os.remove(tmp)
os.execute("mkdir -p " .. tmp)

local function read(path)
  local f = io.open(path, "rb")
  if not f then return nil end
  local s = f:read("*a")
  f:close()
  return s
end

-- Start a Python service in the background; wait for its READY line.
local function start(name, cmd)
  local out = tmp .. "/" .. name .. ".out"
  -- The service writes its own pid (exec), so cleanup stops the Python process itself.
  os.execute(string.format("(sh -c 'echo $$ > %s/%s.pid; exec %s' > %s 2>&1; echo exited >> %s) &",
    tmp, name, cmd, out, out))
  for _ = 1, 600 do
    local s = read(out) or ""
    if s:find("READY") then return end
    if s:find("exited") then error(name .. " failed:\n" .. s) end
    rr.sleep(0.05)
  end
  error(name .. " did not start:\n" .. (read(out) or ""))
end

-- Run an example; returns its exit status and output.
local function run(script, args)
  local out = tmp .. "/run.out"
  local cmd = string.format("cd %s && %s %s %s > %s 2>&1; echo $?", tmp, lua, script, args, out)
  local p = io.popen(cmd)
  local status = p:read("*l")
  p:close()
  return tonumber(status), read(out) or ""
end

local ex = io.popen("cd " .. root .. "/examples && pwd"):read("*l")
local robdef = root .. "/.deps/src/robotraconteur/examples/reynard_the_robot/robdef/experimental.reynard_the_robot.robdef"
local reynard_url = "rr+tcp://localhost:" .. base .. "?service=reynard"
local robot_url = "rr+tcp://localhost:" .. (base + 1) .. "?service=robot"
local camera_url = "rr+tcp://localhost:" .. (base + 2) .. "?service=camera"

local function stop_all()
  for _, name in ipairs({ "reynard", "robot", "camera" }) do
    local pid = read(tmp .. "/" .. name .. ".pid")
    if pid then os.execute("kill " .. pid:gsub("%s", "") .. " 2>/dev/null") end
  end
end

local ok, err = pcall(function()
start("reynard", string.format("%s reynard_mock.py %s %d %s/reynard.log", python, robdef, base, tmp))
start("robot", string.format("%s %s/loopback_robot.py %d", python, refs, base + 1))
start("camera", string.format("%s %s/loopback_camera.py %d", python, refs, base + 2))
end)
if not ok then
  stop_all()
  error(err)
end

t.check("reynard client", function()
  local status, out = run(ex .. "/reynard_the_robot/reynard_client.lua", reynard_url)
  t.eq(status, 0, out)
  t.truthy(out:find("New message: Hello, World From Robot Raconteur!", 1, true), out)
  t.truthy(out:find("Color: rr.array double[3] 1, 0, 0", 1, true), out)
  local log = read(tmp .. "/reynard.log")
  for _, want in ipairs({ "teleport 0.1 -0.2", "drive_robot 0.5 -0.2 -1.0 False", "drive_robot 0.0 0.0 -1.0 False",
    "setf_arm_position 1.7453 -0.5236 -1.2217", "drive_arm 0.1745 -0.5236 -0.2618 1.5 True",
    "say Hello, World From Robot Raconteur!" }) do
    t.truthy(log:find(want, 1, true), "service saw: " .. want .. "\n" .. log)
  end
end)

local robot = rr.connectService(robot_url)
local function joint1() return robot:peekWireInValue("robot_state").joint_position[1] end

t.check("robot jog_freespace", function()
  local status, out = run(ex .. "/standard_devices/robot/robot_client_jog_freespace.lua", robot_url .. " 2")
  t.eq(status, 0, out)
  t.truthy(out:find("0x%x+"), out)
end)

t.check("robot position_command", function()
  local before = joint1()
  local status, out = run(ex .. "/standard_devices/robot/robot_client_position_command.lua", robot_url .. " 2")
  t.eq(status, 0, out)
  t.truthy(math.abs(joint1() - before) > 1e-3, "the robot followed the commands")
end)

t.check("robot trajectory", function()
  local status, out = run(ex .. "/standard_devices/robot/robot_client_trajectory.lua", "abb " .. robot_url)
  t.eq(status, 0, out)
  t.truthy(out:find("action_status"), out)
  local p = robot:peekWireInValue("robot_state").joint_position
  t.truthy(math.abs(p[3] - 0.25) < 0.05, "ended near the trajectory's end: " .. tostring(p))
end)

t.check("camera capture_frame (raw, saved as PPM)", function()
  local status, out = run(ex .. "/standard_devices/camera/camera_client_capture_frame.lua", camera_url .. " " .. tmp .. "/raw")
  t.eq(status, 0, out)
  local ppm = read(tmp .. "/raw.ppm")
  t.truthy(ppm and ppm:sub(1, 13) == "P6\n96 48\n255\n", "PPM header")
  t.eq(ppm:sub(14, 16), "\255\0\0", "left third red")
  t.eq(ppm:sub(-3), "\0\0\255", "right third blue")
end)

t.check("camera capture_frame_compressed (PNG)", function()
  local status, out = run(ex .. "/standard_devices/camera/camera_client_capture_frame_compressed.lua",
    camera_url .. " " .. tmp .. "/compressed")
  t.eq(status, 0, out)
  t.eq((read(tmp .. "/compressed.png") or ""):sub(1, 8), "\137PNG\r\n\26\n")
end)

t.check("camera frame_stream and preview_stream", function()
  local status, out = run(ex .. "/standard_devices/camera/camera_client_image.lua", camera_url .. " 1.5 " .. tmp .. "/stream")
  t.eq(status, 0, out)
  local n = tonumber(out:match("Received (%d+) frames"))
  t.truthy(n and n > 5, out)
  t.truthy(read(tmp .. "/stream.ppm"), "newest frame saved")
  status, out = run(ex .. "/standard_devices/camera/camera_client_image_preview.lua", camera_url .. " 1.5 " .. tmp .. "/preview")
  t.eq(status, 0, out)
  t.truthy(read(tmp .. "/preview.png"), "newest preview frame saved")
end)

robot:close()
stop_all()
rr.sleep(0.5) -- let the services exit before the next run reuses their node names
os.execute("rm -rf " .. tmp)
t.finish()
