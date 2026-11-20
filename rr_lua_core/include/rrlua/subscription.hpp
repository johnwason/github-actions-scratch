// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Subscriptions (M5): RR's ServiceSubscription keeps connections to services (by URL, or by type
// through discovery) and reconnects them. Wire and pipe subscriptions aggregate members of the
// connected services; values stay raw in the core (they use the stubs' own raw wire and pipe
// clients) and are converted only when Lua reads them, using the type of the member on the
// connection that delivered them.
#pragma once

#include "rrlua/guard.hpp"
#include "rrlua/node.hpp"

#include <RobotRaconteur.h>

#include <memory>
#include <string>

namespace rl
{

class Client;
class WireSubscriptionH;
class PipeSubscriptionH;

// Lua handle to a service subscription (SWIG).
class Subscription
{
  public:
    Subscription(std::shared_ptr<NodeEntry> node, RR_SHARED_PTR<RobotRaconteur::ServiceSubscription> sub);

    // The connected clients, as a list of client handles.
    rl::Ret getConnectedClients(lua_State* L);
    // The default client, or an error if none is connected.
    Client* getDefaultClient(lua_State* L);
    // Wait (with the pump) up to timeout seconds for a connected client.
    Client* getDefaultClientWait(lua_State* L, double timeout);
    WireSubscriptionH* subscribeWire(const std::string& name);
    WireSubscriptionH* subscribeWire(const std::string& name, const std::string& servicepath);
    PipeSubscriptionH* subscribePipe(const std::string& name);
    PipeSubscriptionH* subscribePipe(const std::string& name, const std::string& servicepath);
    void close();

  private:
    Client* wrap(lua_State* L, const RR_SHARED_PTR<RobotRaconteur::RRObject>& o);
    std::shared_ptr<NodeEntry> node_;
    RR_SHARED_PTR<RobotRaconteur::ServiceSubscription> sub_;
};

class WireSubscriptionH
{
  public:
    WireSubscriptionH(RR_SHARED_PTR<RobotRaconteur::WireSubscriptionBase> w,
                      RR_SHARED_PTR<RobotRaconteur::ServiceSubscription> sub, std::string name, std::string path);
    rl::Ret getInValue(lua_State* L);    // raises if no value
    rl::Ret tryGetInValue(lua_State* L); // ok, value
    int activeWireConnectionCount();
    void setInValueLifespan(double seconds);
    // Send to every connected wire (no-op with no connection).
    void setOutValueAll(rl::Arg value);
    void close();

  private:
    RR_SHARED_PTR<RobotRaconteur::WireSubscriptionBase> w_;
    RR_SHARED_PTR<RobotRaconteur::ServiceSubscription> sub_;
    std::string name_, path_;
};

class PipeSubscriptionH
{
  public:
    PipeSubscriptionH(RR_SHARED_PTR<RobotRaconteur::PipeSubscriptionBase> p,
                      RR_SHARED_PTR<RobotRaconteur::ServiceSubscription> sub, std::string name, std::string path);
    rl::Ret receivePacket(lua_State* L);    // raises if none
    rl::Ret tryReceivePacket(lua_State* L); // ok, value
    int available();
    int activePipeEndpointCount();
    void sendPacketAll(rl::Arg value);
    void close();

  private:
    RR_SHARED_PTR<RobotRaconteur::PipeSubscriptionBase> p_;
    RR_SHARED_PTR<RobotRaconteur::ServiceSubscription> sub_;
    std::string name_, path_;
};

// node:subscribeService(url or {urls}) and node:subscribeServiceByType(type or {types}).
Subscription* subscribe_service(const std::shared_ptr<NodeEntry>& node, rl::Arg urls);
Subscription* subscribe_service_by_type(const std::shared_ptr<NodeEntry>& node, rl::Arg types);

} // namespace rl
