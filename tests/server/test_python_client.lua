-- M4 acceptance: RR core's Python client exercises a Lua service for every member kind
-- (properties, functions, events, objrefs, wire broadcaster and receiver, pipes).
local t = require("testutil")
local rr = require("robotraconteur")

io.write("runtime: ", jit and jit.version or _VERSION, "\n")
local python = os.getenv("RRLUA_PYTHON")
if not python or python == "" then
  io.write("RRLUA_PYTHON not set\n")
  os.exit(1)
end

local f = assert(io.open("lua_server_test.robdef", "r"))
local DEF = f:read("*a")
f:close()

local node = rr.createNode { name = "experimental.lua_python_peer_service" }
node:registerServiceType(DEF)
local port = node:startTcpServer(0)

local state = { speed = 1.5, pos = { x = 0, y = 0 } }
gen_events = {}
local svc
svc = node:registerService("svc", "experimental.lua_server_test.Root", {
  getProperty = function(name)
    if name == "name" then return "lua service" end
    return state[name]
  end,
  setProperty = function(name, value)
    if name == "speed" and value < 0 then error("speed must not be negative") end
    state[name] = value
  end,
  callFunction = function(name, ...)
    if name == "add" then local a, b = ...; return a + b end
    if name == "fail" then error((...)) end
    if name == "count" then
      local n, i = ..., 0
      return function()
        i = i + 1
        if i > n then error(rr.StopIteration) end
        if i == 99 then error("bad count") end
        return i
      end
    end
    if name == "running_sum" then
      local sum = 0
      return {
        next = function(self, x) sum = sum + x; return sum end,
        close = function(self) gen_events[#gen_events + 1] = "close" end,
        abort = function(self) gen_events[#gen_events + 1] = "abort" end,
      }
    end
    if name == "midpoint" then
      local a, b = ...
      return { x = (a.x + b.x) / 2, y = (a.y + b.y) / 2 }
    end
    if name == "ping" then svc:fireEvent("moved", { x = 3, y = 4 }, 0.5) end
  end,
})
local function child(id)
  return {
    getProperty = function(name) if name == "id" then return id end end,
    callFunction = function(name, who) if name == "hello" then return "hello " .. who .. " from " .. id end end,
  }
end
svc:addObjRef("child", "Child", child(0))
svc:addObjRef("kids", 7, "Child", child(7))
local bcast = svc:wireBroadcaster("state")
local recv = svc:wireReceiver("command")
local pipe = svc:pipe("frames")
buf = svc:memory("buf", rr.array("double", 10))
grid = svc:memory("grid", rr.multidim("int32", { 3, 4 }))

local out = os.tmpname()
local done = out .. ".done"
os.remove(done)
-- Run the client in the background; this state keeps stepping to serve it.
os.execute(string.format("(%s python_client.py %d > %s 2>&1; echo $? > %s) &", python, port, out, done))

local n, echoed = 0, 0
local deadline = os.time() + 60
while os.time() < deadline do
  n = n + 1
  bcast:setOutValue({ n, n + 1, n + 2 })
  while true do
    local ok, p = pipe:tryReceivePacket()
    if not ok then break end
    local d = rr.array("double", p:numel())
    for i = 1, p:numel() do d[i] = p[i] * 2 end
    pipe:sendPacket(d)
    echoed = echoed + 1
  end
  rr.sleep(0.01)
  local fd = io.open(done, "r")
  if fd then fd:close(); break end
end

local fo = assert(io.open(out, "r"))
local log = fo:read("*a")
fo:close()
local fd = io.open(done, "r")
local status = fd and fd:read("*l") or "timeout"
if fd then fd:close() end
os.remove(out); os.remove(done)
io.write(log)

t.check("the Python client passed every check", function()
  t.eq(status, "0", "python exit status")
  for _, what in ipairs({ "properties", "functions", "events", "objrefs", "generators", "memories", "wires", "pipes", "done" }) do
    t.truthy(log:find("ok " .. what, 1, true), "python check " .. what)
  end
end)
t.check("values the Python client sent", function()
  t.eq(state.speed, 99.0)
  local ok, v = recv:tryGetInValue()
  t.eq(ok, true); t.eq(v[1], 7); t.eq(v[2], 8)
  t.eq(echoed, 1)
  t.eq(buf:read(4, 1)[1], 2.5)
  local gr = grid:read({ 1, 1 }, { 2, 2 })
  t.eq(gr[1], 5); t.eq(gr[2], 7); t.eq(gr[3], 6)
end)

svc:close()
node:shutdown()
t.finish()
