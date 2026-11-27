-- Helpers for client tests against RR core's C++ test server (robotraconteur_test_server).
-- The server exits when its stdin closes, so it runs under io.popen(cmd, "w").
local rr = require("robotraconteur")
local t = require("testutil")

local M = {}

M.vectors = assert(os.getenv("RRLUA_TEST_VECTORS"), "RRLUA_TEST_VECTORS not set")
local server_path = assert(os.getenv("RRLUA_TEST_SERVER"), "RRLUA_TEST_SERVER not set")
M.port = tonumber(os.getenv("RRLUA_TEST_PORT") or "22231")

function M.vector(name, ty) return rr._testVector(M.vectors, name, ty) end

-- A multidim rr.array from a vector and its ".dims" vector.
function M.mvector(name, ty)
  local data = M.vector(name, ty)
  local d = M.vector(name .. ".dims", "uint32"):toTable()
  return rr.multidim(ty, d, data)
end

local server
function M.start_server()
  server = assert(io.popen(server_path .. " --robotraconteur-tcp-port=" .. M.port .. " >/dev/null 2>&1", "w"))
end

function M.stop_server()
  if server then server:close(); server = nil end
end

-- Connect, retrying while the server starts (up to about 15 s).
function M.url(service) return "rr+tcp://localhost:" .. M.port .. "?service=" .. service end

function M.connect(node, service)
  local url = M.url(service)
  local last
  for _ = 1, 150 do
    local ok, c = pcall(node.connectService, node, url)
    if ok then return c end
    last = c
    os.execute("sleep 0.1")
  end
  error("could not connect to " .. url .. ": " .. tostring(last))
end

-- Element-wise equality of two rr.arrays (type, dims and values).
function M.same(a, b, what)
  t.truthy(rr.isarray(a), (what or "value") .. " is an rr.array")
  t.eq(a:type(), b:type(), (what or "") .. " type")
  local da, db = a:dims(), b:dims()
  t.eq(#da, #db, (what or "") .. " ndims")
  for i = 1, #da do t.eq(da[i], db[i], (what or "") .. " dim " .. i) end
  t.truthy(a == b, (what or "") .. " values")
end

return M
