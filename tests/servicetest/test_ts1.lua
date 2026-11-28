-- Client tests against RR core's C++ test server, TestService1 (RobotRaconteurTestService).
-- Ported from RR core's test/python/RobotRaconteurTest/ServiceTestClient.py; expected values
-- come from the core's test/data/test_vectors.bin.
local t = require("testutil")
local rr = require("robotraconteur")
local st = require("servicetest")

io.write("runtime: ", jit and jit.version or _VERSION, "\n")

local V, MV, same = st.vector, st.mvector, st.same
local LUAJIT = jit ~= nil

st.start_server()
local node = rr.createNode { name = "experimental.lua_ts1_client" }
local r = st.connect(node, "RobotRaconteurTestService")

t.check("connected; type and root", function()
  t.eq(r:getType(), "com.robotraconteur.testing.TestService1.testroot")
  t.eq(r:isRoot(), true)
end)

t.check("service attributes (getServiceAttributes)", function()
  -- RobotRaconteurTestServiceSupport::RegisterServices sets test (string) and test2 (int32 42).
  local a = r:getServiceAttributes()
  t.eq(a.test, "This is a test attribute")
  t.truthy(rr.isarray(a.test2))
  t.eq(a.test2:type(), "int32"); t.eq(#a.test2, 1); t.eq(a.test2[1], 42)
end)

t.check("double properties", function()
  r:setProperty("d1", 3.456); t.eq(r:getProperty("d1"), 12.345)
  r:setProperty("d2", V("ts1.d2.to_service", "double")); same(r:getProperty("d2"), V("ts1.d2.to_client", "double"), "d2")
  r:setProperty("d3", V("ts1.d3.to_service", "double")); same(r:getProperty("d3"), V("ts1.d3.to_client", "double"), "d3")
  r:setProperty("d4", V("ts1.d4.to_service", "double")); same(r:getProperty("d4"), V("ts1.d4.to_client", "double"), "d4")
  r:setProperty("d5", MV("ts1.d5.to_service", "double")); same(r:getProperty("d5"), MV("ts1.d5.to_client", "double"), "d5")
  r:setProperty("d6", MV("ts1.d6.to_service", "double")); same(r:getProperty("d6"), MV("ts1.d6.to_client", "double"), "d6")
end)

t.check("single and integer properties", function()
  r:setProperty("s1", 3847.9283)
  t.truthy(math.abs(r:getProperty("s1") - 7.8573) < 1e-4, "s1")
  r:setProperty("s2", V("ts1.s2.to_service", "single"))
  r:setProperty("i8_1", 45); t.eq(r:getProperty("i8_1"), -66)
  r:setProperty("i8_2", V("ts1.i8_2.to_service", "int8")); same(r:getProperty("i8_2"), V("ts1.i8_2.to_client", "int8"), "i8_2")
  r:setProperty("u8_1", 232); t.eq(r:getProperty("u8_1"), 222)
  r:setProperty("u8_2", V("ts1.u8_2.to_service", "uint8")); same(r:getProperty("u8_2"), V("ts1.u8_2.to_client", "uint8"), "u8_2")
  r:setProperty("u8_3", MV("ts1.u8_3.to_service", "uint8")); same(r:getProperty("u8_3"), MV("ts1.u8_3.to_client", "uint8"), "u8_3")
  r:setProperty("i16_1", 2387); t.eq(r:getProperty("i16_1"), -13428)
  r:setProperty("i16_2", V("ts1.i16_2.to_service", "int16")); same(r:getProperty("i16_2"), V("ts1.i16_2.to_client", "int16"), "i16_2")
  r:setProperty("u16_1", 54732); t.eq(r:getProperty("u16_1"), 60981)
  r:setProperty("u16_2", V("ts1.u16_2.to_service", "uint16")); same(r:getProperty("u16_2"), V("ts1.u16_2.to_client", "uint16"), "u16_2")
  r:setProperty("i32_1", -9837284); t.eq(r:getProperty("i32_1"), 898734)
  r:setProperty("i32_2", V("ts1.i32_2.to_service", "int32")); same(r:getProperty("i32_2"), V("ts1.i32_2.to_client", "int32"), "i32_2")
  r:setProperty("u32_1", 1550099195); t.eq(r:getProperty("u32_1"), 547919123)
  r:setProperty("u32_2", V("ts1.u32_2.to_service", "uint32")); same(r:getProperty("u32_2"), V("ts1.u32_2.to_client", "uint32"), "u32_2")
end)

t.check("64-bit integer properties (boxed where Lua cannot hold them)", function()
  -- Built from decimal strings, which works on every runtime.
  r:setProperty("i64_1", rr.int64("8621740821050813024"))
  local i = r:getProperty("i64_1")
  if LUAJIT then
    t.eq(tostring(i), "rr.array int64[1] -1357833931563696072", "boxed on LuaJIT")
  else
    t.eq(i, -1357833931563696072); t.eq(math.type(i), "integer")
    r:setProperty("i64_1", 8621740821050813024); t.eq(r:getProperty("i64_1"), -1357833931563696072)
  end
  r:setProperty("u64_1", rr.uint64("1465640522145789825"))
  local u = r:getProperty("u64_1") -- 13389861970863644378 > 2^63: boxed on every runtime
  t.truthy(rr.isarray(u), "boxed"); t.eq(tostring(u), "rr.array uint64[1] 13389861970863644378")
  r:setProperty("i64_2", V("ts1.i64_2.to_service", "int64")); same(r:getProperty("i64_2"), V("ts1.i64_2.to_client", "int64"), "i64_2")
  r:setProperty("u64_2", V("ts1.u64_2.to_service", "uint64")); same(r:getProperty("u64_2"), V("ts1.u64_2.to_client", "uint64"), "u64_2")
end)

t.check("string property", function()
  r:setProperty("str1", "Hello Server!"); t.eq(r:getProperty("str1"), "Hello Client!")
end)

t.check("struct property from the server", function()
  local s1 = r:getProperty("struct1")
  same(s1.dat1, V("ts1.struct1.dat1.to_client", "double"), "dat1")
  t.eq(s1.str2, "Hello world!")
  t.eq(s1.vec3[1], "Hello Client!"); t.eq(s1.vec3[2], "Hello Client, again")
  t.eq(s1.vec3[4372], "This is yet another test string")
  t.eq(s1.dict4.teststring1, "Hello Client!"); t.eq(s1.dict4.anotherstr, "This is yet another test string")
  same(s1.struct1.mydat, V("ts1.struct1.struct1_mydat.to_client", "double"), "struct1.mydat")
  same(s1.dstruct2.test1.mydat, V("ts1.struct1.dstruct2_1.to_client", "double"), "dstruct2.test1")
  same(s1.dstruct2.anothertest.mydat, V("ts1.struct1.dstruct2_3.to_client", "double"), "dstruct2.anothertest")
  same(s1.multidimarray, MV("ts1.struct1.multidimarray.to_client", "double"), "multidimarray")
  t.eq(s1.var3, "This is a vartype string")
end)

local function count(m) local n = 0; for _ in pairs(m) do n = n + 1 end; return n end

t.check("struct property to the server (the server checks every field)", function()
  r:setProperty("struct1", {
    dat1 = V("ts1.struct1.dat1.to_service", "double"),
    str2 = "Hello world 2!",
    vec3 = { [10] = "Hello Server!", [11] = "Hello Server, again", [46372] = "Test string!",
             [46373] = "Test string again" },
    dict4 = { cteststring1 = "Hello Server!", cteststring2 = "Hello Server, again" },
    list5 = { "Hello Server!", "Hello Server, again" },
    struct1 = { mydat = V("ts1.struct1.struct1_mydat.to_service", "double") },
    dstruct2 = { ctest1 = { mydat = V("ts1.struct1.dstruct2_1.to_service", "double") },
                 anothertest = { mydat = V("ts1.struct1.dstruct2_2.to_service", "double") } },
    lstruct3 = { { mydat = V("ts1.struct1.dstruct2_1.to_service", "double") },
                 { mydat = V("ts1.struct1.dstruct2_2.to_service", "double") } },
    multidimarray = MV("ts1.struct1.multidimarray.to_service", "double"),
    var3 = rr.varvalue("double[]", V("ts1.struct1.var3.to_service", "double")),
  })
  r:setProperty("struct2", { mydat = V("ts1.struct2.to_service", "double") })
  -- The server really checks: the wrong vector is rejected.
  t.raises(function() r:setProperty("struct2", { mydat = V("ts1.struct2.to_client", "double") }) end)
  t.raises(function() r:setProperty("d2", V("ts1.d2.to_client", "double")) end)
  same(r:getProperty("struct2").mydat, V("ts1.struct2.to_client", "double"), "struct2")
end)

t.check("int32 and string maps of numbers, arrays, multidims, strings and structs", function()
  local d1 = r:getProperty("is_d1")
  t.eq(count(d1), 3); t.eq(d1[9285], 1.643392e-01); t.eq(d1[74822], 1.537133e+09); t.eq(d1[4], 1.369505e-03)
  r:setProperty("is_d1", { [928] = 4.074501e-07, [394820] = -4.535303e+05, [623] = -2.956241e-20 })
  local d2 = r:getProperty("is_d2")
  t.eq(count(d2), 2); t.eq(d2.testval1, -1.079664e+16); t.eq(d2.testval2, 2.224846e+00)
  r:setProperty("is_d2", { testval3 = 5.242474e+10, testval4 = 2.208636e+08 })
  local d3 = r:getProperty("is_d3")
  t.eq(count(d3), 2)
  same(d3[12], V("ts1.is_d3.to_client.a", "double")); same(d3[832], V("ts1.is_d3.to_client.b", "double"))
  r:setProperty("is_d3", { [47] = V("ts1.is_d3.to_service.a", "double"), [324] = V("ts1.is_d3.to_service.b", "double") })
  local d4 = r:getProperty("is_d4")
  same(d4.testval1, V("ts1.is_d4.to_client.a", "double")); same(d4.testval2, V("ts1.is_d4.to_client.b", "double"))
  r:setProperty("is_d4", { testval3 = V("ts1.is_d4.to_service.a", "double"), testval4 = V("ts1.is_d4.to_service.b", "double") })
  local d5 = r:getProperty("is_d5")
  t.eq(count(d5), 1); same(d5[564], MV("ts1.is_d5.to_client", "double"), "is_d5")
  r:setProperty("is_d5", { [328] = MV("ts1.is_d5.to_service", "double") })
  local d6 = r:getProperty("is_d6")
  t.eq(count(d6), 1); same(d6.testval1, MV("ts1.is_d6.to_client", "double"), "is_d6")
  r:setProperty("is_d6", { testval2 = MV("ts1.is_d6.to_service", "double") })
  local s1 = r:getProperty("is_str1"); t.eq(count(s1), 1); t.eq(s1[23], "Hello server")
  r:setProperty("is_str1", { [24] = "Hello client" })
  local s2 = r:getProperty("is_str2"); t.eq(count(s2), 1); t.eq(s2.testval1, "Hello server")
  r:setProperty("is_str2", { testval2 = "Hello client" })
  same(r:getProperty("is_struct1")[748].mydat, V("ts1.is_struct1.to_client", "double"), "is_struct1")
  r:setProperty("is_struct1", { [372] = { mydat = V("ts1.is_struct1.to_service", "double") } })
  same(r:getProperty("is_struct2").testval1.mydat, V("ts1.is_struct2.to_client", "double"), "is_struct2")
  r:setProperty("is_struct2", { testval2 = { mydat = V("ts1.is_struct2.to_service", "double") } })
end)

t.check("list property and a struct from another service definition", function()
  local l = r:getProperty("list_d1")
  t.eq(#l, 3); t.eq(l[1], 1.643392e-01); t.eq(l[2], 1.537133e+09); t.eq(l[3], 1.369505e-03)
  r:setProperty("list_d1", { 4.074501e-07, -4.535303e+05, -2.956241e-20 })
  same(r:getProperty("struct3").a1, V("ts1.struct3.to_client", "double"), "struct3 (TestService2.ostruct2)")
  r:setProperty("struct3", { a1 = V("ts1.struct3.to_service", "double") })
end)

t.check("functions", function()
  t.eq(select("#", r:callFunction("func1")), 0, "void function returns nothing")
  r:callFunction("func2", 10, 20.34)
  t.eq(r:callFunction("func3", 2, 3.45), 5.45)
  t.eq(r:callFunction("meaning_of_life"), 42)
  t.raises(function() r:callFunction("func3", 1) end, "takes 2 arguments, got 1")
  t.raises(function() r:callFunction("nosuch") end, "not found")
  t.raises(function() r:callFunction("d1") end, "is not a function")
end)

t.check("remote exceptions are Lua errors with the RR error name and script location", function()
  t.raises(function() r:callFunction("func_errtest") end, "test_ts1.lua:%d+: ")
  t.raises(function() r:callFunction("func_errtest1") end, "RobotRaconteur.DataTypeError")
  t.raises(function() r:callFunction("func_errtest2") end, "com.robotraconteur.testing.TestService1.testexception1")
  t.raises(function() r:getProperty("nosuchproperty") end, "property 'nosuchproperty' not found")
end)

t.check("objrefs", function()
  local o1 = r:getObjRef("o1")
  t.eq(o1:getType(), "com.robotraconteur.testing.TestService1.sub1"); t.eq(o1:isRoot(), false)
  local o2_10, o2_34 = r:getObjRef("o2", 10), r:getObjRef("o2", 34)
  local o3_1 = r:getObjRef("o3", 1)
  local o4_myind = r:getObjRef("o4", "myind")
  local o4_special = r:getObjRef("o4", "ind!@#$%^&*().<>     ")
  local o2_10_o2_1_32 = o2_10:getObjRef("o2_2", 32)
  local ind1 = o2_10_o2_1_32:getObjRef("o3_1", "ind1")
  local ind2 = o2_10_o2_1_32:getObjRef("o3_1", "ind2")
  o1:setProperty("d1", V("ts1.client.o_d1a", "double"))
  o2_10:setProperty("d1", V("ts1.client.o_d1b", "double"))
  o2_34:setProperty("d1", V("ts1.client.o_d1c", "double"))
  o3_1:setProperty("d1", V("ts1.client.o_d1d", "double"))
  o4_myind:setProperty("d1", V("ts1.client.o_d1e", "double"))
  o4_special:setProperty("d1", V("ts1.client.o_d1f", "double"))
  o2_10_o2_1_32:setProperty("data", "Hello world!")
  ind1:setProperty("data2", "Test string 1")
  ind2:setProperty("data2", "Test string 2")
  t.raises(function() o1:close() end, "only the service root")
  t.raises(function() r:getObjRef("o2", 1.5) end, "int32")
end)

-- o6 is a varobject: the server swaps the object behind it, and old references must fail.
t.check("varobject objrefs", function()
  r:callFunction("o6_op", 0)
  local o6_1 = r:getObjRef("o6")
  t.eq(o6_1:getType(), "com.robotraconteur.testing.TestService1.sub1")
  local o6_1_1 = o6_1:getObjRef("o2_1")
  o6_1:setProperty("d1", rr.array("double", {0.0}))
  o6_1_1:setProperty("data", "Hello world!")
  r:callFunction("o6_op", 1)
  t.raises(function() o6_1:setProperty("d1", rr.array("double", {0.0})) end)
  t.raises(function() o6_1_1:setProperty("data", "Hello world!") end)
  local o6_2 = r:getObjRef("o6")
  t.eq(o6_2:getType(), "com.robotraconteur.testing.TestService1.sub2")
  o6_2:setProperty("data", "Hello world!")
  r:callFunction("o6_op", 2)
  t.raises(function() o6_2:setProperty("data", "Hello world!") end)
  -- An object of a type from another service definition, pulled on demand.
  local o6_3 = r:getObjRef("o6")
  t.eq(o6_3:getType(), "com.robotraconteur.testing.TestService2.subobj")
  o6_3:callFunction("add_val", 2)
end)

t.check("events: listeners run in the pump with the event arguments", function()
  rr.sleep(0.4) -- let the events from the earlier func1 and func2 calls arrive first
  local ev1, ev2 = 0, nil
  local id1 = r:addEventListener("ev1", function(...) ev1 = ev1 + 1; t.eq(select("#", ...), 0) end)
  local id2 = r:addEventListener("ev2", function(d, s) ev2 = { d, s.mydat[1] } end)
  r:callFunction("func1")
  r:callFunction("func2", 27.3, 98.23)
  for _ = 1, 100 do
    if ev1 > 0 and ev2 then break end
    rr.sleep(0.02)
  end
  t.eq(ev1, 1, "ev1"); t.truthy(ev2, "ev2 received")
  t.eq(ev2[1], 27.3); t.eq(ev2[2], 98.23)
  t.eq(r:removeEventListener(id1), true); t.eq(r:removeEventListener(id1), false)
  r:removeEventListener(id2)
  r:callFunction("func1")
  rr.sleep(0.4)
  t.eq(ev1, 1, "removed listener not called")
  t.raises(function() r:addEventListener("nosuchevent", print) end, "not found")
  t.raises(function() r:addEventListener("ev1", 5) end, "expected a function")
end)

t.check("event listener errors are raised by the pump", function()
  local id = r:addEventListener("ev1", function() error("listener failed") end)
  r:callFunction("func1")
  local err = t.raises(function() for _ = 1, 100 do rr.sleep(0.02) end end, "listener failed")
  t.truthy(err:find("test_ts1.lua"), "error keeps the listener's location: " .. err)
  r:removeEventListener(id)
end)

t.check("callbacks run while the call that triggers them waits", function()
  local called = {}
  r:setCallback("cb1", function() called.cb1 = true end)
  r:setCallback("cb2", function(d1, d2) called.cb2 = (d1 == 739.2 and d2 == 0.392) end)
  r:setCallback("cb3", function(d1, d2) called.cb3 = true; return d1 + d2 + 3.14 end)
  r:setCallback("cb_meaning_of_life", function() called.cb4 = true; return 42 end)
  r:setCallback("cb_errtest", function() called.cb5 = true; error("This is a test") end)
  r:callFunction("test_callbacks") -- the server checks every result, including cb_errtest's error
  t.eq(called.cb1, true); t.eq(called.cb2, true); t.eq(called.cb3, true)
  t.eq(called.cb4, true); t.eq(called.cb5, true)
  -- A wrong return value fails the server's check, which comes back as the call's error.
  r:setCallback("cb3", function() return 1 end)
  t.raises(function() r:callFunction("test_callbacks") end)
  r:setCallback("cb3", nil)
  t.raises(function() r:callFunction("test_callbacks") end)
  t.raises(function() r:setCallback("nosuchcb", print) end, "not found")
end)

t.check("async calls: ops, handlers and rr.await outside a coroutine", function()
  local got
  local op = r:asyncCallFunction(function(v, err) got = { v, err } end, "func3", 2, 3.45)
  t.eq(op:isDone(), false)
  t.raises(function() op:result() end, "not done")
  t.eq(rr.await(op), 5.45) -- blocks, running the pump
  t.eq(op:isDone(), true); t.eq(op:result(), 5.45)
  t.eq(got[1], 5.45); t.eq(got[2], nil)
  t.eq(rr.await(r:asyncCallFunction(nil, "meaning_of_life")), 42)
  t.eq(rr.await(r:asyncCallFunction(nil, "func1")), nil)
  local e = r:asyncCallFunction(function(v, err) got = { v, err } end, "func_errtest1")
  local msg = t.raises(function() rr.await(e) end, "RobotRaconteur.DataTypeError")
  t.truthy(msg:find("test_ts1.lua:%d+:"), "error raised at the await: " .. msg)
  t.eq(got[1], nil); t.truthy(got[2]:find("DataTypeError"), "handler got the error")
  t.raises(function() e:result() end, "DataTypeError")
  rr.await(r:asyncSetProperty(nil, "d1", 3.456)) -- the server checks the value
  t.raises(function() rr.await(r:asyncSetProperty(nil, "d1", 3.457)) end)
  t.eq(rr.await(r:asyncGetProperty(nil, "d1")), r:getProperty("d1"))
  t.raises(function() r:asyncCallFunction(nil, "func3", 1) end, "takes 2 arguments")
  t.raises(function() r:asyncCallFunction(5, "func3", 1, 2) end, "handler function or nil")
  t.raises(function() rr.await(5) end, "expected a robotraconteur op")
end)

t.check("rr.await in coroutines resumed by rr.step", function()
  local results, finished = {}, 0
  local function task(a, b)
    return coroutine.create(function()
      local x = rr.await(r:asyncCallFunction(nil, "func3", a, b))
      local y = rr.await(r:asyncCallFunction(nil, "func3", x, 1))
      results[#results + 1] = y
      finished = finished + 1
    end)
  end
  local c1, c2 = task(1, 2), task(10, 20)
  assert(coroutine.resume(c1)); assert(coroutine.resume(c2))
  t.eq(coroutine.status(c1), "suspended")
  for _ = 1, 200 do
    if finished == 2 then break end
    rr.step(); rr.sleep(0.005)
  end
  t.eq(finished, 2)
  table.sort(results)
  t.eq(results[1], 4); t.eq(results[2], 31)
  -- An error in an awaiting coroutine is raised by the pump.
  local bad = coroutine.create(function() rr.await(r:asyncCallFunction(nil, "func_errtest")) end)
  assert(coroutine.resume(bad))
  t.raises(function() for _ = 1, 200 do rr.step(); rr.sleep(0.005) end end, "test_ts1.lua")
  t.eq(coroutine.status(bad), "dead")
end)

t.check("asyncConnectService", function()
  local c2 = rr.await(node:asyncConnectService(nil, st.url("RobotRaconteurTestService")))
  t.eq(c2:getType(), "com.robotraconteur.testing.TestService1.testroot")
  c2:close()
end)

local function wait_for(cond, what)
  for _ = 1, 250 do
    if cond() then return end
    rr.sleep(0.02)
  end
  error("timed out waiting for " .. what, 2)
end

t.check("wires: values echoed by the server, listeners, lifespan", function()
  local w1, w2, w3 = r:connectWire("w1"), r:connectWire("w2"), r:connectWire("w3")
  t.eq(w1:direction(), "both")
  w1:setInValueLifespan(10); t.eq(w1:getInValueLifespan(), 10)
  local changed = { 0, 0, 0 }
  w1:addValueChangedListener(function(v) changed[1] = changed[1] + 1; t.truthy(rr.isarray(v)) end)
  w2:addValueChangedListener(function(v) changed[2] = changed[2] + 1; t.truthy(v.mydat) end)
  w3:addValueChangedListener(function() changed[3] = changed[3] + 1 end)
  w1:setOutValue({ 0 })
  w2:setOutValue({ mydat = { 0 } })
  w3:setOutValue(rr.multidim("int32", { 1, 1 }, { 0 }))
  local d1, d2, i3 = V("ts1.client.d1", "double"), V("ts1.client.d2", "double"), st.mvector("ts1.client.i3", "int32")
  w1:setOutValue(d1)
  w2:setOutValue({ mydat = d2 })
  w3:setOutValue(i3)
  wait_for(function()
    local ok1, v1 = w1:tryGetInValue()
    local ok3, v3 = w3:tryGetInValue()
    return ok1 and v1 == d1 and ok3 and #v3:dims() == 2 and v3:numel() == i3:numel() and
      changed[1] > 0 and changed[2] > 0 and changed[3] > 0
  end, "the echoed wire values")
  same(w1:getInValue(), d1, "w1 in")
  same(w2:getInValue().mydat, d2, "w2 in")
  same(w3:getInValue(), i3, "w3 in")
  local ok, out = w1:tryGetOutValue(); t.eq(ok, true); same(out, d1, "w1 out")
  same(w1:getOutValue(), d1, "w1 out")
  t.eq(w1:inValueValid(), true); t.eq(w1:outValueValid(), true)
  -- Listeners are coalesced: never more calls than values sent.
  t.truthy(changed[1] <= 2, "w1 listener calls " .. changed[1])
  w1:setInValueLifespan(0.001)
  rr.sleep(0.02)
  t.raises(function() w1:getInValue() end, "not set")
  t.eq(w1:tryGetInValue(), false)
  w1:close(); w2:close(); w3:close(); w1:close()
  t.raises(function() w1:getInValue() end, "closed")
end)

t.check("pipes: packets echoed by the server, listeners, acks", function()
  local e1, e2, e3 = r:connectPipe("p1"), r:connectPipe("p1", 3432), r:connectPipe("p2")
  t.eq(e2:index(), 3432); t.eq(e1:direction(), "both")
  e1:setRequestPacketAck(true); t.eq(e1:getRequestPacketAck(), true)
  local notified = 0
  e1:addPacketReceivedListener(function() notified = notified + 1 end)
  local n = e1:sendPacket({ 1, 2, 3, 4 })
  t.eq(type(n), "number")
  e1:sendPacket({ 5, 6, 7, 8 })
  local d3 = V("ts1.client.d3", "double")
  e1:sendPacket(d3)
  e2:sendPacket({ 3.21 }); e2:sendPacket({ 4.72 }); e2:sendPacket({ 72.34 })
  e3:sendPacket({ mydat = { 738.29 } }); e3:sendPacket({ mydat = { 89.83 } })
  wait_for(function() return e1:available() == 3 and e2:available() == 3 and e3:available() == 2 end,
    "the echoed packets")
  t.truthy(notified >= 1 and notified <= 3, "e1 listener calls " .. notified)
  same(e1:peekNextPacket(), rr.array("double", { 1, 2, 3, 4 }), "peek")
  same(e1:receivePacket(), rr.array("double", { 1, 2, 3, 4 }), "e1 1")
  same(e1:receivePacket(), rr.array("double", { 5, 6, 7, 8 }), "e1 2")
  same(e1:receivePacket(), d3, "e1 3")
  t.eq(e1:available(), 0)
  t.eq(e1:tryReceivePacket(), false)
  t.raises(function() e1:receivePacket() end, "no packet")
  t.eq(e2:receivePacket()[1], 3.21); t.eq(e2:receivePacket()[1], 4.72); t.eq(e2:receivePacket()[1], 72.34)
  t.eq(e3:receivePacket().mydat[1], 738.29)
  local ok, p = e3:tryReceivePacket(); t.eq(ok, true); t.eq(p.mydat[1], 89.83)
  rr.sleep(0.2)
  r:callFunction("pipe_check_error")
  e1:close(); e2:close(); e3:close()
  t.raises(function() e1:sendPacket({ 1 }) end, "closed")
  t.raises(function() r:connectPipe("nosuchpipe") end, "not found")
  t.raises(function() r:connectWire("p1") end, "not found")
end)

t.check("large arrays stream as rr.array without table conversion", function()
  local n = 1000000 -- 8 MB of doubles
  local big = rr.array("double", n)
  for i = 1, n, 997 do big[i] = i * 0.5 end
  local e = r:connectPipe("p1")
  local w = r:connectWire("w1")
  local t0 = os.clock()
  e:sendPacket(big)
  wait_for(function() return e:available() == 1 end, "the large packet")
  local back = e:receivePacket()
  w:setOutValue(big)
  wait_for(function() local ok, v = w:tryGetInValue(); return ok and v:numel() == n end, "the large wire value")
  local wback = w:getInValue()
  local cpu = os.clock() - t0
  t.truthy(rr.isarray(back) and rr.isarray(wback), "received as rr.array")
  t.truthy(back == big and wback == big, "values")
  io.write(string.format("  8 MB through a pipe and a wire, both ways: %.3f s of CPU in this process\n", cpu))
  t.truthy(cpu < 5, "large array CPU time " .. cpu)
  e:close(); w:close()
end)

t.check("deadlock detection: a callback that triggers itself fails cleanly", function()
  local depth = 0
  for _, cb in ipairs({ "cb2", "cb3", "cb_meaning_of_life", "cb_errtest" }) do r:setCallback(cb, nil) end
  r:setCallback("cb1", function()
    depth = depth + 1
    r:callFunction("test_callbacks") -- calls cb1 again, nesting blocking calls
  end)
  local err = t.raises(function() r:callFunction("test_callbacks") end)
  t.truthy(depth >= 2 and depth <= 9, "nesting stopped at depth " .. depth)
  r:setCallback("cb1", nil)
  t.raises(function() rr.step(); r:callFunction("test_callbacks") end)
  t.eq(r:callFunction("meaning_of_life"), 42, "client still works")
  io.write("  nested depth ", depth, ": ", (err:gsub("\n.*", "")), "\n")
end)

t.check("memories", function()
  local m1 = r:getMemory("m1")
  t.eq(m1:length(), 100); t.eq(m1:type(), "double")
  -- Python: Read(10, buf, 1, 10) into an 11-element buffer.
  local want = V("ts1.client.m1_1a", "double")
  local got = m1:read(11, 10)
  for i = 1, 10 do t.eq(got[i], want[i + 1], "m1 read " .. i) end
  -- Python: Write(20, m1_2, 2, 8).
  local m1_2 = V("ts1.client.m1_2a", "double")
  local part = rr.array("double", 8)
  for i = 1, 8 do part[i] = m1_2[i + 2] end
  m1:write(21, part)
  local all = m1:read(1)
  t.eq(all:numel(), 100)
  local initial = V("ts1.m1.initial", "double")
  for i = 1, 100 do
    local e = (i >= 21 and i <= 28) and m1_2[i - 18] or initial[i]
    t.eq(all[i], e, "m1 element " .. i)
  end
  m1:write(1, { 1.5, 2.5 }) -- a table works for array memories
  t.eq(m1:read(1, 2)[2], 2.5)
  m1:write(1, rr.array("double", { initial[1], initial[2] }))
  t.raises(function() m1:read(0, 1) end, ">= 1")
  t.raises(function() m1:write(1, rr.array("int32", { 1 })) end, "rr.array of double")

  local m2 = r:getMemory("m2")
  local d = m2:dims(); t.eq(#d, 5); t.eq(d[1], 10); t.eq(m2:length(), 100000)
  local block = rr.multidim("double", { 10, 10, 10, 10, 10 })
  for i = 1, block:numel() do block[i] = i * 0.25 end
  m2:write({ 1, 1, 1, 1, 1 }, block)
  local back = m2:read({ 1, 1, 1, 1, 1 }, { 10, 10, 10, 10, 10 })
  t.truthy(back == block, "m2 full read back")
  local sub = m2:read({ 3, 2, 1, 1, 1 }, { 2, 3, 1, 1, 1 })
  t.eq(#sub:dims(), 5)
  -- column-major: element (3,2,1,1,1) is index 3 + (2-1)*10 = 13
  t.eq(sub[1], block[13]); t.eq(sub[2], block[14]); t.eq(sub[3], block[23])
  local small = rr.multidim("double", { 2, 2 }, { -1, -2, -3, -4 })
  m2:write({ 5, 5, 5, 5, 5 }, small)
  local s2 = m2:read({ 5, 5, 5, 5, 5 }, { 2, 2, 1, 1, 1 })
  t.eq(s2[1], -1); t.eq(s2[4], -4)

  local m3 = r:getMemory("m3")
  t.eq(m3:type(), "uint8")
  local img = rr.multidim("uint8", { 1024, 1024 })
  for i = 1, img:numel(), 7 do img[i] = i % 256 end
  m3:write({ 1, 1 }, img)
  t.truthy(m3:read({ 1, 1 }, { 1024, 1024 }) == img, "m3 1 MB read back")
  t.raises(function() m3:read(1) end, "multidimensional")
end)

t.check("subscriptions by URL: default client, wire and pipe subscriptions", function()
  local sub = node:subscribeService(st.url("RobotRaconteurTestService"))
  local c = sub:getDefaultClientWait(10)
  t.eq(c:getType(), "com.robotraconteur.testing.TestService1.testroot")
  t.eq(c:callFunction("meaning_of_life"), 42)
  t.eq(#sub:getConnectedClients(), 1)
  t.eq(sub:getDefaultClient():isRoot(), false)
  local ws = sub:subscribeWire("w1")
  wait_for(function() return ws:activeWireConnectionCount() == 1 end, "the wire subscription")
  ws:setOutValueAll({ 1, 2, 3 }) -- the server echoes it back
  wait_for(function() return (ws:tryGetInValue()) end, "the echoed value")
  t.eq(ws:getInValue()[3], 3)
  local ps = sub:subscribePipe("p1")
  wait_for(function() return ps:activePipeEndpointCount() == 1 end, "the pipe subscription")
  ps:sendPacketAll({ 4, 5 })
  wait_for(function() return ps:available() == 1 end, "the echoed packet")
  t.eq(ps:receivePacket()[2], 5)
  t.eq(ps:tryReceivePacket(), false)
  ws:close(); ps:close()
  sub:close()
  t.raises(function() sub:getDefaultClient() end)
end)

t.check("authentication against RobotRaconteurTestService_auth", function()
  local url = st.url("RobotRaconteurTestService_auth")
  local r1 = node:connectService(url, { username = "testuser1", credentials = { password = "testpass1" } })
  t.eq(r1:callFunction("func3", 2.2, 3.3), 5.5)
  r1:close()
  local r2 = rr.await(node:asyncConnectService(nil, url, { username = "testuser2", credentials = { password = "testpass2" } }))
  t.eq(r2:callFunction("func3", 2.2, 3.3), 5.5)
  r2:close()
  t.raises(function() node:connectService(url, { username = "testuser2", credentials = { password = "testpass1" } }) end)
  t.raises(function() node:connectService(url) end)
  t.raises(function() node:connectService(url, { username = 5 }) end, "username must be a string")
end)

-- Ported from RR core's test/matlab/RobotRaconteurMatlabTest.m (varvalue properties, null).
t.check("varvalue properties and null (MATLAB test port)", function()
  same(r:getProperty("var_num"), V("ts1.var_num.to_client", "int32"), "var_num")
  r:setProperty("var_num", rr.varvalue("int32[]", V("ts1.var_num.to_service", "int32")))
  t.raises(function() r:setProperty("var_num", rr.varvalue("int32[]", { 1 })) end) -- the server checks it
  t.eq(r:getProperty("var_str"), "Hello Client!")
  r:setProperty("var_str", "Hello Server!")
  local vs = r:getProperty("var_struct")
  same(vs.mydat, V("ts1.var_struct.to_client", "double"), "var_struct")
  r:setProperty("var_struct", rr.varvalue("com.robotraconteur.testing.TestService1.teststruct2",
    { mydat = V("ts1.var_struct.to_service", "double") }))
  t.eq(r:getProperty("nulltest"), nil)
  r:setProperty("nulltest", nil)
end)

-- Ported from RR core's test/matlab/RobotRaconteurMatlabEmptyArrayTest.m: empty values must
-- marshal (the server may still reject them, but never with a DataTypeError).
t.check("empty arrays marshal (MATLAB test port)", function()
  for name, v in pairs({ d2 = rr.array("double", 0), i32_2 = rr.array("int32", 0), str1 = "", list_d1 = {} }) do
    local ok, err = pcall(function() r:setProperty(name, v) end)
    t.truthy(ok or not tostring(err):find("DataTypeError"), name .. ": " .. tostring(err))
  end
end)

-- Ported from the TypeScript and Racket client tests (see PROGRESS.md, M5 review).
t.check("broadcast pipe flow control and broadcast wire streaming", function()
  local e = r:connectPipe("broadcastpipe")
  rr.sleep(1)
  -- RR core's clients acknowledge a broadcast packet when it arrives, not when Lua consumes it,
  -- so the broadcaster's backlog (3) does not throttle a client that does not drain its queue
  -- (the TypeScript client acknowledges on consume). Packets queue up until Lua drains them.
  local n = e:available()
  t.truthy(n >= 1, "packets received: " .. n)
  while e:tryReceivePacket() do end
  wait_for(function() return e:available() > 0 end, "packets after consuming the backlog")
  e:close()
  local w = r:connectWire("broadcastwire")
  local changes = 0
  w:addValueChangedListener(function() changes = changes + 1 end)
  wait_for(function() return w:inValueValid() end, "the broadcast wire value")
  local v = w:getInValue(); t.truthy(v >= 0 and v <= 100, "value " .. tostring(v))
  rr.sleep(0.3)
  t.truthy(changes > 0, "the value keeps changing")
  w:close()
end)

t.check("a large property (10 MB) and many concurrent async calls", function()
  local x = r:getProperty("i32_huge")
  t.eq(x:numel(), 2621440); t.eq(x[1], 0); t.eq(x[2621440], 2621439)
  local ops = {}
  for i = 1, 200 do ops[i] = r:asyncGetProperty(nil, i % 2 == 0 and "d1" or "str1") end
  for i = 1, 200 do
    local v = rr.await(ops[i])
    if i % 2 == 0 then t.eq(v, 12.345) else t.eq(type(v), "string") end
  end
end)

t.check("connection failures", function()
  local t0 = os.time()
  t.raises(function() node:connectService("rr+tcp://localhost:1?service=RobotRaconteurTestService") end)
  t.truthy(os.time() - t0 <= 2, "refused quickly")
  t.raises(function() node:connectService(st.url("no_such_service")) end, "ServiceNotFound")
  t.raises(function()
    node:connectService("rr+tcp://localhost:" .. st.port .. "?nodename=wrong_node&service=RobotRaconteurTestService")
  end)
end)

t.check("misuse errors", function()
  local w = r:connectWire("w1")
  t.raises(function() r:connectWire("w1") end) -- RR core reports a bare std::exception here
  w:close()
  t.raises(function() r:connectWire("d1") end, "not found")
  t.raises(function() r:getProperty("func3") end)
  t.raises(function() r:getObjRef("d1") end)
  t.raises(function() r:peekWireInValue("d1") end, "not found")
  t.raises(function() r:addEventListener("d1", print) end)
  t.raises(function() r:setCallback("ev1", print) end)
  local w2 = r:connectWire("w2")
  t.raises(function() w2:getInValue() end, "not set")
  t.raises(function() w2:setOutValue({ mydat = "not numbers" }) end)
  w2:close()
  local p = r:connectPipe("p1")
  t.raises(function() p:sendPacket("text") end)
  p:close()
  -- string objref indexes with characters that need escaping
  for _, ind in ipairs({ "sp ace/slash%?#[]", "caf\195\169 \240\159\152\129" }) do
    t.eq(r:getObjRef("o4", ind):getType(), "com.robotraconteur.testing.TestService1.sub1")
  end
end)

t.check("event listeners: every listener runs, the first error is raised", function()
  rr.sleep(0.3)
  local second = 0
  local a = r:addEventListener("ev1", function() error("first listener failed") end)
  local b = r:addEventListener("ev1", function() second = second + 1 end)
  r:callFunction("func1")
  t.raises(function() for _ = 1, 100 do rr.sleep(0.02) end end, "first listener failed")
  t.eq(second, 1, "the second listener still ran")
  r:removeEventListener(a); r:removeEventListener(b)
  t.eq(r:callFunction("meaning_of_life"), 42)
end)

t.check("newStructure: a struct with every field at its default", function()
  local s = r:newStructure("teststruct1")
  t.eq(s.str2, ""); t.eq(s.dat1:numel(), 0); t.eq(s.struct1, nil)
  t.eq(type(s.vec3), "table"); t.eq(next(s.vec3), nil)
  local s2 = r:newStructure("com.robotraconteur.testing.TestService1.teststruct2")
  t.eq(s2.mydat:type(), "double")
end)

t.check("closing a pipe or wire stops its listeners, even with a notification queued", function()
  local e = r:connectPipe("p1")
  local closed, after = false, 0
  e:addPacketReceivedListener(function() if closed then after = after + 1 end; e:available() end)
  e:sendPacket({ 1 })
  os.execute("sleep 0.5") -- the echo arrives and its notification is queued; nothing pumps
  closed = true
  e:close() -- waits with the pump: the queued notification must not call the listener
  t.eq(after, 0)
  local w = r:connectWire("w1")
  local wafter = 0
  w:addValueChangedListener(function() if closed then wafter = wafter + 1 end; w:getInValue() end)
  closed = false
  w:setOutValue({ 1 })
  os.execute("sleep 0.5")
  closed = true
  w:close()
  t.eq(wafter, 0)
  rr.sleep(0.1)
end)

t.check("close", function()
  r:close(); r:close()
  t.raises(function() r:getProperty("d1") end, "client has been closed")
end)

node:shutdown()
st.stop_server()
t.finish()
