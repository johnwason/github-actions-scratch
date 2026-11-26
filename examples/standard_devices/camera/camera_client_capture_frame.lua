-- Capture a single frame from a standard camera (com.robotraconteur.imaging.Camera)
-- and save it to a file. A Lua port of RR core's
-- examples/standard_devices/camera/python/camera_client_capture_frame.py
--
--     lua camera_client_capture_frame.lua [url] [output path without extension]
local dir = (arg[0]:match("(.*[/\\])") or "./")
package.path = dir .. "?.lua;" .. package.path
local rr = require("robotraconteur")
local image_util = require("image_util")

-- URL for connecting to the camera. By default connects to the Gazebo simulated camera.
local url = arg[1] or "rr+tcp://localhost:59823?service=camera"
local out = arg[2] or "frame"

-- Connect to the camera
local cam = rr.connectService(url)

-- Capture the frame from the camera, in raw format
local raw_frame = cam:callFunction("capture_frame")

-- Save it as a PPM or PGM file
local encodings = cam:getConstants("com.robotraconteur.image").ImageEncoding
print("Saved " .. image_util.save_image(raw_frame, out, encodings))
cam:close()
