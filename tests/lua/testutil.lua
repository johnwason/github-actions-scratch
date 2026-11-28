-- Small assertion helpers shared by the Lua tests. Works on Lua 5.1 (LuaJIT) through 5.4.
local M = {}

local failures = 0
local count = 0

function M.check(name, fn)
  count = count + 1
  local ok, err = pcall(fn)
  if ok then
    io.write("ok   ", name, "\n")
  else
    failures = failures + 1
    io.write("FAIL ", name, ": ", tostring(err), "\n")
  end
end

function M.eq(a, b, what)
  if a ~= b then
    error((what or "value") .. ": expected " .. tostring(b) .. ", got " .. tostring(a), 2)
  end
end

function M.truthy(v, what)
  if not v then error((what or "condition") .. " is false", 2) end
end

function M.raises(fn, pattern)
  local ok, err = pcall(fn)
  if ok then error("expected an error", 2) end
  if pattern and not tostring(err):find(pattern) then
    error("error '" .. tostring(err) .. "' does not match '" .. pattern .. "'", 2)
  end
  return err
end

-- Resident set size of this process in KB (Linux).
function M.rss_kb()
  local f = assert(io.open("/proc/self/status", "r"))
  local s = f:read("*a")
  f:close()
  return tonumber(s:match("VmRSS:%s*(%d+)"))
end

function M.finish()
  io.write(string.format("%d checks, %d failed\n", count, failures))
  os.exit(failures == 0 and 0 or 1)
end

return M
