// Copyright 2026 the robotraconteur_lua authors. SPDX-License-Identifier: Apache-2.0
//
// Client side (M2.4): stubs created at runtime from pulled service definitions (CLAUDE.md
// D2), and the Lua handle to a connected object (SWIG, D1).
#pragma once

#include "rrlua/guard.hpp"
#include "rrlua/marshal.hpp"
#include "rrlua/node.hpp"

#include <RobotRaconteur.h>

#include <memory>
#include <string>

namespace rl
{

// A ServiceStub for any object type, driven by its ServiceEntryDefinition.
class DynamicStub : public virtual RobotRaconteur::ServiceStub
{
  public:
    DynamicStub(boost::string_ref path, const RR_SHARED_PTR<RobotRaconteur::ServiceEntryDefinition>& type,
                const RR_SHARED_PTR<RobotRaconteur::ClientContext>& c);

    RR_SHARED_PTR<RobotRaconteur::ServiceEntryDefinition> objtype;

    MarshalContext marshal_context();

    void DispatchEvent(const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallbackCall(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry>& m) RR_OVERRIDE;
    std::string RRType() RR_OVERRIDE;
    void RRInitStub() RR_OVERRIDE {} // pipes and wires arrive in M3
};

// Creates DynamicStubs for one service definition.
class StubFactory : public virtual RobotRaconteur::ServiceFactory
{
  public:
    explicit StubFactory(const std::string& defstring);

    std::string GetServiceName() RR_OVERRIDE;
    std::string DefString() RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::ServiceDefinition> ServiceDef() RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::StructureStub> FindStructureStub(boost::string_ref s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackStructure(
        const RR_INTRUSIVE_PTR<RobotRaconteur::RRStructure>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRValue> UnpackStructure(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackPodArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseArray>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseArray> UnpackPodArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackPodMultiDimArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseMultiDimArray>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseMultiDimArray> UnpackPodMultiDimArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackNamedArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseArray>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseArray> UnpackNamedArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackNamedMultiDimArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseMultiDimArray>& s) RR_OVERRIDE;
    RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseMultiDimArray> UnpackNamedMultiDimArray(
        const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList>& s) RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::ServiceStub> CreateStub(
        boost::string_ref objecttype, boost::string_ref path,
        const RR_SHARED_PTR<RobotRaconteur::ClientContext>& context) RR_OVERRIDE;
    RR_SHARED_PTR<RobotRaconteur::ServiceSkel> CreateSkel(
        boost::string_ref objecttype, boost::string_ref path, const RR_SHARED_PTR<RobotRaconteur::RRObject>& obj,
        const RR_SHARED_PTR<RobotRaconteur::ServerContext>& context) RR_OVERRIDE;
    void DownCastAndThrowException(RobotRaconteur::RobotRaconteurException& exp) RR_OVERRIDE { throw exp; }
    RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException> DownCastException(
        const RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>& exp) RR_OVERRIDE
    {
        return exp;
    }

  private:
    std::string defstring_;
    RR_SHARED_PTR<RobotRaconteur::ServiceDefinition> def_;
};

class DynamicStubFactory : public virtual RobotRaconteur::DynamicServiceFactory
{
  public:
    RR_SHARED_PTR<RobotRaconteur::ServiceFactory> CreateServiceFactory(boost::string_ref def) RR_OVERRIDE;
    std::vector<RR_SHARED_PTR<RobotRaconteur::ServiceFactory> > CreateServiceFactories(
        const std::vector<std::string>& defs) RR_OVERRIDE;
};

// Lua handle to a connected object (a service root or an objref).
class Client
{
  public:
    Client(RR_SHARED_PTR<DynamicStub> stub, std::shared_ptr<NodeEntry> node, bool root);

    rl::Ret getProperty(lua_State* L, const std::string& name);
    void setProperty(const std::string& name, rl::Arg value);
    Client* getObjRef(const std::string& name);
    Client* getObjRef(const std::string& name, rl::Arg index);
    std::string getType();
    bool isRoot();
    // Disconnect (service root only). Idempotent.
    void close();

    // obj:callFunction(name, ...) (registered as a native method): stack 1 = self.
    int call_function(lua_State* L);

  private:
    RR_SHARED_PTR<DynamicStub> stub();
    RR_SHARED_PTR<DynamicStub> stub_;
    std::shared_ptr<NodeEntry> node_;
    bool root_;
    bool closed_ = false;
};

// node:connectService(url) implementation.
Client* connect_service(const std::shared_ptr<NodeEntry>& node, const std::string& url);

} // namespace rl
