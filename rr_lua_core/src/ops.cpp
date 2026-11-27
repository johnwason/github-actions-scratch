// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/ops.hpp"

#include "rrlua/async.hpp"
#include "rrlua/context.hpp"
#include "rrlua/guard.hpp"
#include "rrlua/lua.hpp"

#include <string>

namespace rl
{
namespace
{
const char* const kOpMeta = "robotraconteur.op";

struct OpHeader
{
    int done; // 0 pending, 1 done
    int ok;   // 1 success, 0 error
};

OpHeader* test_op(lua_State* L, int idx)
{
    void* p = lua_touserdata(L, idx);
    if (!p || !lua_getmetatable(L, idx))
        return nullptr;
    luaL_getmetatable(L, kOpMeta);
    bool same = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return same ? static_cast<OpHeader*>(p) : nullptr;
}

OpHeader* check_op(lua_State* L, int idx)
{
    OpHeader* h = test_op(L, idx);
    if (!h)
        luaL_argerror(L, idx, "expected a robotraconteur op");
    return h;
}

// Push field `name` of the op's user value table.
void op_field(lua_State* L, int op_idx, const char* name)
{
    lua_getuservalue(L, op_idx);
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
}

// The completion, on the Lua thread.
class OpTask : public Task
{
  public:
    OpTask(int ref, OpConverter conv, const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>& err)
        : ref_(ref), conv_(std::move(conv)), err_(err)
    {}

    void run(lua_State* L) override
    {
        if (!lua_checkstack(L, 16))
            throw std::runtime_error("Lua stack overflow");
        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref_);
        luaL_unref(L, LUA_REGISTRYINDEX, ref_);
        int op = lua_gettop(L);
        OpHeader* h = static_cast<OpHeader*>(lua_touserdata(L, op));
        if (!h)
        {
            lua_settop(L, top);
            return;
        }
        lua_getuservalue(L, op);
        int uv = lua_gettop(L);

        // Store the result. A value that fails to unpack becomes the op's error.
        std::string err;
        if (err_)
            err = err_->Error + ": " + err_->Message;
        else
        {
            try
            {
                conv_(L);
                lua_setfield(L, uv, "value");
            }
            catch (const RobotRaconteur::RobotRaconteurException& e)
            {
                lua_settop(L, uv);
                err = e.Error + ": " + e.Message;
            }
            catch (const std::exception& e)
            {
                lua_settop(L, uv);
                err = e.what();
            }
        }
        if (!err.empty())
        {
            lua_pushlstring(L, err.data(), err.size());
            lua_setfield(L, uv, "err");
        }
        h->done = 1;
        h->ok = err.empty() ? 1 : 0;

        // Resume awaiting coroutines first, then the handler, so an error in one cannot leave
        // the others waiting. The first error is raised afterwards.
        std::string first_error;
        lua_getfield(L, uv, "waiters");
        int waiters = lua_gettop(L);
        if (lua_istable(L, waiters))
        {
            int n = static_cast<int>(lua_rawlen(L, waiters));
            for (int i = 1; i <= n; ++i)
            {
                lua_rawgeti(L, waiters, i);
                lua_State* co = lua_tothread(L, -1);
                if (co && lua_status(co) == LUA_YIELD)
                {
#if LUA_VERSION_NUM >= 504
                    int nres = 0;
                    int st = lua_resume(co, L, 0, &nres);
#else
                    int st = lua_resume(co, L, 0);
                    int nres = lua_gettop(co);
#endif
                    if (st == LUA_OK || st == LUA_YIELD)
                        lua_pop(co, nres); // values yielded to us, or the body's results
                    else
                    {
                        if (first_error.empty())
                            first_error = lua_error_text(co, -1);
                        lua_pop(co, 1);
                    }
                }
                lua_pop(L, 1);
            }
        }
        lua_getfield(L, uv, "handler");
        if (lua_isfunction(L, -1))
        {
            op_field(L, op, "value");
            op_field(L, op, "err");
            if (lua_pcall(L, 2, 0, 0) != 0)
            {
                if (first_error.empty())
                    first_error = lua_error_text(L, -1);
                lua_pop(L, 1);
            }
        }
        else
            lua_pop(L, 1);
        lua_settop(L, top);
        if (!first_error.empty())
            throw LuaError(first_error);
    }

  private:
    int ref_;
    OpConverter conv_;
    RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException> err_;
};

// op:isDone()
int op_isdone(lua_State* L)
{
    lua_pushboolean(L, check_op(L, 1)->done);
    return 1;
}

// _status(op) -> done, ok, value-or-error (for the Lua-side await and result)
int op_status(lua_State* L)
{
    OpHeader* h = check_op(L, 1);
    lua_pushboolean(L, h->done);
    lua_pushboolean(L, h->ok);
    if (!h->done)
        lua_pushnil(L);
    else
        op_field(L, 1, h->ok ? "value" : "err");
    return 3;
}

// _addwaiter(op, co)
int op_addwaiter(lua_State* L)
{
    check_op(L, 1);
    luaL_checktype(L, 2, LUA_TTHREAD);
    lua_getuservalue(L, 1);
    lua_getfield(L, -1, "waiters");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "waiters");
    }
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, static_cast<int>(lua_rawlen(L, -2)) + 1);
    return 0;
}

// _wait(op): block, running the pump, until op is done (await outside a coroutine).
int op_wait(lua_State* L)
{
    OpHeader* h = check_op(L, 1);
    return guard(L, [L, h]() {
        pump_until(L, [h]() { return h->done != 0; });
        return 0;
    });
}

// rr.await and op:result, in Lua: a C function cannot run code after a coroutine yield on
// LuaJIT (no lua_yieldk), and await must raise the op's error after resuming.
const char kOpLua[] = R"lua(
local status, addwaiter, wait = ...
local running, yield, isyieldable = coroutine.running, coroutine.yield, coroutine.isyieldable
local function can_yield()
  if isyieldable then return isyieldable() end
  local co, main = running()
  return co ~= nil and not main
end
local function result(op)
  local done, ok, v = status(op)
  if not done then error("operation is not done; use rr.await(op)", 2) end
  if not ok then error(v, 2) end
  return v
end
local function await(op)
  local done = status(op)
  if not done then
    if can_yield() then
      addwaiter(op, running())
      repeat yield() until (status(op))
    else
      wait(op)
    end
  end
  local _, ok, v = status(op)
  if not ok then error(v, 2) end
  return v
end
return await, result
)lua";

} // namespace

int push_new_op(lua_State* L, int handler_idx)
{
    if (handler_idx != 0)
        handler_idx = lua_absindex(L, handler_idx);
    OpHeader* h = static_cast<OpHeader*>(lua_newuserdata(L, sizeof(OpHeader)));
    h->done = 0;
    h->ok = 0;
    luaL_getmetatable(L, kOpMeta);
    lua_setmetatable(L, -2);
    lua_newtable(L);
    if (handler_idx != 0 && lua_isfunction(L, handler_idx))
    {
        lua_pushvalue(L, handler_idx);
        lua_setfield(L, -2, "handler");
    }
    lua_setuservalue(L, -2);
    lua_pushvalue(L, -1);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

void release_op(lua_State* L, int ref) { luaL_unref(L, LUA_REGISTRYINDEX, ref); }

void post_op_result(const std::shared_ptr<DispatchQueue>& q, int ref, OpConverter conv,
                    const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>& err)
{
    if (q)
        q->post(std::unique_ptr<Task>(new OpTask(ref, std::move(conv), err)));
}

void open_ops(lua_State* L, int module_idx)
{
    module_idx = lua_absindex(L, module_idx);
    if (luaL_newmetatable(L, kOpMeta))
    {
        lua_pushstring(L, kOpMeta);
        lua_setfield(L, -2, "__name");
    }
    int meta = lua_gettop(L);
    if (luaL_loadbuffer(L, kOpLua, sizeof(kOpLua) - 1, "=robotraconteur.await") != 0)
        lua_error(L);
    lua_pushcfunction(L, op_status);
    lua_pushcfunction(L, op_addwaiter);
    lua_pushcfunction(L, op_wait);
    lua_call(L, 3, 2); // await, result
    lua_newtable(L);   // methods
    lua_pushcfunction(L, op_isdone);
    lua_setfield(L, -2, "isDone");
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, "result");
    lua_setfield(L, meta, "__index");
    lua_pop(L, 1); // result
    lua_pushliteral(L, "await");
    lua_insert(L, -2);
    lua_rawset(L, module_idx);
    lua_pop(L, 1); // metatable
}

} // namespace rl
