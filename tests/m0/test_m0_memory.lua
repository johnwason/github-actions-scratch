-- M0.4: memory stays flat over 50,000 C++ throws with a 4 KB string argument, through
-- both the SWIG %exception path and the %native guard path.
local t = require("testutil")
local m = require("rrlua_m0")

io.write("runtime: ", jit and jit.version or _VERSION, "\n")

local N = 50000
local LIMIT_KB = 8 * 1024 -- a leak of the argument and message would be ~400 MB
local arg4k = string.rep("x", 4096)
local p = m.Probe()

local function run(name, fn)
  t.check(name, function()
    for _ = 1, 2000 do pcall(fn, arg4k) end -- warm up allocator and caches
    collectgarbage(); collectgarbage()
    local before = t.rss_kb()
    for _ = 1, N do
      local ok = pcall(fn, arg4k)
      if ok then error("expected a throw") end
    end
    collectgarbage(); collectgarbage()
    local after = t.rss_kb()
    io.write(string.format("     %s: RSS %d KB -> %d KB over %d throws\n", name, before, after, N))
    t.truthy(after - before < LIMIT_KB, "RSS growth " .. (after - before) .. " KB")
    t.eq(p:liveTrackers(), 0, "live trackers")
  end)
end

run("SWIG method throws", function(s) p:throwWith(s) end)
run("%native throws", function(s) m.varargs_throw(s) end)

t.finish()
