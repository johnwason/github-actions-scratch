// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
#include "rrlua/node.hpp"

#include "rrlua/async.hpp"
#include "rrlua/server.hpp"
#include "rrlua/subscription.hpp"

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

Subscription* Node::subscribeService(rl::Arg urls) { return subscribe_service(entry_, urls); }

Subscription* Node::subscribeServiceByType(rl::Arg types) { return subscribe_service_by_type(entry_, types); }

rl::Ret Node::findServiceByType(lua_State* L, const std::string& type) { return findServiceByType(L, type, 5.0); }

rl::Ret Node::findServiceByType(lua_State* L, const std::string& type, double timeout)
{
    RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode> n = rr_node();
    typedef RR_SHARED_PTR<std::vector<RobotRaconteur::ServiceInfo2> > Result;
    auto c = std::make_shared<Completion<Result> >(blocking_queue(L));
    std::shared_ptr<Completion<Result> > cc = c;
    std::vector<std::string> schemes(1, "rr+tcp");
    n->AsyncFindServiceByType(
        type, schemes, [cc](const Result& r) { cc->complete(r, RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>()); },
        static_cast<int32_t>(timeout * 1000.0));
    Result r = c->wait(L);
    lua_createtable(L, r ? static_cast<int>(r->size()) : 0, 0);
    int t = lua_gettop(L);
    if (r)
        for (size_t i = 0; i < r->size(); ++i)
        {
            const RobotRaconteur::ServiceInfo2& si = (*r)[i];
            lua_createtable(L, 0, 5);
            lua_pushstring(L, si.Name.c_str());
            lua_setfield(L, -2, "name");
            lua_pushstring(L, si.NodeName.c_str());
            lua_setfield(L, -2, "nodeName");
            lua_pushstring(L, si.NodeID.ToString().c_str());
            lua_setfield(L, -2, "nodeID");
            lua_pushstring(L, si.RootObjectType.c_str());
            lua_setfield(L, -2, "rootObjectType");
            lua_createtable(L, static_cast<int>(si.ConnectionURL.size()), 0);
            for (size_t k = 0; k < si.ConnectionURL.size(); ++k)
            {
                lua_pushstring(L, si.ConnectionURL[k].c_str());
                lua_rawseti(L, -2, static_cast<int>(k + 1));
            }
            lua_setfield(L, -2, "urls");
            lua_rawseti(L, t, static_cast<int>(i + 1));
        }
    return {1};
}

std::string Node::registerServiceType(const std::string& def) { return register_service_type(entry_, def); }

int Node::startTcpServer(int port) { return start_tcp_server(entry_, port); }

Service* Node::registerService(lua_State* L, const std::string& name, const std::string& type, rl::Arg handlers)
{
    return register_service(L, entry_, name, type, handlers, rl::Arg());
}

Service* Node::registerService(lua_State* L, const std::string& name, const std::string& type, rl::Arg handlers,
                               rl::Arg options)
{
    return register_service(L, entry_, name, type, handlers, options);
}

} // namespace rl
