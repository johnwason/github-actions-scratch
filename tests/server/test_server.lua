-- M4: a Lua service, used over TCP by a Lua client on a second node in the same state. The
-- client's blocking calls run the pump, which runs the service's handlers.
local t = require("testutil")
local rr = require("robotraconteur")

io.write("runtime: ", jit and jit.version or _VERSION, "\n")

local f = assert(io.open("lua_server_test.robdef", "r"))
local DEF = f:read("*a")
f:close()

local server = rr.createNode { name = "experimental.lua_server" }
t.check("register type, start TCP server", function()
  t.eq(server:registerServiceType(DEF), "experimental.lua_server_test")
end)
local port = server:startTcpServer(0)
t.truthy(port > 0, "listening port")

local state = { speed = 1.5, pos = { x = 0, y = 0 } }
local calls = 0
local svc = server:registerService("svc", "experimental.lua_server_test.Root", {
  getProperty = function(name)
    calls = calls + 1
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
    if name == "midpoint" then
      local a, b = ...
      return { x = (a.x + b.x) / 2, y = (a.y + b.y) / 2 }
    end
  end,
})
t.eq(svc:getName(), "svc"); t.eq(svc:getType(), "experimental.lua_server_test.Root")

local function child(id)
  return {
    getProperty = function(name) if name == "id" then return id end end,
    callFunction = function(name, who) if name == "hello" then return "hello " .. who .. " from " .. id end end,
  }
end
local child_svc = svc:addObjRef("child", "Child", child(0))
-- Set before any client asks for the child: the wire server does not exist yet.
child_svc:wireBroadcaster("counter"):setOutValue(5)
svc:addObjRef("kids", 7, "Child", child(7))
svc:addObjRef("kids", 9, "experimental.lua_server_test.Child", child(9))

local cnode = rr.createNode { name = "experimental.lua_server_client" }
local c = cnode:connectService("rr+tcp://localhost:" .. port .. "?service=svc")

t.check("properties", function()
  t.eq(c:getType(), "experimental.lua_server_test.Root")
  t.eq(c:getProperty("speed"), 1.5)
  c:setProperty("speed", 2.25)
  t.eq(c:getProperty("speed"), 2.25)
  t.eq(state.speed, 2.25)
  t.eq(c:getProperty("name"), "lua service")
  c:setProperty("pos", { x = 1, y = 2 })
  local p = c:getProperty("pos"); t.eq(p.x, 1); t.eq(p.y, 2)
  local err = t.raises(function() c:setProperty("speed", -1) end, "speed must not be negative")
  t.truthy(err:find("RobotRaconteur.OperationFailed"), err)
end)

t.check("functions", function()
  t.eq(c:callFunction("add", 2, 3.5), 5.5)
  local m = c:callFunction("midpoint", { x = 0, y = 0 }, { x = 2, y = 4 })
  t.eq(m.x, 1); t.eq(m.y, 2)
end)

t.check("handler errors reach the client with the message", function()
  t.raises(function() c:callFunction("fail", "boom") end, "boom")
end)

t.check("events", function()
  local got
  c:addEventListener("moved", function(p, tm) got = { p.x, p.y, tm } end)
  svc:fireEvent("moved", { x = 3, y = 4 }, 0.5)
  for _ = 1, 100 do if got then break end; rr.sleep(0.01) end
  t.truthy(got, "event received"); t.eq(got[1], 3); t.eq(got[2], 4); t.eq(got[3], 0.5)
  t.raises(function() svc:fireEvent("moved", 1) end, "takes 2 arguments")
  t.raises(function() svc:fireEvent("nosuch") end, "not found")
end)

t.check("objrefs from the registry", function()
  local ch = c:getObjRef("child")
  t.eq(ch:getType(), "experimental.lua_server_test.Child")
  t.eq(ch:getProperty("id"), 0)
  t.eq(c:getObjRef("kids", 7):callFunction("hello", "lua"), "hello lua from 7")
  t.eq(c:getObjRef("kids", 9):getProperty("id"), 9)
  t.raises(function() c:getObjRef("kids", 8) end, "not found")
  t.raises(function() svc:addObjRef("kids", "Child", child(1)) end, "is indexed")
end)

local function wait_for(cond, what)
  for _ = 1, 250 do
    if cond() then return end
    rr.sleep(0.01)
  end
  error("timed out waiting for " .. what, 2)
end

t.check("wire broadcaster and receiver", function()
  local b = svc:wireBroadcaster("state")
  b:setOutValue({ 1, 2, 3 })
  local w = c:connectWire("state")
  wait_for(function() return w:inValueValid() end, "the broadcast value")
  t.eq(w:getInValue()[3], 3)
  b:setOutValue({ 4 })
  wait_for(function() return w:getInValue():numel() == 1 end, "the new broadcast value")
  t.eq(w:getInValue()[1], 4)
  t.raises(function() b:getInValue() end, "not set")

  local r = svc:wireReceiver("command")
  t.eq(r:tryGetInValue(), false)
  local cw = c:connectWire("command")
  cw:setOutValue({ 7, 8 })
  wait_for(function() return (r:tryGetInValue()) end, "the received value")
  local ok, v = r:tryGetInValue(); t.eq(ok, true); t.eq(v[2], 8)
  t.raises(function() r:setOutValue({ 1 }) end, "receiver")
  svc:wireBroadcaster("both")
  t.raises(function() svc:wireReceiver("both") end, "already used as a broadcaster")
  t.raises(function() svc:wireBroadcaster("nosuch") end, "not found")
  w:close(); cw:close()
end)

t.check("a child object's wire, set before the objref was opened", function()
  local ch = c:getObjRef("child")
  local w = ch:connectWire("counter")
  wait_for(function() return w:inValueValid() end, "the child's wire value")
  t.eq(w:getInValue(), 5)
  w:close()
end)

t.check("pipes in both directions", function()
  local p = svc:pipe("frames")
  t.eq(p:endpointCount(), 0)
  local e = c:connectPipe("frames")
  wait_for(function() return p:endpointCount() == 1 end, "the endpoint")
  t.eq(p:sendPacket({ 1, 2 }), 1)
  wait_for(function() return e:available() == 1 end, "the packet at the client")
  t.eq(e:receivePacket()[2], 2)
  e:sendPacket({ 9 }); e:sendPacket({ 10 })
  wait_for(function() return p:available() == 2 end, "the packets at the service")
  t.eq(p:receivePacket()[1], 9)
  local ok, v = p:tryReceivePacket(); t.eq(ok, true); t.eq(v[1], 10)
  t.eq(p:tryReceivePacket(), false)
  e:close()
  wait_for(function() return p:endpointCount() == 0 end, "the endpoint to close")
end)

t.check("closing the service", function()
  c:close()
  svc:close(); svc:close()
  t.raises(function() cnode:connectService("rr+tcp://localhost:" .. port .. "?service=svc") end)
end)

cnode:shutdown()
server:shutdown()
t.finish()
