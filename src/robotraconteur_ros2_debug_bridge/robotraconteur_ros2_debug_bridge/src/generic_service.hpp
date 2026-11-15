// Copyright 2026 Wason Technology, LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A generic Robot Raconteur server layer for objects whose definitions are
// generated at run time (type_mapper.hpp): a ServiceFactory, a ServiceSkel,
// and pipe and wire servers working on packed MessageElements, against Robot
// Raconteur core's public interfaces. Modeled on the SWIG bindings' Wrapped
// layer (WrappedServiceFactory, WrappedServiceSkel, WrappedPipeServer, ...).
//
// Threading: Robot Raconteur calls into these from its thread pool, several
// clients concurrently. GenericObject callbacks must be thread-safe and must
// not block on ROS.

#ifndef GENERIC_SERVICE_HPP_
#define GENERIC_SERVICE_HPP_

#include <RobotRaconteur.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace robotraconteur_ros2_debug_bridge
{

using ElementPtr = RR_INTRUSIVE_PTR<RobotRaconteur::MessageElement>;
using RRErrorPtr = RR_SHARED_PTR<RobotRaconteur::RobotRaconteurException>;

/// Serves the objects of one generated definition. Register it with
/// RobotRaconteurNode::RegisterServiceType (one factory per node).
class GenericServiceFactory : public RobotRaconteur::ServiceFactory
{
public:
  explicit GenericServiceFactory(const std::string & definition_text);

  std::string GetServiceName() override;
  std::string DefString() override;
  RR_SHARED_PTR<RobotRaconteur::ServiceDefinition> ServiceDef() override;
  RR_SHARED_PTR<RobotRaconteur::StructureStub> FindStructureStub(boost::string_ref s) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackStructure(
    const RR_INTRUSIVE_PTR<RobotRaconteur::RRStructure> & structin) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::RRValue> UnpackStructure(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> & mstructin) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackPodArray(
    const RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseArray> & structure) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseArray> UnpackPodArray(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> & structure) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackPodMultiDimArray(
    const RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseMultiDimArray> & structure) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::RRPodBaseMultiDimArray> UnpackPodMultiDimArray(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> & structure) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackNamedArray(
    const RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseArray> & structure) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseArray> UnpackNamedArray(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> & structure) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> PackNamedMultiDimArray(
    const RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseMultiDimArray> & structure) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::RRNamedBaseMultiDimArray> UnpackNamedMultiDimArray(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> & structure) override;
  RR_SHARED_PTR<RobotRaconteur::ServiceStub> CreateStub(
    boost::string_ref objecttype, boost::string_ref path,
    const RR_SHARED_PTR<RobotRaconteur::ClientContext> & context) override;
  RR_SHARED_PTR<RobotRaconteur::ServiceSkel> CreateSkel(
    boost::string_ref objecttype, boost::string_ref path,
    const RR_SHARED_PTR<RobotRaconteur::RRObject> & obj,
    const RR_SHARED_PTR<RobotRaconteur::ServerContext> & context) override;
  void DownCastAndThrowException(RobotRaconteur::RobotRaconteurException & exp) override;
  RRErrorPtr DownCastException(const RRErrorPtr & exp) override;

private:
  std::string text_;
  RR_SHARED_PTR<RobotRaconteur::ServiceDefinition> def_;
};

class GenericPipeServer;
class GenericWireServer;
class GenericSkel;

/// One generator a function returned (`T{generator} f(...)`). Called on
/// Robot Raconteur threads.
class GeneratorHandler
{
public:
  using Done = std::function<void (const ElementPtr & ret, const RRErrorPtr & err)>;
  virtual ~GeneratorHandler() = default;
  /// The next value, asynchronously, exactly once; StopIterationException
  /// ends the sequence. `param` is null for a generator without one.
  virtual void next(const ElementPtr & param, Done done) = 0;
  /// The client aborted (Abort) or closed (Close) the generator.
  virtual void abort() = 0;
  virtual void close() = 0;
};

/// An object of a generated definition. Override the members its type has;
/// the defaults report the member as not found.
class GenericObject : public RobotRaconteur::RRObject
{
public:
  /// `type`: the fully qualified object type, "<definition>.<object>".
  explicit GenericObject(std::string type);
  ~GenericObject() override;

  std::string RRType() override {return type_;}

  /// Completes a function call exactly once, from any thread: the "return"
  /// element (null for void), or an error.
  using Completion = std::function<void (const ElementPtr & ret, const RRErrorPtr & err)>;

  virtual ElementPtr get_property(const std::string & name);
  /// Parameters are named by the definition's parameter names. May complete
  /// later; throwing before completing reports the error to the client.
  virtual void call_function(
    const std::string & name, const std::vector<ElementPtr> & params, Completion done);
  /// A generator function: the generator for these parameters. Must not
  /// wait (throwing reports the error to the client).
  virtual std::shared_ptr<GeneratorHandler> start_generator(
    const std::string & name, const std::vector<ElementPtr> & params);
  /// For objrefs; `index` is empty for an unindexed objref.
  virtual RR_SHARED_PTR<RobotRaconteur::RRObject> get_sub_object(
    const std::string & name, const std::string & index);

  /// The skel has made the pipe and wire servers. Called with Robot
  /// Raconteur's skel lock held: do not call back into the server context.
  virtual void attach(GenericSkel & skel);
  /// The skel releases the object (client gone or object replaced).
  virtual void detach(GenericSkel & skel);

private:
  std::string type_;
};

/// The skel of a GenericObject.
class GenericSkel : public RobotRaconteur::ServiceSkel
{
public:
  void Init(
    boost::string_ref path, const RR_SHARED_PTR<RobotRaconteur::RRObject> & object,
    const RR_SHARED_PTR<RobotRaconteur::ServerContext> & context) override;

  RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallGetProperty(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> & m) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallSetProperty(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> & m) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallFunction(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> & m) override;
  RR_SHARED_PTR<RobotRaconteur::RRObject> GetSubObj(
    boost::string_ref name, boost::string_ref ind) override;
  void ReleaseCastObject() override;
  std::string GetObjectType() override;

  void DispatchPipeMessage(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> & m, uint32_t e) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallPipeFunction(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> & m, uint32_t e) override;
  void DispatchWireMessage(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> & m, uint32_t e) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> CallWireFunction(
    const RR_INTRUSIVE_PTR<RobotRaconteur::MessageEntry> & m, uint32_t e) override;

  RR_SHARED_PTR<GenericPipeServer> pipe(const std::string & name) const;
  RR_SHARED_PTR<GenericWireServer> wire(const std::string & name) const;

private:
  RR_SHARED_PTR<GenericObject> object_;
  std::string type_;
  std::set<std::string> generator_functions_;
  std::map<std::string, RR_SHARED_PTR<GenericPipeServer>> pipes_;
  std::map<std::string, RR_SHARED_PTR<GenericWireServer>> wires_;
};

// ---------------------------------------------------------------------------
// Pipes

class GenericPipeEndpoint : public RobotRaconteur::PipeEndpointBase
{
public:
  GenericPipeEndpoint(
    const RR_SHARED_PTR<RobotRaconteur::PipeBase> & parent_arg, int32_t index_arg,
    uint32_t endpoint_arg, bool unreliable_arg,
    RobotRaconteur::MemberDefinition_Direction direction_arg);

  /// Received packets (the "packet" element), on a Robot Raconteur thread.
  /// Packets that arrived before are delivered when the callback is set.
  void set_packet_callback(std::function<void(const ElementPtr &)> cb);
  void set_closed_callback(std::function<void()> cb);
  /// Sends asynchronously; `done` runs when sent or failed.
  void async_send(const ElementPtr & packet, std::function<void()> done);

protected:
  void fire_PipeEndpointClosedCallback() override;
  void fire_PacketReceivedEvent() override;
  void fire_PacketAckReceivedEvent(uint32_t packetnum) override;

private:
  std::mutex mutex_;
  std::function<void(const ElementPtr &)> packet_cb_;
  std::function<void()> closed_cb_;
};

class GenericPipeServer : public RobotRaconteur::PipeServerBase
{
public:
  GenericPipeServer(
    const std::string & name_arg, const RR_SHARED_PTR<RobotRaconteur::ServiceSkel> & skel_arg,
    bool unreliable_arg, RobotRaconteur::MemberDefinition_Direction direction_arg);

  void set_connect_callback(std::function<void(const RR_SHARED_PTR<GenericPipeEndpoint> &)> cb);
  void Shutdown() override;

protected:
  RR_SHARED_PTR<RobotRaconteur::PipeEndpointBase> CreateNewPipeEndpoint(
    int32_t index_arg, uint32_t endpoint_arg, bool unreliable_arg,
    RobotRaconteur::MemberDefinition_Direction direction_arg) override;
  void fire_PipeConnectCallback(const RR_SHARED_PTR<RobotRaconteur::PipeEndpointBase> & e)
  override;

private:
  std::mutex mutex_;
  std::function<void(const RR_SHARED_PTR<GenericPipeEndpoint> &)> connect_cb_;
};

/// Sends each packet to every connected endpoint of a readonly pipe, each a
/// copy (Robot Raconteur's serialization writes into the elements). An
/// endpoint with `max_backlog` packets in flight skips packets.
class PipeFanout : public std::enable_shared_from_this<PipeFanout>
{
public:
  explicit PipeFanout(size_t max_backlog);
  void attach(const RR_SHARED_PTR<GenericPipeServer> & pipe);
  void send(const ElementPtr & packet);
  size_t connected();

private:
  struct Endpoint
  {
    RR_WEAK_PTR<GenericPipeEndpoint> ep;
    size_t in_flight = 0;
  };
  const size_t max_backlog_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<Endpoint>> endpoints_;
};

// ---------------------------------------------------------------------------
// Wires

class GenericWireConnection : public RobotRaconteur::WireConnectionBase
{
public:
  GenericWireConnection(
    const RR_SHARED_PTR<RobotRaconteur::WireBase> & parent_arg, uint32_t endpoint_arg,
    RobotRaconteur::MemberDefinition_Direction direction_arg);

  /// Values the client sets (the "packet" element), on a Robot Raconteur thread.
  void set_value_callback(std::function<void(const ElementPtr &)> cb);
  void set_closed_callback(std::function<void()> cb);
  /// The value the client sees (its InValue).
  void set_out_value(const ElementPtr & value);

protected:
  void fire_WireValueChanged(
    const RR_INTRUSIVE_PTR<RobotRaconteur::RRValue> & value,
    RobotRaconteur::TimeSpec time) override;
  void fire_WireClosedCallback() override;

private:
  std::mutex mutex_;
  std::function<void(const ElementPtr &)> value_cb_;
  std::function<void()> closed_cb_;
};

class GenericWireServer : public RobotRaconteur::WireServerBase
{
public:
  GenericWireServer(
    const std::string & name_arg, const RR_SHARED_PTR<RobotRaconteur::ServiceSkel> & skel_arg,
    RobotRaconteur::MemberDefinition_Direction direction_arg);

  void set_connect_callback(std::function<void(const RR_SHARED_PTR<GenericWireConnection> &)> cb);
  /// Client PeekInValue (the server's value), PeekOutValue and PokeOutValue
  /// (the client's value), without a connection. Unset: not available.
  struct PeekPoke
  {
    std::function<ElementPtr()> peek_in;
    std::function<ElementPtr()> peek_out;
    std::function<void(const ElementPtr &)> poke_out;
  };
  void set_peek_poke(PeekPoke pp);
  void Shutdown() override;
  RR_SHARED_PTR<RobotRaconteur::RobotRaconteurNode> rr_node() {return GetNode();}

protected:
  RR_SHARED_PTR<RobotRaconteur::WireConnectionBase> CreateNewWireConnection(
    uint32_t e, RobotRaconteur::MemberDefinition_Direction direction_arg) override;
  void fire_WireConnectCallback(const RR_SHARED_PTR<RobotRaconteur::WireConnectionBase> & e)
  override;
  RR_INTRUSIVE_PTR<RobotRaconteur::RRValue> do_PeekInValue(const uint32_t & ep) override;
  RR_INTRUSIVE_PTR<RobotRaconteur::RRValue> do_PeekOutValue(const uint32_t & ep) override;
  void do_PokeOutValue(
    const RR_INTRUSIVE_PTR<RobotRaconteur::RRValue> & value, const RobotRaconteur::TimeSpec & ts,
    const uint32_t & ep) override;

private:
  std::mutex mutex_;
  std::function<void(const RR_SHARED_PTR<GenericWireConnection> &)> connect_cb_;
  PeekPoke peek_poke_;
};

/// Sets the value of every connection of a readonly wire, each a copy; a
/// late connection and PeekInValue get the last value.
class WireFanout : public std::enable_shared_from_this<WireFanout>
{
public:
  void attach(const RR_SHARED_PTR<GenericWireServer> & wire);
  void set(const ElementPtr & value);
  size_t connected();

private:
  std::mutex mutex_;
  ElementPtr last_;
  std::vector<RR_WEAK_PTR<GenericWireConnection>> connections_;
};

/// Copies the element and every nested element list; array data is shared
/// (serialization does not write into it).
ElementPtr copy_element(const ElementPtr & e);

/// The struct value of a "packet"/"value"/parameter element, or null.
RR_INTRUSIVE_PTR<RobotRaconteur::MessageElementNestedElementList> element_structure(
  const ElementPtr & e);

}  // namespace robotraconteur_ros2_debug_bridge

#endif  // GENERIC_SERVICE_HPP_
