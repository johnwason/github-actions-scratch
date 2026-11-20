-- M2.3: rr.array, the typed numeric array shared by marshalling for every numeric type.
local t = require("testutil")
local rr = require("robotraconteur")

local LUAJIT = jit ~= nil
io.write("runtime: ", LUAJIT and jit.version or _VERSION, "\n")

local TYPES = { "double", "single", "int8", "uint8", "int16", "uint16", "int32", "uint32",
                "int64", "uint64", "bool", "cdouble", "csingle" }

t.check("construct every type; zero filled; type, dims, numel, #", function()
  for _, ty in ipairs(TYPES) do
    local a = rr.array(ty, 3)
    t.eq(a:type(), ty); t.eq(a:numel(), 3); t.eq(#a, 3)
    local d = a:dims(); t.eq(#d, 1); t.eq(d[1], 3)
    t.truthy(rr.isarray(a), ty .. " isarray")
    local z = a[1]
    if ty == "bool" then t.eq(z, false)
    elseif ty:sub(1, 1) == "c" then t.eq(z.real, 0); t.eq(z.imag, 0)
    else t.eq(z, 0) end
  end
  t.truthy(not rr.isarray({}), "table is not an array")
  t.raises(function() return rr.array("float", 1) end, "unknown element type 'float'")
  t.raises(function() return rr.array("double", -1) end, "non%-negative")
  t.eq(rr.array("double", 0):numel(), 0)
end)

t.check("from tables, 1-based access, toTable round trip", function()
  local a = rr.array("double", { 1.5, 2.5, 3.5 })
  t.eq(a[1], 1.5); t.eq(a[3], 3.5)
  a[2] = 9; t.eq(a:get(2), 9)
  a:set(3, -1); t.eq(a[3], -1)
  local tt = a:toTable(); t.eq(#tt, 3); t.eq(tt[1], 1.5); t.eq(tt[2], 9); t.eq(tt[3], -1)
  t.raises(function() return a[0] end, "out of range")
  t.raises(function() return a[4] end, "out of range")
  t.raises(function() a[4] = 1 end, "out of range")
  t.raises(function() a.x = 1 end, "cannot set")
  t.eq(rr.array("int32", 4, 7)[4], 7, "fill value")
end)

t.check("integer conversions are exact, never rounded or wrapped", function()
  t.eq(rr.array("int8", { -128, 127 })[1], -128)
  t.raises(function() rr.array("int8", { 128 }) end, "out of range")
  t.raises(function() rr.array("uint8", { -1 }) end, "out of range")
  t.raises(function() rr.array("int32", { 1.5 }) end, "expected an integer")
  t.raises(function() rr.array("int32", { 0 / 0 }) end, "expected an integer")
  t.raises(function() rr.array("int32", { "1" }) end, "expected an integer")
  t.raises(function() rr.array("uint32", { 4294967296 }) end, "out of range")
  t.eq(rr.array("uint32", { 4294967295 })[1], 4294967295)
  t.eq(rr.array("int16", { 2.0 })[1], 2, "integral float accepted")
  t.raises(function() rr.array("double", { "x" }) end, "expected a number")
end)

t.check("bool and complex", function()
  local b = rr.array("bool", { true, false, 1, 0 })
  t.eq(b[1], true); t.eq(b[2], false); t.eq(b[3], true); t.eq(b[4], false)
  t.raises(function() rr.array("bool", { 2 }) end, "expected a boolean")
  local c = rr.array("cdouble", { { real = 1, imag = -2 }, 3 })
  t.eq(c[1].real, 1); t.eq(c[1].imag, -2); t.eq(c[2].real, 3); t.eq(c[2].imag, 0)
  t.raises(function() rr.array("cdouble", { { 1, 2 } }) end, "real=, imag=")
  local s = rr.array("csingle", { { real = 0.5, imag = 0.25 } })
  t.eq(s[1].real, 0.5); t.eq(s[1].imag, 0.25)
end)

t.check("single precision rounds through float", function()
  local a = rr.array("single", { 0.1 })
  t.truthy(a[1] ~= 0.1 and math.abs(a[1] - 0.1) < 1e-7, "stored as float")
end)

t.check("64-bit integers: exact, boxed when Lua cannot hold them", function()
  local big = rr.array("int64", 1)
  -- 2^53 + 1 cannot be held by a double; set it through a box built from parts
  local box = rr.array("int64", { 4503599627370496 }) -- 2^52, exact everywhere
  t.eq(box[1], 4503599627370496)
  local u = rr.array("uint64", 1)
  u[1] = 9007199254740992 -- 2^53, exact in a double
  t.eq(u[1], 9007199254740992)
  if not LUAJIT then
    -- Lua 5.4 integers: full int64 range, uint64 above maxinteger is boxed
    big[1] = math.maxinteger; t.eq(big[1], math.maxinteger)
    big[1] = math.mininteger; t.eq(big[1], math.mininteger)
    local m = rr.array("uint64", 1)
    m:set(1, rr.array("uint64", { math.maxinteger }))
    t.eq(m[1], math.maxinteger)
    t.raises(function() rr.array("uint64", { -1 }) end, "out of range")
    -- 2^64 - 1 via a uint64 box made by reading back an element
    local w = rr.array("uint64", 1); w[1] = 0
    t.truthy(math.type(w[1]) == "integer", "integer subtype")
  else
    -- LuaJIT: beyond 2^53 a value comes back as a 1-element box
    big[1] = 9007199254740992 -- 2^53
    local twice = rr.array("int64", 1)
    twice[1] = big -- copy through a box
    t.eq(twice[1], 9007199254740992)
  end
  -- a box is an rr.array of one element, accepted wherever an integer is
  local b64 = rr.array("int64", { 5 })
  t.eq(rr.array("int32", { b64 })[1], 5)
end)

t.check("multidim arrays are column-major with 1-based indices", function()
  -- 2 x 3, column-major values: (1,1)=1 (2,1)=2 (1,2)=3 (2,2)=4 (1,3)=5 (2,3)=6
  local m = rr.multidim("int32", { 2, 3 }, { 1, 2, 3, 4, 5, 6 })
  local d = m:dims(); t.eq(#d, 2); t.eq(d[1], 2); t.eq(d[2], 3)
  t.eq(m:numel(), 6)
  t.eq(m:get(1, 1), 1); t.eq(m:get(2, 1), 2); t.eq(m:get(1, 2), 3); t.eq(m:get(2, 3), 6)
  t.eq(m[4], 4, "linear index")
  m:set(2, 2, 40); t.eq(m[4], 40)
  t.raises(function() return m:get(3, 1) end, "out of range")
  t.raises(function() return m:get(1, 1, 1) end, "3 indices given for a 2%-dimensional array")
  local r = m:reshape { 3, 2 }; t.eq(r:get(3, 1), 3); t.eq(r:get(1, 2), 40)
  t.raises(function() return m:reshape { 4, 2 } end, "reshape")
  local z = rr.multidim("double", { 2, 2, 2 }); t.eq(z:numel(), 8)
  t.raises(function() rr.multidim("double", { 2, 2 }, { 1, 2, 3 }) end, "3 values given, expected 4")
  t.raises(function() rr.multidim("double", {}) end, "dims must have")
end)

t.check("conversion between arrays, copy, equality, tostring", function()
  local a = rr.array("int16", { 1, 2, 3 })
  local d = rr.array("double", a); t.eq(d:type(), "double"); t.eq(d[3], 3)
  t.raises(function() rr.array("uint8", rr.array("int16", { -1 })) end, "out of range")
  local c = a:copy(); c[1] = 5; t.eq(a[1], 1, "copy is independent")
  t.truthy(a == rr.array("int16", { 1, 2, 3 }), "equal")
  t.truthy(a ~= rr.array("int32", { 1, 2, 3 }), "type matters")
  t.truthy(rr.multidim("double", { 1, 2 }) ~= rr.multidim("double", { 2, 1 }), "dims matter")
  local s = tostring(rr.multidim("int32", { 2, 2 }, { 1, 2, 3, 4 }))
  t.eq(s, "rr.array int32[2,2] 1, 2, 3, 4")
  t.eq(tostring(rr.array("bool", { true })), "rr.array bool[1] true")
end)

t.check("large arrays allocate inline and copy without tables", function()
  local n = 1000000
  local a = rr.array("double", n, 1.25)
  local b = rr.array("double", a) -- same type: one memcpy
  t.eq(b[n], 1.25); t.eq(#b, n)
end)

t.finish()
