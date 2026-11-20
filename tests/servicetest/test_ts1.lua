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

t.check("close", function()
  r:close(); r:close()
  t.raises(function() r:getProperty("d1") end, "client has been closed")
end)

node:shutdown()
st.stop_server()
t.finish()
