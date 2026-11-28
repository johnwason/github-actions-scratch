-- M2.1: explicit nodes, the per-state default node, shutdown, and rr.step draining the
-- per-state dispatch queue.
local t = require("testutil")
local rr = require("robotraconteur")

io.write("runtime: ", jit and jit.version or _VERSION, "\n")

local UUID = "^{?%x%x%x%x%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%x%x%x%x%x%x%x%x}?$"

t.check("createNode with options", function()
  local n = rr.createNode { name = "experimental.lua_test1", threads = 4 }
  t.eq(n:getNodeName(), "experimental.lua_test1")
  t.truthy(n:getNodeID():find(UUID), "node ID " .. n:getNodeID())
  t.eq(n:isOwned(), true)
  t.eq(n:isShutdown(), false)
  n:shutdown()
end)

t.check("nodes are independent", function()
  local a, b = rr.createNode(), rr.createNode()
  t.truthy(a:getNodeID() ~= b:getNodeID(), "distinct node IDs")
  a:shutdown()
  t.eq(a:isShutdown(), true)
  t.eq(b:isShutdown(), false, "shutting one down leaves the other")
  b:shutdown()
end)

t.check("bad options raise Lua errors with the script location", function()
  t.raises(function() rr.createNode { bogus = 1 } end, "test_node_step.lua:%d+: .*unknown option 'bogus'")
  t.raises(function() rr.createNode { name = 5 } end, "name must be a string")
  t.raises(function() rr.createNode { threads = 0 } end, "threads must be")
  t.raises(function() rr.createNode { threads = 1.5 } end, "threads must be")
  t.raises(function() rr.createNode "x" end, "options must be a table")
  t.raises(function() rr.createNode { [1] = 2 } end, "keys must be strings")
end)

t.check("default node is per state, lazy, and stable", function()
  local d1, d2 = rr.defaultNode(), rr.defaultNode()
  t.eq(d1:getNodeID(), d2:getNodeID(), "same default node")
  t.eq(d1:isOwned(), true)
  local n = rr.createNode()
  t.truthy(n:getNodeID() ~= d1:getNodeID(), "explicit nodes are not the default node")
  n:shutdown()
end)

t.check("shutdown is idempotent and handles share state", function()
  local n = rr.createNode { name = "experimental.lua_test2" }
  n:shutdown(); n:shutdown()
  t.eq(n:isShutdown(), true)
  t.eq(n:getNodeName(), "experimental.lua_test2", "name still readable")
  -- a replaced default node after shutdown
  local d = rr.defaultNode(); local id = d:getNodeID(); d:shutdown()
  t.eq(rr.defaultNode():isShutdown(), false, "a fresh default node")
  t.truthy(rr.defaultNode():getNodeID() ~= id, "new default node ID")
end)

t.check("Node cannot be constructed from Lua", function()
  t.raises(function() return rr.Node() end)
end)

t.check("step runs tasks posted from other threads, on this thread", function()
  t.eq(rr.step(), 0, "empty queue")
  local before = rr._testCounter()
  rr._testPostFromThread(1000)
  rr._testCounter() -- joins the posting thread
  t.eq(rr._queueSize(), 1000, "queued, not run")
  t.eq(rr._testCounter(), before, "nothing ran without step")
  t.eq(rr.step(), 1000)
  t.eq(rr._testCounter(), before + 1000)
  t.eq(rr._queueSize(), 0)
end)

t.check("step budget", function()
  rr._testPostFromThread(500); rr._testCounter()
  t.eq(rr.step(0), 1, "a zero budget runs one task")
  local n = rr.step(10)
  t.eq(n, 499, "a large budget drains the queue")
  t.raises(function() rr.step(-1) end, "budget")
  t.raises(function() rr.step("x") end, "budget")
end)

t.finish()
