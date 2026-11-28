-- Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
--
-- Lua port of RR's TestService3 sequence generator and the pod/namedarray fill and verify
-- helpers (test/cpp/service/ServiceTest2.cpp, test/python/RobotRaconteurTest/ServiceTest2.py),
-- in this binding's representation: pods and namedarrays are tables of fields, arrays of them
-- are lists, multidimensional arrays of them are lists in column-major order with a `dims` field.
--
-- M.init(vector) must be called first with a function(name, type) returning test vectors.

local rr = require("robotraconteur")

local M = {}
local double_constants, uint32_constants

function M.init(vector)
  local d = vector("seq.double_constants", "double")
  local u = vector("seq.uint32_constants", "uint32")
  double_constants, uint32_constants = {}, {}
  for i = 1, d:numel() do double_constants[i] = d[i] end
  for i = 1, u:numel() do uint32_constants[i] = u[i] end
end

local Gen = {}
Gen.__index = Gen

local function gen(seed)
  return setmetatable({ counter = seed % 7823, step = seed % 17 + 7 }, Gen)
end
M.gen = gen

function Gen:double()
  self.counter = self.counter + self.step
  return double_constants[self.counter % #double_constants + 1]
end

function Gen:uint32()
  self.counter = self.counter + self.step
  return uint32_constants[self.counter % #uint32_constants + 1]
end

function Gen:int8()
  self.counter = self.counter + self.step
  return self.counter % 0xFF - 128
end

local failures

local function expect(cond, what)
  if not cond then failures[#failures + 1] = what end
end

-- Run verify(...) and raise one error listing every mismatch.
function M.verify(fn, ...)
  failures = {}
  fn(...)
  local f = failures
  failures = nil
  if #f > 0 then
    error(#f .. " mismatches, first: " .. table.concat(f, "; ", 1, math.min(#f, 5)), 2)
  end
end

local function numel(a)
  if rr.isarray(a) then return a:numel() end
  return #a
end

local function doubles(g, n)
  local t = {}
  for i = 1, n do t[i] = g:double() end
  return rr.array("double", t)
end

local function int8s(g, n)
  local t = {}
  for i = 1, n do t[i] = g:int8() end
  return rr.array("int8", t)
end

local function verify_seq(g, a, n, what, next)
  expect(a ~= nil and numel(a) == n, what .. " length " .. tostring(a and numel(a)) .. " ~= " .. n)
  if a == nil then return end
  for i = 1, math.min(numel(a), n) do
    local want = next(g)
    expect(a[i] == want, what .. "[" .. i .. "] " .. tostring(a[i]) .. " ~= " .. tostring(want))
  end
end

local function verify_dims(v, dims, what)
  local d = v and v.dims
  local ok = type(d) == "table" and #d == #dims
  for i = 1, #dims do ok = ok and d[i] == dims[i] end
  expect(ok, what .. " dims")
end

-- transform: rotation (q0..q3), translation (x, y, z), 7 doubles.
function M.fill_transform(seed)
  local g = gen(seed)
  local r = { q0 = g:double(), q1 = g:double(), q2 = g:double(), q3 = g:double() }
  local t = { x = g:double(), y = g:double(), z = g:double() }
  return { rotation = r, translation = t }
end

function M.verify_transform(v, seed, what)
  what = what or "transform"
  local g = gen(seed)
  if type(v) ~= "table" or type(v.rotation) ~= "table" or type(v.translation) ~= "table" then
    expect(false, what .. " is not a transform")
    return
  end
  for _, k in ipairs({ "q0", "q1", "q2", "q3" }) do
    local want = g:double()
    expect(v.rotation[k] == want, what .. ".rotation." .. k)
  end
  for _, k in ipairs({ "x", "y", "z" }) do
    local want = g:double()
    expect(v.translation[k] == want, what .. ".translation." .. k)
  end
end

local function fill_list(n, seed, fill)
  local g = gen(seed)
  local t = {}
  for i = 1, n do t[i] = fill(g:uint32()) end
  return t
end

local function verify_list(v, n, seed, verify, what)
  expect(type(v) == "table" and #v == n, what .. " length " .. tostring(type(v) == "table" and #v) .. " ~= " .. n)
  if type(v) ~= "table" then return end
  local g = gen(seed)
  for i = 1, math.min(#v, n) do verify(v[i], g:uint32(), what .. "[" .. i .. "]") end
end

local function fill_md(m, n, seed, fill)
  local t = fill_list(m * n, seed, fill)
  t.dims = { m, n }
  return t
end

local function verify_md(v, m, n, seed, verify, what)
  verify_dims(v, { m, n }, what)
  verify_list(v, m * n, seed, verify, what)
end

function M.fill_transform_array(n, seed) return fill_list(n, seed, M.fill_transform) end
function M.verify_transform_array(v, n, seed, what)
  verify_list(v, n, seed, M.verify_transform, what or "transform[]")
end
function M.fill_transform_multidimarray(m, n, seed) return fill_md(m, n, seed, M.fill_transform) end
function M.verify_transform_multidimarray(v, m, n, seed, what)
  verify_md(v, m, n, seed, M.verify_transform, what or "transform[*]")
end

function M.fill_testpod2(seed)
  local g = gen(seed)
  local s = { i1 = g:int8(), i2 = int8s(g, 15) }
  s.i3 = int8s(g, g:uint32() % 15)
  return s
end

function M.verify_testpod2(v, seed, what)
  what = what or "testpod2"
  if type(v) ~= "table" then expect(false, what .. " is not a pod") return end
  local g = gen(seed)
  local want = g:int8()
  expect(v.i1 == want, what .. ".i1")
  verify_seq(g, v.i2, 15, what .. ".i2", Gen.int8)
  local n = g:uint32() % 15
  verify_seq(g, v.i3, n, what .. ".i3", Gen.int8)
end

function M.fill_testpod1(seed)
  local g = gen(seed)
  local s = { d1 = g:double(), d2 = doubles(g, 6) }
  s.d3 = doubles(g, g:uint32() % 6)
  s.d4 = doubles(g, 9):reshape({ 3, 3 })
  s.s1 = M.fill_testpod2(g:uint32())
  s.s2 = {}
  for i = 1, 8 do s.s2[i] = M.fill_testpod2(g:uint32()) end
  s.s3 = {}
  for i = 1, g:uint32() % 9 do s.s3[i] = M.fill_testpod2(g:uint32()) end
  s.s4 = { dims = { 2, 4 } }
  for i = 1, 8 do s.s4[i] = M.fill_testpod2(g:uint32()) end
  s.t1 = M.fill_transform(g:uint32())
  s.t2 = {}
  for i = 1, 4 do s.t2[i] = M.fill_transform(g:uint32()) end
  s.t3 = {}
  for i = 1, g:uint32() % 15 do s.t3[i] = M.fill_transform(g:uint32()) end
  s.t4 = { dims = { 2, 4 } }
  for i = 1, 8 do s.t4[i] = M.fill_transform(g:uint32()) end
  return s
end

function M.verify_testpod1(v, seed, what)
  what = what or "testpod1"
  if type(v) ~= "table" then expect(false, what .. " is not a pod") return end
  local g = gen(seed)
  local want = g:double()
  expect(v.d1 == want, what .. ".d1")
  verify_seq(g, v.d2, 6, what .. ".d2", Gen.double)
  local n = g:uint32() % 6
  verify_seq(g, v.d3, n, what .. ".d3", Gen.double)
  verify_seq(g, v.d4, 9, what .. ".d4", Gen.double)
  M.verify_testpod2(v.s1, g:uint32(), what .. ".s1")
  for i = 1, 8 do M.verify_testpod2(v.s2 and v.s2[i], g:uint32(), what .. ".s2[" .. i .. "]") end
  n = g:uint32() % 9
  expect(v.s3 and #v.s3 == n, what .. ".s3 length")
  for i = 1, n do M.verify_testpod2(v.s3 and v.s3[i], g:uint32(), what .. ".s3[" .. i .. "]") end
  verify_dims(v.s4, { 2, 4 }, what .. ".s4")
  for i = 1, 8 do M.verify_testpod2(v.s4 and v.s4[i], g:uint32(), what .. ".s4[" .. i .. "]") end
  M.verify_transform(v.t1, g:uint32(), what .. ".t1")
  for i = 1, 4 do M.verify_transform(v.t2 and v.t2[i], g:uint32(), what .. ".t2[" .. i .. "]") end
  n = g:uint32() % 15
  expect(v.t3 and #v.t3 == n, what .. ".t3 length")
  for i = 1, n do M.verify_transform(v.t3 and v.t3[i], g:uint32(), what .. ".t3[" .. i .. "]") end
  verify_dims(v.t4, { 2, 4 }, what .. ".t4")
  for i = 1, 8 do M.verify_transform(v.t4 and v.t4[i], g:uint32(), what .. ".t4[" .. i .. "]") end
end

function M.fill_testpod1_array(n, seed) return fill_list(n, seed, M.fill_testpod1) end
function M.verify_testpod1_array(v, n, seed, what)
  verify_list(v, n, seed, M.verify_testpod1, what or "testpod1[]")
end
function M.fill_testpod1_multidimarray(m, n, seed) return fill_md(m, n, seed, M.fill_testpod1) end
function M.verify_testpod1_multidimarray(v, m, n, seed, what)
  verify_md(v, m, n, seed, M.verify_testpod1, what or "testpod1[*]")
end

local TS3 = "com.robotraconteur.testing.TestService3."

-- teststruct3, filled like ServiceTest2_fill_teststruct3 (varvalue fields tagged with rr.varvalue).
function M.fill_teststruct3(seed)
  local g = gen(seed)
  local o = {}
  o.s1 = M.fill_testpod1(g:uint32())
  local s2_seed = g:uint32()
  o.s2 = M.fill_testpod1_array(s2_seed % 17, s2_seed)
  o.s3 = M.fill_testpod1_array(11, g:uint32())
  local s4_seed = g:uint32()
  o.s4 = M.fill_testpod1_array(s4_seed % 16, s4_seed)
  o.s5 = M.fill_testpod1_multidimarray(3, 3, g:uint32())
  local s6_seed = g:uint32()
  o.s6 = M.fill_testpod1_multidimarray(s6_seed % 6, s6_seed % 3, s6_seed)
  o.s7 = { M.fill_testpod1(g:uint32()) }
  o.s8 = { M.fill_testpod1_array(2, g:uint32()), M.fill_testpod1_array(4, g:uint32()) }
  o.s9 = { M.fill_testpod1_multidimarray(2, 3, g:uint32()), M.fill_testpod1_multidimarray(4, 5, g:uint32()) }
  o.s10 = rr.varvalue(TS3 .. "testpod1[]", { M.fill_testpod1(g:uint32()) })
  o.s11 = rr.varvalue(TS3 .. "testpod1[]", M.fill_testpod1_array(3, g:uint32()))
  o.s12 = rr.varvalue(TS3 .. "testpod1[*]", M.fill_testpod1_multidimarray(2, 2, g:uint32()))
  o.s13 = rr.varvalue(TS3 .. "testpod1[]{list}", { { M.fill_testpod1(g:uint32()) } })
  o.s14 = rr.varvalue(TS3 .. "testpod1[]{list}",
    { M.fill_testpod1_array(3, g:uint32()), M.fill_testpod1_array(5, g:uint32()) })
  o.s15 = rr.varvalue(TS3 .. "testpod1[*]{list}",
    { M.fill_testpod1_multidimarray(7, 2, g:uint32()), M.fill_testpod1_multidimarray(5, 1, g:uint32()) })
  o.t1 = M.fill_transform(g:uint32())
  o.t2 = M.fill_transform_array(4, g:uint32())
  o.t3 = M.fill_transform_multidimarray(2, 4, g:uint32())
  o.t4 = rr.varvalue(TS3 .. "transform[]", M.fill_transform_array(10, g:uint32()))
  o.t5 = rr.varvalue(TS3 .. "transform[*]", M.fill_transform_multidimarray(6, 5, g:uint32()))
  o.t6 = { M.fill_transform(g:uint32()) }
  o.t7 = { M.fill_transform_array(4, g:uint32()), M.fill_transform_array(4, g:uint32()) }
  o.t8 = { M.fill_transform_multidimarray(2, 4, g:uint32()), M.fill_transform_multidimarray(2, 4, g:uint32()) }
  o.t9 = rr.varvalue(TS3 .. "transform[]{list}", { { M.fill_transform(g:uint32()) } })
  o.t10 = rr.varvalue(TS3 .. "transform[]{list}",
    { M.fill_transform_array(3, g:uint32()), M.fill_transform_array(5, g:uint32()) })
  o.t11 = rr.varvalue(TS3 .. "transform[*]{list}",
    { M.fill_transform_multidimarray(7, 2, g:uint32()), M.fill_transform_multidimarray(5, 1, g:uint32()) })
  return o
end

-- Verify a received teststruct3. Received varvalue fields arrive as plain values.
function M.verify_teststruct3(v, seed)
  local function vv(x) return x end
  local g = gen(seed)
  M.verify_testpod1(v.s1, g:uint32(), "s1")
  local s2_seed = g:uint32()
  M.verify_testpod1_array(v.s2, s2_seed % 17, s2_seed, "s2")
  M.verify_testpod1_array(v.s3, 11, g:uint32(), "s3")
  local s4_seed = g:uint32()
  M.verify_testpod1_array(v.s4, s4_seed % 16, s4_seed, "s4")
  M.verify_testpod1_multidimarray(v.s5, 3, 3, g:uint32(), "s5")
  local s6_seed = g:uint32()
  M.verify_testpod1_multidimarray(v.s6, s6_seed % 6, s6_seed % 3, s6_seed, "s6")
  expect(type(v.s7) == "table" and #v.s7 == 1, "s7 length")
  M.verify_testpod1(v.s7 and v.s7[1], g:uint32(), "s7[1]")
  expect(type(v.s8) == "table" and #v.s8 == 2, "s8 length")
  M.verify_testpod1_array(v.s8 and v.s8[1], 2, g:uint32(), "s8[1]")
  M.verify_testpod1_array(v.s8 and v.s8[2], 4, g:uint32(), "s8[2]")
  expect(type(v.s9) == "table" and #v.s9 == 2, "s9 length")
  M.verify_testpod1_multidimarray(v.s9 and v.s9[1], 2, 3, g:uint32(), "s9[1]")
  M.verify_testpod1_multidimarray(v.s9 and v.s9[2], 4, 5, g:uint32(), "s9[2]")
  local s10 = vv(v.s10)
  M.verify_testpod1(s10 and s10[1], g:uint32(), "s10")
  M.verify_testpod1_array(vv(v.s11), 3, g:uint32(), "s11")
  M.verify_testpod1_multidimarray(vv(v.s12), 2, 2, g:uint32(), "s12")
  local s13 = vv(v.s13)
  expect(type(s13) == "table" and #s13 == 1, "s13 length")
  local s13_1 = s13 and vv(s13[1])
  M.verify_testpod1(s13_1 and s13_1[1], g:uint32(), "s13[1]")
  local s14 = vv(v.s14)
  expect(type(s14) == "table" and #s14 == 2, "s14 length")
  M.verify_testpod1_array(s14 and vv(s14[1]), 3, g:uint32(), "s14[1]")
  M.verify_testpod1_array(s14 and vv(s14[2]), 5, g:uint32(), "s14[2]")
  local s15 = vv(v.s15)
  expect(type(s15) == "table" and #s15 == 2, "s15 length")
  M.verify_testpod1_multidimarray(s15 and vv(s15[1]), 7, 2, g:uint32(), "s15[1]")
  M.verify_testpod1_multidimarray(s15 and vv(s15[2]), 5, 1, g:uint32(), "s15[2]")
  M.verify_transform(v.t1, g:uint32(), "t1")
  M.verify_transform_array(v.t2, 4, g:uint32(), "t2")
  M.verify_transform_multidimarray(v.t3, 2, 4, g:uint32(), "t3")
  M.verify_transform_array(vv(v.t4), 10, g:uint32(), "t4")
  M.verify_transform_multidimarray(vv(v.t5), 6, 5, g:uint32(), "t5")
  expect(type(v.t6) == "table" and #v.t6 == 1, "t6 length")
  M.verify_transform(v.t6 and v.t6[1], g:uint32(), "t6[1]")
  M.verify_transform_array(v.t7 and v.t7[1], 4, g:uint32(), "t7[1]")
  M.verify_transform_array(v.t7 and v.t7[2], 4, g:uint32(), "t7[2]")
  M.verify_transform_multidimarray(v.t8 and v.t8[1], 2, 4, g:uint32(), "t8[1]")
  M.verify_transform_multidimarray(v.t8 and v.t8[2], 2, 4, g:uint32(), "t8[2]")
  local t9 = vv(v.t9)
  expect(type(t9) == "table" and #t9 == 1, "t9 length")
  local t9_1 = t9 and vv(t9[1])
  M.verify_transform(t9_1 and t9_1[1], g:uint32(), "t9[1]")
  local t10 = vv(v.t10)
  M.verify_transform_array(t10 and vv(t10[1]), 3, g:uint32(), "t10[1]")
  M.verify_transform_array(t10 and vv(t10[2]), 5, g:uint32(), "t10[2]")
  local t11 = vv(v.t11)
  M.verify_transform_multidimarray(t11 and vv(t11[1]), 7, 2, g:uint32(), "t11[1]")
  M.verify_transform_multidimarray(t11 and vv(t11[2]), 5, 1, g:uint32(), "t11[2]")
end

return M
