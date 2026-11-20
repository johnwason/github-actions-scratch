-- Client tests against RR core's C++ test server, TestService5 (RobotRaconteurTestService3, an
-- asynchronous service). Ported from RR core's test/python/RobotRaconteurTest/ServiceTestClient3.py.
-- Generator members (f4, f5, f6, err_func3) are M3.
local t = require("testutil")
local rr = require("robotraconteur")
local st = require("servicetest")

io.write("runtime: ", jit and jit.version or _VERSION, "\n")

st.start_server()
local node = rr.createNode { name = "experimental.lua_ts5_client" }
local r = st.connect(node, "RobotRaconteurTestService3")

t.check("connected", function()
  t.eq(r:getType(), "com.robotraconteur.testing.TestService5.asynctestroot")
end)

t.check("properties", function()
  t.eq(r:getProperty("d1"), 8.5515)
  r:setProperty("d1", 3.0819)
  t.raises(function() r:setProperty("d1", 3.0818) end, "RobotRaconteur.InvalidArgument")
  t.raises(function() r:getProperty("err") end, "RobotRaconteur.InvalidArgument: Test message 1")
  t.raises(function() r:setProperty("err", 100) end, "RobotRaconteur.InvalidOperation: Test message 2")
end)

t.check("functions", function()
  t.eq(r:callFunction("f1"), nil)
  r:callFunction("f2", 247)
  t.raises(function() r:callFunction("f2", 246) end, "RobotRaconteur.InvalidArgument")
  t.eq(r:callFunction("f3", 1, 2), 3)
  t.raises(function() r:callFunction("err_func") end, "RobotRaconteur.InvalidOperation")
  t.raises(function() r:callFunction("err_func2") end, "com.robotraconteur.testing.TestService5.asynctestexp")
end)

r:close()
node:shutdown()
st.stop_server()
t.finish()
