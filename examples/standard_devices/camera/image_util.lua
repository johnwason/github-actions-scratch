-- Helpers for the camera examples: save com.robotraconteur.image images to files (the Python
-- examples display them with OpenCV, which Lua does not have).
local M = {}

-- Save a com.robotraconteur.image.Image (raw pixels) as a PPM (color) or PGM (mono) file.
-- encodings is cam:getConstants("com.robotraconteur.image").ImageEncoding.
function M.save_image(img, path, encodings)
  local info = img.image_info
  local w, h, step = info.width, info.height, info.step
  local bytes = img.data:toBytes()
  local enc = info.encoding
  local rows = {}
  if enc == encodings.rgb888 or enc == encodings.bgr888 then
    for y = 0, h - 1 do
      local row = bytes:sub(y * step + 1, y * step + w * 3)
      if enc == encodings.bgr888 then row = row:gsub("(.)(.)(.)", "%3%2%1") end
      rows[#rows + 1] = row
    end
    path = path .. ".ppm"
    local f = assert(io.open(path, "wb"))
    f:write(string.format("P6\n%d %d\n255\n", w, h), table.concat(rows))
    f:close()
  elseif enc == encodings.mono8 then
    for y = 0, h - 1 do rows[#rows + 1] = bytes:sub(y * step + 1, y * step + w) end
    path = path .. ".pgm"
    local f = assert(io.open(path, "wb"))
    f:write(string.format("P5\n%d %d\n255\n", w, h), table.concat(rows))
    f:close()
  else
    error("image encoding " .. tostring(enc) .. " is not handled by this example")
  end
  return path
end

-- Save a com.robotraconteur.image.CompressedImage (PNG or JPEG bytes) as is.
function M.save_compressed_image(img, path)
  local bytes = img.data:toBytes()
  if bytes:sub(1, 8) == "\137PNG\r\n\26\n" then
    path = path .. ".png"
  elseif bytes:sub(1, 2) == "\255\216" then
    path = path .. ".jpg"
  else
    path = path .. ".bin"
  end
  local f = assert(io.open(path, "wb"))
  f:write(bytes)
  f:close()
  return path
end

return M
