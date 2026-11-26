-- M2.3: marshalling of every value type, through the real RR wire format (pack, write a
-- message, read it back, unpack), with types from a service definition at runtime.
local t = require("testutil")
local rr = require("robotraconteur")

local LUAJIT = jit ~= nil
io.write("runtime: ", LUAJIT and jit.version or _VERSION, "\n")

local DEF = [[
service experimental.lua_marshal

stdver 0.10

enum Color
    red = 1,
    green,
    blue = 10
end

struct Inner
    field string name
    field double[] values
end

struct Outer
    field int32 id
    field Inner inner
    field Inner{list} inners
    field varvalue extra
    field double[2,2] mat
    field Color color
end

pod Point
    field double x
    field double y
end

pod Sample
    field uint32 seq
    field Point p
    field double[3] v
    field int16[4-] var
    field double[2,2] m
end

namedarray Vec2
    field double a
    field double b
end

namedarray Pose2
    field Vec2 pos
    field double theta
    field double[2] extra
end

object Dummy
    property int32 x
end
]]

local function rt(ty, v) return rr._roundtrip({ DEF }, ty, v) end

t.check("numeric scalars of every type", function()
  t.eq(rt("double", 1.5), 1.5)
  t.eq(rt("single", 0.5), 0.5)
  t.eq(rt("int8", -128), -128); t.eq(rt("uint8", 255), 255)
  t.eq(rt("int16", -32768), -32768); t.eq(rt("uint16", 65535), 65535)
  t.eq(rt("int32", -2147483648), -2147483648); t.eq(rt("uint32", 4294967295), 4294967295)
  t.eq(rt("int64", -9007199254740992), -9007199254740992)
  t.eq(rt("uint64", 9007199254740992), 9007199254740992)
  t.eq(rt("bool", true), true); t.eq(rt("bool", false), false)
  local c = rt("cdouble", { real = 1, imag = -2 }); t.eq(c.real, 1); t.eq(c.imag, -2)
  local s = rt("csingle", 3); t.eq(s.real, 3); t.eq(s.imag, 0)
  if not LUAJIT then
    t.eq(rt("int64", math.mininteger), math.mininteger)
    t.eq(rt("int64", math.maxinteger), math.maxinteger)
  end
end)

t.check("scalar conversions are exact", function()
  t.raises(function() rt("int8", 128) end, "out of range")
  t.raises(function() rt("uint32", -1) end, "out of range")
  t.raises(function() rt("int32", 1.5) end, "expected an integer")
  t.raises(function() rt("double", "x") end, "expected a number")
  t.raises(function() rt("int32", nil) end, "must not be nil")
  t.raises(function() rt("double", { 1, 2 }) end, "expected a number")
  t.raises(function() rt("double", rr.array("double", { 1, 2 })) end, "expected a scalar")
  t.eq(rt("double", rr.array("double", { 4 })), 4, "1-element array accepted as a scalar")
end)

t.check("strings", function()
  t.eq(rt("string", "hello"), "hello")
  t.eq(rt("string", ""), "")
  t.eq(rt("string", "a\0b"), "a\0b", "embedded zero")
  t.eq(rt("string", "caf\195\169"), "caf\195\169", "UTF-8")
  t.raises(function() rt("string", 5) end, "expected a string")
  t.raises(function() rt("string", nil) end, "must not be nil")
end)

t.check("numeric arrays: tables or rr.array in, rr.array out", function()
  local a = rt("double[]", { 1, 2, 3 })
  t.truthy(rr.isarray(a)); t.eq(a:type(), "double"); t.eq(#a, 3); t.eq(a[3], 3)
  local b = rt("int32[]", rr.array("int16", { -1, 5 }))
  t.eq(b:type(), "int32"); t.eq(b[1], -1)
  t.eq(#rt("double[]", {}), 0, "empty")
  t.eq(#rt("uint8[3]", { 1, 2, 3 }), 3)
  t.raises(function() rt("uint8[3]", { 1, 2 }) end, "array length 2 does not match")
  t.eq(#rt("uint8[3-]", { 1, 2 }), 2)
  t.raises(function() rt("uint8[3-]", { 1, 2, 3, 4 }) end, "array length 4")
  t.raises(function() rt("int8[]", { 1, 300 }) end, "element 2: value out of range")
  local cplx = rt("cdouble[]", { { real = 1, imag = 2 } }); t.eq(cplx[1].imag, 2)
  local bl = rt("bool[]", { true, false }); t.eq(bl:type(), "bool"); t.eq(bl[1], true)
  t.raises(function() rt("double[]", nil) end, "must not be nil")
end)

t.check("multidimarrays keep dims (column-major)", function()
  local m = rt("double[*]", rr.multidim("double", { 2, 3 }, { 1, 2, 3, 4, 5, 6 }))
  local d = m:dims(); t.eq(#d, 2); t.eq(d[1], 2); t.eq(d[2], 3); t.eq(m:get(2, 3), 6)
  local f = rt("int32[2,2]", rr.multidim("int32", { 2, 2 }, { 1, 2, 3, 4 }))
  t.eq(f:get(1, 2), 3)
  t.raises(function() rt("int32[2,2]", rr.multidim("int32", { 2, 3 })) end, "dimensions do not match")
  local one = rt("double[*]", { 1, 2 }); t.eq(one:dims()[1], 2, "a flat table is 1-D")
end)

t.check("structs: nested, null fields absent, unknown fields rejected", function()
  local v = rt("Outer", {
    id = 7,
    inner = { name = "a", values = { 1, 2 } },
    inners = { { name = "b", values = {} }, rr.null },
    mat = rr.multidim("double", { 2, 2 }, { 1, 2, 3, 4 }),
    color = "blue",
  })
  t.eq(v.id, 7); t.eq(v.inner.name, "a"); t.eq(v.inner.values[2], 2)
  t.eq(#v.inners, 2); t.eq(v.inners[1].name, "b"); t.eq(v.inners[2], rr.null, "null list entry")
  t.eq(v.extra, nil, "null varvalue field is absent")
  t.eq(v.mat:get(1, 2), 3)
  t.eq(v.color, 10, "enum by name, received as integer")
  t.raises(function() rt("Outer", { id = 1, mat = rr.multidim("double", { 2, 2 }), color = 1, idd = 2 }) end,
    "unknown field 'idd'")
  t.raises(function() rt("Outer", { mat = rr.multidim("double", { 2, 2 }), color = 1 }) end,
    "must not be nil for field \"id\"")
  t.raises(function() rt("Outer", { id = 1, inner = { name = 3 }, mat = rr.multidim("double", { 2, 2 }), color = 1 }) end,
    "expected a string.*for field \"inner.name\"")
  t.eq(rt("Inner", nil), nil, "a null struct")
end)

t.check("enums", function()
  t.eq(rt("Color", "green"), 2)
  t.eq(rt("Color", 1), 1)
  t.raises(function() rt("Color", "purple") end, "'purple' is not a value of enum")
  t.raises(function() rt("Color", nil) end, "must not be nil")
end)

t.check("lists and maps", function()
  local l = rt("int32{list}", { 1, 2, 3 }); t.eq(#l, 3); t.eq(l[3], 3)
  t.raises(function() rt("int32{list}", { 1, nil, 3 }) end, "sequence")
  t.raises(function() rt("int32{list}", { a = 1 }) end, "sequence")
  local mi = rt("string{int32}", { [0] = "zero", [5] = "five", [-2] = "neg" })
  t.eq(mi[0], "zero"); t.eq(mi[5], "five"); t.eq(mi[-2], "neg")
  t.raises(function() rt("string{int32}", { [1.5] = "x" }) end, "int32 integers")
  local ms = rt("double{string}", { a = 1, b = 2 }); t.eq(ms.a, 1); t.eq(ms.b, 2)
  t.raises(function() rt("double{string}", { [1] = 1 }) end, "must be strings")
  local nested = rt("Inner{string}", { k = { name = "n", values = { 9 } } })
  t.eq(nested.k.values[1], 9)
  local empty = rt("int32{list}", {}); t.eq(#empty, 0)
  t.eq(rt("int32{list}", nil), nil, "null container")
end)

t.check("pods: scalar and arrays, fixed/var/multidim fields, nested pods", function()
  local s = rt("Sample", { seq = 3, p = { x = 1, y = 2 }, v = { 1, 2, 3 }, var = { 5 },
                           m = rr.multidim("double", { 2, 2 }, { 1, 2, 3, 4 }) })
  t.eq(s.seq, 3); t.eq(s.p.y, 2); t.eq(s.v[3], 3); t.eq(#s.var, 1); t.eq(s.var[1], 5)
  t.eq(s.m:get(2, 1), 2); t.eq(#s.m:dims(), 2, "multidim pod field keeps dims")
  local arr = rt("Point[]", { { x = 1, y = 2 }, { x = 3, y = 4 } })
  t.eq(#arr, 2); t.eq(arr[2].x, 3)
  t.eq(#rt("Point[2]", { { x = 1, y = 2 }, { x = 3, y = 4 } }), 2)
  t.raises(function() rt("Point[2]", { { x = 1, y = 2 } }) end, "array length 1")
  t.raises(function() rt("Point", { x = 1 }) end, "must not be nil for field \"y\"")
  t.raises(function() rt("Point", nil) end, "must not be nil")
  t.raises(function() rt("Sample", { seq = 1, p = { x = 1, y = 2 }, v = { 1, 2 }, var = {}, m = { 1, 2, 3, 4 } }) end,
    "for field \"v\"")
end)

t.check("namedarrays: tables of fields, nested; rr.array accepted without tables", function()
  local v = rt("Vec2", { a = 1, b = 2 }); t.eq(v.a, 1); t.eq(v.b, 2)
  local p = rt("Pose2", { pos = { a = 1, b = 2 }, theta = 0.5, extra = { 7, 8 } })
  t.eq(p.pos.b, 2); t.eq(p.theta, 0.5); t.eq(p.extra[2], 8)
  local arr = rt("Vec2[]", { { a = 1, b = 2 }, { a = 3, b = 4 } }); t.eq(#arr, 2); t.eq(arr[2].a, 3)
  local flat = rt("Vec2[]", rr.array("double", { 1, 2, 3, 4, 5, 6 }))
  t.eq(#flat, 3); t.eq(flat[3].b, 6)
  t.raises(function() rt("Vec2[]", rr.array("double", { 1, 2, 3 })) end, "multiple of 2")
  t.raises(function() rt("Vec2", { a = 1 }) end, "must not be nil")
end)

t.check("varvalue: tagged, detected, and received numbers as arrays", function()
  local n = rt("varvalue", rr.varvalue("int32", 5))
  t.truthy(rr.isarray(n), "received numbers are arrays"); t.eq(n:type(), "int32"); t.eq(n[1], 5)
  t.eq(rt("varvalue", "text"), "text", "string detected")
  local a = rt("varvalue", rr.array("uint16", { 1, 2 })); t.eq(a:type(), "uint16")
  local m = rt("varvalue", rr.multidim("double", { 2, 2 })); t.eq(#m:dims(), 2)
  local s = rt("varvalue", rr.varvalue("experimental.lua_marshal.Inner", { name = "x", values = { 1 } }))
  t.eq(s.name, "x")
  local pod = rt("varvalue", rr.varvalue("experimental.lua_marshal.Point", { { x = 1, y = 2 } }))
  t.eq(pod[1].y, 2)
  local lst = rt("varvalue", rr.varvalue("varvalue{list}", { "a", rr.varvalue("double", 1) }))
  t.eq(lst[1], "a"); t.eq(lst[2][1], 1)
  t.eq(rt("varvalue", nil), nil)
  t.raises(function() rt("varvalue", 5) end, "needs rr.varvalue")
  t.raises(function() rt("varvalue", { 1 }) end, "needs rr.varvalue")
  t.raises(function() rt("varvalue", rr.varvalue("notatype[", 1)) end, "invalid rr.varvalue type")
  t.eq(tostring(rr.null), "rr.null")
end)

t.check("multidim arrays of pods and namedarrays: column-major lists with dims", function()
  local pts = { dims = { 2, 3 } }
  for i = 1, 6 do pts[i] = { x = i, y = -i } end
  local back = rt("Point[*]", pts)
  t.eq(#back, 6); t.eq(back.dims[1], 2); t.eq(back.dims[2], 3); t.eq(back[5].y, -5)
  t.eq(#rt("Point[2,3]", pts), 6)
  t.raises(function() rt("Point[3,2]", pts) end, "dims do not match")
  local vs = { dims = { 1, 2 }, { a = 1, b = 2 }, { a = 3, b = 4 } }
  local vb = rt("Vec2[*]", vs)
  t.eq(vb.dims[2], 2); t.eq(vb[2].b, 4)
  t.raises(function() rt("Vec2[*]", { { a = 1, b = 2 } }) end, "needs a dims field")
  t.raises(function() rt("Vec2[*]", { dims = { 2, 2 }, { a = 1, b = 2 } }) end, "expected 4 entries")
  t.raises(function() rt("Vec2[*]", { dims = { 1.5 }, { a = 1, b = 2 } }) end, "non%-negative integers")
  local empty = rt("Point[*]", { dims = { 0, 4 } })
  t.eq(#empty, 0); t.eq(empty.dims[2], 4)
  -- Inside a varvalue, the wire type carries the dims.
  local m = rt("varvalue", rr.varvalue("experimental.lua_marshal.Point[*]", pts))
  t.eq(m.dims[2], 3); t.eq(m[6].x, 6)
end)

t.finish()
