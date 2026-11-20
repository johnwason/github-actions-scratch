-- Client tests against RR core's C++ test server, TestService3 (RobotRaconteurTestService2).
-- Ported from RR core's test/python/RobotRaconteurTest/ServiceTestClient2.py (enums, complex,
-- bools, exceptions, pods, namedarrays). The server checks every value we send.
local t = require("testutil")
local rr = require("robotraconteur")
local st = require("servicetest")
local seq = require("seqgen")

io.write("runtime: ", jit and jit.version or _VERSION, "\n")

local V, same = st.vector, st.same
seq.init(st.vector)

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

t.check("pods", function()
  seq.verify(seq.verify_testpod1, r:getProperty("testpod1_prop"), 563921043)
  r:setProperty("testpod1_prop", seq.fill_testpod1(85932659))
  seq.verify(seq.verify_testpod1, r:callFunction("testpod1_func2"), 95836295)
  r:callFunction("testpod1_func1", seq.fill_testpod1(29546592))
  -- The server rejects a pod filled from the wrong seed.
  t.raises(function() r:setProperty("testpod1_prop", seq.fill_testpod1(85932658)) end)
end)

t.check("teststruct3", function()
  seq.verify(seq.verify_teststruct3, r:getProperty("teststruct3_prop"), 16483675)
  r:setProperty("teststruct3_prop", seq.fill_teststruct3(858362))
  local bad = seq.fill_teststruct3(858362)
  bad.t1.translation.z = bad.t1.translation.z + 1
  t.raises(function() r:setProperty("teststruct3_prop", bad) end)
  -- The Lua verifier itself catches a wrong seed.
  t.raises(function() seq.verify(seq.verify_teststruct3, r:getProperty("teststruct3_prop"), 16483676) end,
    "mismatches")
end)

t.check("namedarrays", function()
  r:setProperty("testnamedarray1", seq.fill_transform(3956378).translation)
  local a1 = seq.fill_transform(74637)
  a1.translation = r:getProperty("testnamedarray1")
  seq.verify(seq.verify_transform, a1, 74637)
  r:setProperty("testnamedarray2", seq.fill_transform(827635))
  seq.verify(seq.verify_transform, r:getProperty("testnamedarray2"), 1294)
  r:setProperty("testnamedarray3", seq.fill_transform_array(6, 19274))
  seq.verify(seq.verify_transform_array, r:getProperty("testnamedarray3"), 8, 837512)
  r:setProperty("testnamedarray4", seq.fill_transform_multidimarray(5, 2, 6385))
  seq.verify(seq.verify_transform_multidimarray, r:getProperty("testnamedarray4"), 7, 2, 66134)
  r:setProperty("testnamedarray5", seq.fill_transform_multidimarray(3, 2, 7732))
  seq.verify(seq.verify_transform_multidimarray, r:getProperty("testnamedarray5"), 3, 2, 773142)
  t.raises(function() r:setProperty("testnamedarray4", seq.fill_transform_multidimarray(5, 2, 6386)) end)
  t.raises(function()
    local m = seq.fill_transform_multidimarray(5, 2, 6385); m.dims = { 2, 5 }
    r:setProperty("testnamedarray4", m)
  end)
  t.raises(function()
    local m = seq.fill_transform_multidimarray(5, 2, 6385); m.dims = nil
    r:setProperty("testnamedarray4", m)
  end, "needs a dims field")
end)

t.check("wire peek and poke", function()
  t.eq(r:peekWireInValue("peekwire"), 56295674)
  r:pokeWireOutValue("pokewire", 75738265)
  t.eq(r:peekWireOutValue("pokewire"), 75738265)
  local w = r:connectWire("pokewire")
  t.eq(w:direction(), "writeonly")
  for _ = 1, 3 do w:setOutValue(8638356) end
  rr.sleep(0.1)
  t.eq(r:peekWireOutValue("pokewire"), 8638356)
  w:close()
  t.raises(function() r:pokeWireOutValue("peekwire", 1) end)
end)

r:close()
node:shutdown()
st.stop_server()
t.finish()
