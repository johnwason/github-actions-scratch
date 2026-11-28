-- M0.4: raw pass-through (rl::Arg / rl::Ret), %native varargs, and the error guard.
local t = require("testutil")
local m = require("rrlua_m0")

io.write("runtime: ", jit and jit.version or _VERSION, "\n")

local p = m.Probe()

t.check("echo passes values through intact", function()
  local tbl, fn = {}, function() end
  t.eq(p:echo(tbl), tbl, "table identity")
  t.eq(p:echo(fn), fn, "function identity")
  t.eq(p:echo("a\0b"), "a\0b", "string with embedded zero")
  t.eq(p:echo(1.5), 1.5)
  t.eq(p:echo(true), true)
  t.eq(p:echo(p), p, "userdata identity")
end)

t.check("nil passes through intact", function()
  t.eq(select("#", p:echo(nil)), 1, "result count")
  t.eq(p:echo(nil), nil)
  t.eq(p:typeName(nil), "nil")
end)

t.check("multiple and zero results", function()
  local a, b = p:swap(1, nil)
  t.eq(a, nil); t.eq(b, 1)
  t.eq(select("#", p:swap(nil, nil)), 2)
  t.eq(select("#", p:none()), 0)
end)

t.check("wrong argument counts raise Lua errors", function()
  t.raises(function() return p:echo() end)
  t.raises(function() return p:echo(1, 2) end)
end)

t.check("%native varargs counts nils", function()
  t.eq(select("#", m.varargs()), 1)
  t.eq(m.varargs(), 0)
  local n, a, b, c = m.varargs(nil, 1, nil)
  t.eq(n, 3); t.eq(a, nil); t.eq(b, 1); t.eq(c, nil)
  t.eq(select("#", m.varargs(nil, 1, nil)), 4)
end)

t.check("SWIG method: C++ throw becomes a Lua error with location; destructors run", function()
  local err = t.raises(function() p:throwWith("hello") end, "m0 error: hello")
  t.truthy(tostring(err):find("test_m0.lua:%d+:"), "script location in '" .. tostring(err) .. "'")
  t.eq(p:liveTrackers(), 0, "live trackers")
end)

t.check("%native: C++ throw becomes a Lua error with location; destructors run", function()
  local err = t.raises(function() m.varargs_throw("bye") end, "m0 native error: bye")
  t.truthy(tostring(err):find("test_m0.lua:%d+:"), "script location in '" .. tostring(err) .. "'")
  t.eq(p:liveTrackers(), 0, "live trackers")
end)

t.check("errors inside coroutines", function()
  local co = coroutine.create(function() p:throwWith("co") end)
  local ok, err = coroutine.resume(co)
  t.eq(ok, false)
  t.truthy(tostring(err):find("m0 error: co"), "message")
  t.eq(p:liveTrackers(), 0, "live trackers")
end)

t.finish()
