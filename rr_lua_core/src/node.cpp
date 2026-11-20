// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/node.hpp"

#include <stdexcept>

namespace rl
{

Node::Node(std::shared_ptr<NodeEntry> entry) : entry_(std::move(entry)) {}

std::string Node::getNodeName() { return entry_->node->NodeName(); }

std::string Node::getNodeID() { return entry_->node->NodeID().ToString(); }

bool Node::isOwned() { return entry_->owned; }

bool Node::isShutdown() { return entry_->shutdown; }

void Node::shutdown()
{
    if (!entry_->owned)
        throw std::logic_error("cannot shut down a node borrowed from the host");
    if (entry_->shutdown)
        return;
    entry_->shutdown = true;
    entry_->node->Shutdown();
}

RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode> Node::rr_node()
{
    if (entry_->shutdown)
        throw std::logic_error("node has been shut down");
    return entry_->node;
}

} // namespace rl
