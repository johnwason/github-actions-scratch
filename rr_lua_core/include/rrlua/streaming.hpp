// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Client wires and pipes (M3). Values travel as raw MessageElements (`rawelements`, as in the
// MEX binding) and are converted only when Lua reads or writes them, so Lua stays off the
// network path (CLAUDE.md D10): the core keeps a wire's latest value and a pipe's received
// packets, and Lua reads them during its step.
//
// Optional listeners (wire value changed, pipe packet received) are coalesced: at most one
// notification task per connection or endpoint is queued at a time, so a fast stream cannot
// grow the queue. A listener reads the latest value, or drains the packets, when it runs.
#pragma once

#include "rrlua/context.hpp"
#include "rrlua/guard.hpp"

#include <RobotRaconteur.h>

#include <boost/thread/mutex.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace rl
{

class DynamicStub;

// Lua-thread notification hook shared by wire connections and pipe endpoints.
class Notifier
{
  public:
    virtual ~Notifier() = default;
    // Lua thread: call the listeners.
    virtual void notify(lua_State* L) = 0;

    // Lua thread: route notifications to L's state; returns the handlers.
    std::shared_ptr<LuaHandlers> attach(lua_State* L, const boost::weak_ptr<void>& owner);
    std::shared_ptr<LuaHandlers> handlers();

    // Lua thread: listener bookkeeping (RR threads read the count to skip posting).
    long long add_listener(lua_State* L, const boost::weak_ptr<void>& owner, const char* what, int fn_idx);
    bool remove_listener(lua_State* L, long long id);
    // Lua thread: the registered listener references, oldest first.
    std::vector<int> listener_refs();

  protected:
    // RR thread: post one notification unless one is already queued.
    void post_notification(const RR_SHARED_PTR<Notifier>& self);
    // RR thread: the object was closed (its references are released by the next sweep).
    void mark_closed();
    // Lua thread: a notification has started (later arrivals post a new one).
    void notification_started() { pending_.store(false); }

  private:
    boost::mutex m_;
    std::weak_ptr<DispatchQueue> queue_;
    std::shared_ptr<LuaHandlers> handlers_;
    std::atomic<bool> pending_{false};
    std::atomic<int> listeners_{0};
};

class LuaWireConnection : public virtual RobotRaconteur::WireConnectionBase, public Notifier
{
  public:
    LuaWireConnection(const RR_SHARED_PTR<RobotRaconteur::WireBase>& parent,
                      const RR_SHARED_PTR<RobotRaconteur::TypeDefinition>& type,
                      RobotRaconteur::MemberDefinition_Direction direction);

    RR_SHARED_PTR<RobotRaconteur::TypeDefinition> type;

    bool try_in_value(RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement>& v);
    bool try_out_value(RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement>& v);
    void set_out_value(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement>& v);
    // The stub this connection belongs to (null once released).
    RR_SHARED_PTR<DynamicStub> stub();

    void notify(lua_State* L) RR_OVERRIDE;

  protected:
    void fire_WireValueChanged(const RR_INTRUSIVE_PTR<RobotRaconteur::RRValue>& value,
                               RobotRaconteur::TimeSpec time) RR_OVERRIDE;
    void fire_WireClosedCallback() RR_OVERRIDE;
};

class LuaWireClient : public virtual RobotRaconteur::WireClientBase
{
  public:
    LuaWireClient(const std::string& name, const RR_SHARED_PTR<RobotRaconteur::ServiceStub>& stub,
                  const RR_SHARED_PTR<RobotRaconteur::TypeDefinition>& type,
                  RobotRaconteur::MemberDefinition_Direction direction);

    RR_SHARED_PTR<RobotRaconteur::TypeDefinition> type;

    void connect(boost::function<void(const RR_SHARED_PTR<RobotRaconteur::WireConnectionBase>&,
                                      const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>&)>
                     handler,
                 int32_t timeout);
    void peek(bool in,
              boost::function<void(const RR_INTRUSIVE_PTR<RobotRaconteur::RRValue>&, const RobotRaconteur::TimeSpec&,
                                   const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>&)>
                  handler,
              int32_t timeout);
    void poke(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement>& v,
              boost::function<void(const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>&)> handler,
              int32_t timeout);

  protected:
    RR_SHARED_PTR<RobotRaconteur::WireConnectionBase> CreateNewWireConnection(
        RobotRaconteur::MemberDefinition_Direction direction) RR_OVERRIDE;
};

class LuaPipeEndpoint : public RobotRaconteur::PipeEndpointBase, public Notifier
{
  public:
    LuaPipeEndpoint(const RR_SHARED_PTR<RobotRaconteur::PipeBase>& parent, int32_t index,
                    const RR_SHARED_PTR<RobotRaconteur::TypeDefinition>& type, bool unreliable,
                    RobotRaconteur::MemberDefinition_Direction direction);

    RR_SHARED_PTR<RobotRaconteur::TypeDefinition> type;

    void send(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement>& v,
              boost::function<void(uint32_t, const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>&)> handler);
    // The next packet (removed unless peek), or null if none.
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement> next(bool peek);
    int32_t index() const { return index_; }
    RR_SHARED_PTR<DynamicStub> stub();

    void notify(lua_State* L) RR_OVERRIDE;

  protected:
    void fire_PipeEndpointClosedCallback() RR_OVERRIDE;
    void fire_PacketReceivedEvent() RR_OVERRIDE;
    void fire_PacketAckReceivedEvent(uint32_t packetnum) RR_OVERRIDE;

  private:
    int32_t index_;
};

class LuaPipeClient : public virtual RobotRaconteur::PipeClientBase
{
  public:
    LuaPipeClient(const std::string& name, const RR_SHARED_PTR<RobotRaconteur::ServiceStub>& stub,
                  const RR_SHARED_PTR<RobotRaconteur::TypeDefinition>& type, bool unreliable,
                  RobotRaconteur::MemberDefinition_Direction direction);

    RR_SHARED_PTR<RobotRaconteur::TypeDefinition> type;

    void connect(int32_t index,
                 boost::function<void(const RR_SHARED_PTR<RobotRaconteur::PipeEndpointBase>&,
                                      const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>&)>
                     handler,
                 int32_t timeout);

  protected:
    RR_SHARED_PTR<RobotRaconteur::PipeEndpointBase> CreateNewPipeEndpoint(
        int32_t index, bool unreliable, RobotRaconteur::MemberDefinition_Direction direction) RR_OVERRIDE;
};

// Lua handle to a wire connection (SWIG).
class WireConnection
{
  public:
    WireConnection(lua_State* L, RR_SHARED_PTR<LuaWireConnection> c, RR_SHARED_PTR<DynamicStub> stub);

    rl::Ret getInValue(lua_State* L);
    rl::Ret tryGetInValue(lua_State* L); // ok, value
    rl::Ret getOutValue(lua_State* L);
    rl::Ret tryGetOutValue(lua_State* L);
    void setOutValue(rl::Arg value);
    bool inValueValid();
    bool outValueValid();
    double getInValueLifespan();
    void setInValueLifespan(double seconds);
    double getOutValueLifespan();
    void setOutValueLifespan(double seconds);
    std::string direction();
    // fn(value) runs in the pump after new values arrive (coalesced: with the latest value).
    long long addValueChangedListener(lua_State* L, rl::Arg fn);
    bool removeValueChangedListener(lua_State* L, long long id);
    void close(lua_State* L);

  private:
    RR_SHARED_PTR<LuaWireConnection> conn();
    RR_SHARED_PTR<LuaWireConnection> c_;
    RR_SHARED_PTR<DynamicStub> stub_;
    bool closed_ = false;
};

// Lua handle to a pipe endpoint (SWIG).
class PipeEndpoint
{
  public:
    PipeEndpoint(lua_State* L, RR_SHARED_PTR<LuaPipeEndpoint> e, RR_SHARED_PTR<DynamicStub> stub);

    // Send (waits for the send to complete, running the pump). Returns the packet number.
    double sendPacket(rl::Arg value);
    rl::Ret receivePacket(lua_State* L);    // raises if none
    rl::Ret tryReceivePacket(lua_State* L); // ok, value
    rl::Ret peekNextPacket(lua_State* L);   // raises if none
    int available();
    int index();
    bool isUnreliable();
    std::string direction();
    bool getRequestPacketAck();
    void setRequestPacketAck(bool ack);
    // fn() runs in the pump after packets arrive (coalesced: drain with receivePacket).
    long long addPacketReceivedListener(lua_State* L, rl::Arg fn);
    bool removePacketReceivedListener(lua_State* L, long long id);
    void close(lua_State* L);

  private:
    RR_SHARED_PTR<LuaPipeEndpoint> ep();
    RR_SHARED_PTR<LuaPipeEndpoint> e_;
    RR_SHARED_PTR<DynamicStub> stub_;
    bool closed_ = false;
};

} // namespace rl
