-- Client tests against RR core's C++ test server, TestService3 (RobotRaconteurTestService2).
-- Ported from RR core's test/python/RobotRaconteurTest/ServiceTestClient2.py (enums, complex,
-- bools, exceptions). Pods and namedarrays need the LFSR fill/verify port (see PROGRESS.md).
local t = require("testutil")
local rr = require("robotraconteur")
local st = require("servicetest")

io.write("runtime: ", jit and jit.version or _VERSION, "\n")

local V, same = st.vector, st.same

-- A complex array from a vector of interleaved real and imaginary parts.
local function cvec(name, ctype, rtype, dims_name)
  local v = V(name, rtype)
  local n = math.floor(#v / 2)
  local a = rr.array(ctype, n)
  for i = 1, n do a[i] = { real = v[2 * i - 1], imag = v[2 * i] } end
  if dims_name then return rr.multidim(ctype, V(dims_name, "uint32"):toTable(), a) end
  return a
end

st.start_server()
local node = rr.createNode { name = "experimental.lua_ts3_client" }
local r = st.connect(node, "RobotRaconteurTestService2")

t.check("connected", function()
  t.eq(r:getType(), "com.robotraconteur.testing.TestService3.testroot3")
end)

t.check("enums", function()
  t.eq(r:getProperty("testenum1_prop"), -1, "anothervalue")
  r:setProperty("testenum1_prop", "hexval1")
  r:setProperty("testenum1_prop", 0x10)
end)

t.check("complex", function()
  local c1 = r:getProperty("c1"); t.eq(c1.real, 5.708705e+01); t.eq(c1.imag, -2.328294e-03)
  r:setProperty("c1", { real = 5.708705e+01, imag = -2.328294e-03 })
  same(r:getProperty("c2"), cvec("ts3.c2.to_client", "cdouble", "double"), "c2")
  r:setProperty("c2", cvec("ts3.c2.to_service", "cdouble", "double"))
  same(r:getProperty("c3"), cvec("ts3.c3.to_client", "cdouble", "double", "ts3.c3.to_client.dims"), "c3")
  r:setProperty("c3", cvec("ts3.c3.to_service", "cdouble", "double", "ts3.c3.to_service.dims"))
  same(r:getProperty("c5")[1], cvec("ts3.c5.to_client", "cdouble", "double"), "c5")
  r:setProperty("c5", { cvec("ts3.c5.to_service", "cdouble", "double") })
  local c7 = r:getProperty("c7")
  t.truthy(math.abs(c7.real - -5.527021e-18) < 1e-23 and math.abs(c7.imag - -9.848457e+03) < 1e-2, "c7")
  r:setProperty("c7", { real = 9.303345e-12, imag = -3.865684e-05 })
  same(r:getProperty("c8"), cvec("ts3.c8.to_client", "csingle", "single"), "c8")
  r:setProperty("c8", cvec("ts3.c8.to_service", "csingle", "single"))
  same(r:getProperty("c9"), cvec("ts3.c9.to_client", "csingle", "single", "ts3.c9.to_client.dims"), "c9")
  r:setProperty("c9", cvec("ts3.c9.to_service", "csingle", "single", "ts3.c9.to_service.dims"))
end)

t.check("bools", function()
  r:setProperty("b1", true); t.eq(r:getProperty("b1"), true)
  r:setProperty("b2", { true, false, false, true, true, true, false, true })
  same(r:getProperty("b2"), rr.array("bool", { true, false, true, true, false, true, false }), "b2")
  r:setProperty("b3", rr.multidim("bool", { 2, 1 }, { true, false }))
  same(r:getProperty("b3"), rr.multidim("bool", { 2, 2 }, { false, true, true, false }), "b3")
  r:setProperty("b4", { true }); t.eq(r:getProperty("b4")[1], true)
  r:setProperty("b5", { rr.array("bool", { true, false }) })
  same(r:getProperty("b5")[1], rr.array("bool", { false, true, false, false }), "b5")
  r:setProperty("b6", { rr.multidim("bool", { 2, 1 }, { true, false }) })
  same(r:getProperty("b6")[1], rr.multidim("bool", { 2, 2 }, { false, true, true, false }), "b6")
end)

t.check("exceptions carry the RR error name and message", function()
  t.raises(function() r:callFunction("test_exception_params1") end, "RobotRaconteur.InvalidOperation: test error")
  t.raises(function() r:callFunction("test_exception_params2") end,
    "com.robotraconteur.testing.TestService3.test_exception4: test error2")
end)

r:close()
node:shutdown()
st.stop_server()
t.finish()
