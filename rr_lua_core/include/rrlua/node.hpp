// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Lua handle to a node (wrapped by SWIG, CLAUDE.md D1). Several handles can refer to the same
// node; they share its NodeEntry. Destroying a handle never shuts the node down: owned nodes
// are shut down by node:shutdown() or when their lua_State closes.
#pragma once

#include "rrlua/context.hpp"

#include <memory>
#include <string>

namespace rl
{

class Client;

class Node
{
  public:
    explicit Node(std::shared_ptr<NodeEntry> entry);

    std::string getNodeName();
    std::string getNodeID();
    bool isOwned();
    bool isShutdown();
    // Shut down an owned node. Raises for a borrowed node. Idempotent.
    void shutdown();

    // Connect to a service (blocking). Returns a handle owned by the caller.
    Client* connectService(lua_State* L, const std::string& url);

    std::shared_ptr<NodeEntry> entry() const { return entry_; }

    // The node, or an exception if it has been shut down through this binding.
    RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode> rr_node();

  private:
    std::shared_ptr<NodeEntry> entry_;
};

} // namespace rl
