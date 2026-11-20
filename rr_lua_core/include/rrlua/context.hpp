// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Per-lua_State binding state (CLAUDE.md D6, D8, "State teardown"). Stored as a sentinel
// userdata in the Lua registry; its __gc tears the state's binding down when the host closes
// the lua_State: close the dispatch queue, then shut down the nodes this state owns. Nodes
// borrowed from the host are left running.
#pragma once

#include "rrlua/dispatch.hpp"
#include "rrlua/lua_api.hpp"

#include <RobotRaconteur.h>

#include <memory>
#include <thread>
#include <vector>

namespace rl
{

// A node attached to a lua_State. Shared by every Lua handle to that node.
struct NodeEntry
{
    RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode> node;
    bool owned = false;    // created by this state: shut down with it
    bool shutdown = false; // shut down through this binding
};

class StateContext
{
  public:
    // The context of L's state, or nullptr if the module was never opened in it.
    static StateContext* get(lua_State* L);
    // The context, created on first use. Call only with no non-trivial C++ object live in
    // the calling frames: it allocates Lua memory, which can raise.
    static StateContext* ensure(lua_State* L);
    // get(), or a C++ exception if the module was never opened in this state.
    static StateContext* require(lua_State* L);

    std::shared_ptr<DispatchQueue> queue() const { return queue_; }

    // Create, initialize and attach a node owned by this state.
    std::shared_ptr<NodeEntry> create_node(const std::string& name, int thread_count);
    // Attach a node the host created and keeps owning (never shut down by the binding).
    std::shared_ptr<NodeEntry> attach_borrowed(const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode>& node);
    // The state's private default node, created on first use. Never the RR singleton.
    std::shared_ptr<NodeEntry> default_node();

    bool in_step = false;

    // Test support: tasks posted from other threads (see _testPostFromThread).
    long long test_counter = 0;
    std::vector<std::thread> test_threads;

    ~StateContext();

  private:
    StateContext();
    void teardown();
    void prune();

    std::shared_ptr<DispatchQueue> queue_;
    std::vector<std::shared_ptr<NodeEntry> > nodes_;
    std::shared_ptr<NodeEntry> default_;
    bool torn_down_ = false;

    static int gc(lua_State* L);
};

} // namespace rl
