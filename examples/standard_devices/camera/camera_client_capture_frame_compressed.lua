-- Capture a single compressed frame from a standard camera (com.robotraconteur.imaging.Camera)
-- and save it to a file. A Lua port of RR core's
-- examples/standard_devices/camera/python/camera_client_capture_frame_compressed.py
--
--     lua camera_client_capture_frame_compressed.lua [url] [output path without extension]
local dir = (arg[0]:match("(.*[/\\])") or "./")
package.path = dir .. "?.lua;" .. package.path
local rr = require("robotraconteur")
local image_util = require("image_util")

-- URL for connecting to the camera. By default connects to the Gazebo simulated camera.
local url = arg[1] or "rr+tcp://localhost:59823?service=camera"
local out = arg[2] or "frame"

-- Connect to the camera
local cam = rr.connectService(url)

-- Capture the compressed frame from the camera (PNG or JPEG)
local raw_frame = cam:callFunction("capture_frame_compressed")

-- Save the compressed bytes as is
print("Saved " .. image_util.save_compressed_image(raw_frame, out))
cam:close()
