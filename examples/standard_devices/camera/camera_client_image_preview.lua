-- Receive the preview stream (preview_stream, reduced-resolution compressed frames) from a standard camera and save the newest frame to a file.
-- A Lua port of RR core's examples/standard_devices/camera/python/camera_client_image_preview.py,
-- which shows the stream in an OpenCV window.
--
--     lua camera_client_image_preview.lua [url] [seconds] [output path without extension]
local dir = (arg[0]:match("(.*[/\\])") or "./")
package.path = dir .. "?.lua;" .. package.path
local rr = require("robotraconteur")
local image_util = require("image_util")

-- URL for connecting to the camera. By default connects to the Gazebo simulated camera.
local url = arg[1] or "rr+tcp://localhost:59823?service=camera"
local duration = tonumber(arg[2]) or 5
local out = arg[3] or "frame"

-- Connect to the camera
local cam = rr.connectService(url)
local encodings = cam:getConstants("com.robotraconteur.image").ImageEncoding

-- Connect the pipe to get the pipe endpoint p
local p = cam:connectPipe("preview_stream")

local current_frame, frames = nil, 0
-- This listener runs (in rr.sleep) when new packets have arrived: keep the newest frame
p:addPacketReceivedListener(function()
  while p:available() > 0 do
    current_frame = p:receivePacket()
    frames = frames + 1
  end
end)

-- Start the camera streaming, ignore the error if it is already streaming
local ok, err = pcall(function() cam:callFunction("start_streaming") end)
if not ok then print(err) end

-- Receive frames for a while
local t0 = rr.time()
while rr.time() - t0 < duration do
  rr.sleep(0.05)
end

p:close()
pcall(function() cam:callFunction("stop_streaming") end)
print(string.format("Received %d frames", frames))
if current_frame then print("Saved " .. image_util.save_compressed_image(current_frame, out)) end
cam:close()
