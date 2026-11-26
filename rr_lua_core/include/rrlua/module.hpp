// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Module-level functions of require("robotraconteur") and the C++ API for host plugins that
// link the binding statically (CLAUDE.md D7).
#pragma once

#include "rrlua/guard.hpp"
#include "rrlua/node.hpp"

namespace rl
{

class Client;

// rr.createNode([opts]): opts = { name = string, threads = integer }
Node* createNode(lua_State* L);
Node* createNode(lua_State* L, rl::Arg opts);
// rr.defaultNode(): the state's private default node (never the RR singleton)
Node* defaultNode(lua_State* L);
// rr.connectService(url[, options]): connect with the default node.
Client* connectService(lua_State* L, const std::string& url);
Client* connectService(lua_State* L, const std::string& url, rl::Arg options);

// Test support, not part of the API.
void _testPostFromThread(lua_State* L, int n);
double _testCounter(lua_State* L);
int _queueSize(lua_State* L);

} // namespace rl

// rr.step([budget]) (%native): run queued tasks on this thread. With a budget in seconds,
// stop once that much time has passed (checked after each task); without one, run the
// tasks that were queued when step was called. Returns the number of tasks run. Never
// waits for work.
int rl_step(lua_State* L);
// rr.sleep(seconds): wait, running this state's queued tasks (events, callbacks, completions).
int rl_sleep(lua_State* L);
// rr.time(): monotonic seconds.
int rl_time(lua_State* L);

extern "C" {
// Open the module in L; leaves the module table on the stack. For hosts that link the
// binding statically; the same as require("robotraconteur").
int luaopen_robotraconteur(lua_State* L);
}

namespace rl
{
// Host plugins: make a node the host created usable from L, and push a handle to it. The
// state borrows the node: it never shuts it down. The module must already be open in L.
// Call only from a host C++ frame, not from inside a Lua C function, as it may raise.
void push_borrowed_node(lua_State* L, const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node);
} // namespace rl
