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

#include "generic_service.hpp"

#include <algorithm>
#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace robotraconteur_ros2_debug_bridge
{

namespace RR = RobotRaconteur;

namespace
{

[[noreturn]] void not_for_generic()
{
  throw RR::ServiceException("Invalid for a generic service type");
}

}  // namespace

// ---------------------------------------------------------------------------
// GenericServiceFactory

GenericServiceFactory::GenericServiceFactory(const std::string & definition_text)
: text_(definition_text), def_(RR_MAKE_SHARED<RR::ServiceDefinition>())
{
  def_->FromString(text_);
}

std::string GenericServiceFactory::GetServiceName() {return def_->Name;}
std::string GenericServiceFactory::DefString() {return text_;}
RR_SHARED_PTR<RR::ServiceDefinition> GenericServiceFactory::ServiceDef() {return def_;}

// Structures travel packed (value_converter.hpp): no boxing here.
RR_SHARED_PTR<RR::StructureStub> GenericServiceFactory::FindStructureStub(boost::string_ref)
{
  not_for_generic();
}
RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> GenericServiceFactory::PackStructure(
  const RR_INTRUSIVE_PTR<RR::RRStructure> &) {not_for_generic();}
RR_INTRUSIVE_PTR<RR::RRValue> GenericServiceFactory::UnpackStructure(
  const RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> &) {not_for_generic();}
RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> GenericServiceFactory::PackPodArray(
  const RR_INTRUSIVE_PTR<RR::RRPodBaseArray> &) {not_for_generic();}
RR_INTRUSIVE_PTR<RR::RRPodBaseArray> GenericServiceFactory::UnpackPodArray(
  const RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> &) {not_for_generic();}
RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> GenericServiceFactory::PackPodMultiDimArray(
  const RR_INTRUSIVE_PTR<RR::RRPodBaseMultiDimArray> &) {not_for_generic();}
RR_INTRUSIVE_PTR<RR::RRPodBaseMultiDimArray> GenericServiceFactory::UnpackPodMultiDimArray(
  const RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> &) {not_for_generic();}
RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> GenericServiceFactory::PackNamedArray(
  const RR_INTRUSIVE_PTR<RR::RRNamedBaseArray> &) {not_for_generic();}
RR_INTRUSIVE_PTR<RR::RRNamedBaseArray> GenericServiceFactory::UnpackNamedArray(
  const RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> &) {not_for_generic();}
RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList>
GenericServiceFactory::PackNamedMultiDimArray(
  const RR_INTRUSIVE_PTR<RR::RRNamedBaseMultiDimArray> &) {not_for_generic();}
RR_INTRUSIVE_PTR<RR::RRNamedBaseMultiDimArray> GenericServiceFactory::UnpackNamedMultiDimArray(
  const RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> &) {not_for_generic();}

RR_SHARED_PTR<RR::ServiceStub> GenericServiceFactory::CreateStub(
  boost::string_ref, boost::string_ref, const RR_SHARED_PTR<RR::ClientContext> &)
{
  throw RR::ServiceException("Generic service types are server only");
}

RR_SHARED_PTR<RR::ServiceSkel> GenericServiceFactory::CreateSkel(
  boost::string_ref objecttype, boost::string_ref path, const RR_SHARED_PTR<RR::RRObject> & obj,
  const RR_SHARED_PTR<RR::ServerContext> & context)
{
  const auto parts = RR::SplitQualifiedName(objecttype);
  if (parts.get<0>() != GetServiceName()) {
    return GetNode()->GetServiceType(parts.get<0>())->CreateSkel(objecttype, path, obj, context);
  }
  for (const auto & e : def_->Objects) {
    if (e->Name == parts.get<1>()) {
      auto skel = RR_MAKE_SHARED<GenericSkel>();
      skel->Init(path, obj, context);  // after make_shared: Init uses shared_from_this
      return skel;
    }
  }
  throw RR::ServiceException("Invalid service skel type " + objecttype.to_string());
}

void GenericServiceFactory::DownCastAndThrowException(RR::RobotRaconteurException & exp)
{
  throw exp;
}

RRErrorPtr GenericServiceFactory::DownCastException(const RRErrorPtr & exp) {return exp;}

// ---------------------------------------------------------------------------
// GenericObject

GenericObject::GenericObject(std::string type)
: type_(std::move(type)) {}
GenericObject::~GenericObject() = default;

ElementPtr GenericObject::get_property(const std::string & name)
{
  throw RR::MemberNotFoundException("Member " + name + " not found");
}

void GenericObject::call_function(
  const std::string & name, const std::vector<ElementPtr> &, Completion)
{
  throw RR::MemberNotFoundException("Member " + name + " not found");
}

RR_SHARED_PTR<RR::RRObject> GenericObject::get_sub_object(
  const std::string & name,
  const std::string &)
{
  throw RR::MemberNotFoundException("Member " + name + " not found");
}

void GenericObject::attach(GenericSkel &) {}
void GenericObject::detach(GenericSkel &) {}

std::shared_ptr<GeneratorHandler> GenericObject::start_generator(
  const std::string & name, const std::vector<ElementPtr> &)
{
  throw RR::MemberNotFoundException("Member " + name + " not found");
}

namespace
{

// A generator a GenericObject's function returned.
class GenericGeneratorServer : public RR::GeneratorServerBase
{
public:
  GenericGeneratorServer(
    const std::string & name_arg, int32_t index_arg,
    const RR_SHARED_PTR<RR::ServiceSkel> & skel_arg,
    const RR_SHARED_PTR<RR::ServerEndpoint> & ep_arg, std::shared_ptr<GeneratorHandler> handler)
  : GeneratorServerBase(name_arg, index_arg, skel_arg, ep_arg), handler_(std::move(handler)) {}

  void CallNext(const RR_INTRUSIVE_PTR<RR::MessageEntry> & entry) override
  {
    const RR_WEAK_PTR<RR::ServiceSkel> s = skel;
    const int32_t idx = index;
    const auto endpoint = ep;
    if (entry->Error != RR::MessageErrorType_None) {
      // The client closed (StopIteration) or aborted the generator.
      if (entry->Error == RR::MessageErrorType_StopIteration) {
        handler_->close();
      } else {
        handler_->abort();
      }
      EndAsyncCallNext(
        s, RR::CreateMessageElement("return", RR::ScalarToRRArray<int32_t>(0)), RRErrorPtr(), idx,
        entry, endpoint);
      return;
    }
    ElementPtr param;
    entry->TryFindElement("parameter", param);
    auto once = std::make_shared<std::atomic<bool>>(false);
    handler_->next(
      param, [s, idx, entry, endpoint, once](const ElementPtr & ret, const RRErrorPtr & err) {
        if (once->exchange(true)) {
          return;
        }
        if (ret) {
          ret->ElementName = "return";
        }
        EndAsyncCallNext(s, ret, err, idx, entry, endpoint);
      });
  }

private:
  std::shared_ptr<GeneratorHandler> handler_;
};

}  // namespace

// ---------------------------------------------------------------------------
// GenericSkel

void GenericSkel::Init(
  boost::string_ref path, const RR_SHARED_PTR<RR::RRObject> & object,
  const RR_SHARED_PTR<RR::ServerContext> & context)
{
  object_ = RR_DYNAMIC_POINTER_CAST<GenericObject>(object);
  if (!object_) {
    throw RR::ServiceException("Not a generic object");
  }
  type_ = object_->RRType();
  const auto parts = RR::SplitQualifiedName(type_);
  RR_SHARED_PTR<RR::ServiceEntryDefinition> entry;
  for (const auto & e : context->GetNode()->GetServiceType(parts.get<0>())->ServiceDef()->Objects) {
    if (e->Name == parts.get<1>()) {
      entry = e;
    }
  }
  if (!entry) {
    throw RR::ServiceException("Unknown object type " + type_);
  }
  ServiceSkel::Init(path, object, context);
  auto self = shared_from_this();
  for (const auto & m : entry->Members) {
    if (auto p = RR_DYNAMIC_POINTER_CAST<RR::PipeDefinition>(m)) {
      pipes_[p->Name] =
        RR_MAKE_SHARED<GenericPipeServer>(p->Name, self, p->IsUnreliable(), p->Direction());
    } else if (auto w = RR_DYNAMIC_POINTER_CAST<RR::WireDefinition>(m)) {
      wires_[w->Name] = RR_MAKE_SHARED<GenericWireServer>(w->Name, self, w->Direction());
    } else if (auto f = RR_DYNAMIC_POINTER_CAST<RR::FunctionDefinition>(m)) {
      if (f->IsGenerator()) {
        generator_functions_.insert(f->Name);
      }
    }
  }
  object_->attach(*this);
}

RR_INTRUSIVE_PTR<RR::MessageEntry> GenericSkel::CallGetProperty(
  const RR_INTRUSIVE_PTR<RR::MessageEntry> & m)
{
  ElementPtr value = object_->get_property(m->MemberName.str().to_string());
  value->ElementName = "value";
  auto res = RR::CreateMessageEntry(RR::MessageEntryType_PropertyGetRes, m->MemberName);
  res->AddElement(value);
  return res;
}

RR_INTRUSIVE_PTR<RR::MessageEntry> GenericSkel::CallSetProperty(
  const RR_INTRUSIVE_PTR<RR::MessageEntry> & m)
{
  throw RR::ReadOnlyMemberException("Member " + m->MemberName.str().to_string() + " is read only");
}

RR_INTRUSIVE_PTR<RR::MessageEntry> GenericSkel::CallFunction(
  const RR_INTRUSIVE_PTR<RR::MessageEntry> & m)
{
  const std::string name = m->MemberName.str().to_string();
  if (generator_functions_.count(name) != 0) {
    // The generator now; its values come through Next (asynchronous).
    auto handler = object_->start_generator(name, m->elements);
    int32_t index = 0;
    {
      boost::mutex::scoped_lock lock(generators_lock);
      index = get_new_generator_index();
      generators.insert(
        std::make_pair(
          index, RR_MAKE_SHARED<GenericGeneratorServer>(
            name, index, shared_from_this(), RR::ServerEndpoint::GetCurrentEndpoint(), handler)));
    }
    auto res = RR::CreateMessageEntry(RR::MessageEntryType_FunctionCallRes, m->MemberName);
    res->AddElement("index", RR::ScalarToRRArray<int32_t>(index));
    return res;
  }
  // Always completed asynchronously (EndAsyncCallFunction), so no Robot
  // Raconteur thread waits on ROS.
  auto done_flag = std::make_shared<std::atomic<bool>>(false);
  RR_WEAK_PTR<RR::ServiceSkel> weak = shared_from_this();
  auto ep = RR::ServerEndpoint::GetCurrentEndpoint();
  GenericObject::Completion done =
    [weak, m, ep, done_flag](const ElementPtr & ret, const RRErrorPtr & err) {
      if (done_flag->exchange(true)) {
        return;
      }
      EndAsyncCallFunction(weak, ret, err, m, ep);
    };
  try {
    object_->call_function(m->MemberName.str().to_string(), m->elements, done);
  } catch (...) {
    if (!done_flag->exchange(true)) {
      throw;  // not completed: the error goes to the client now
    }
  }
  return RR_INTRUSIVE_PTR<RR::MessageEntry>();
}

RR_SHARED_PTR<RR::RRObject> GenericSkel::GetSubObj(boost::string_ref name, boost::string_ref ind)
{
  return object_->get_sub_object(name.to_string(), ind.to_string());
}

void GenericSkel::ReleaseCastObject()
{
  for (auto & p : pipes_) {
    p.second->Shutdown();
  }
  for (auto & w : wires_) {
    w.second->Shutdown();
  }
  if (object_) {
    object_->detach(*this);
  }
}

std::string GenericSkel::GetObjectType() {return type_;}

void GenericSkel::DispatchPipeMessage(const RR_INTRUSIVE_PTR<RR::MessageEntry> & m, uint32_t e)
{
  const auto it = pipes_.find(m->MemberName.str().to_string());
  if (it == pipes_.end()) {
    throw RR::MemberNotFoundException("Pipe member not found");
  }
  it->second->PipePacketReceived(m, e);
}

RR_INTRUSIVE_PTR<RR::MessageEntry> GenericSkel::CallPipeFunction(
  const RR_INTRUSIVE_PTR<RR::MessageEntry> & m, uint32_t e)
{
  const auto it = pipes_.find(m->MemberName.str().to_string());
  if (it == pipes_.end()) {
    throw RR::MemberNotFoundException("Pipe member not found");
  }
  return it->second->PipeCommand(m, e);
}

void GenericSkel::DispatchWireMessage(const RR_INTRUSIVE_PTR<RR::MessageEntry> & m, uint32_t e)
{
  const auto it = wires_.find(m->MemberName.str().to_string());
  if (it == wires_.end()) {
    throw RR::MemberNotFoundException("Wire member not found");
  }
  it->second->WirePacketReceived(m, e);
}

RR_INTRUSIVE_PTR<RR::MessageEntry> GenericSkel::CallWireFunction(
  const RR_INTRUSIVE_PTR<RR::MessageEntry> & m, uint32_t e)
{
  const auto it = wires_.find(m->MemberName.str().to_string());
  if (it == wires_.end()) {
    throw RR::MemberNotFoundException("Wire member not found");
  }
  return it->second->WireCommand(m, e);
}

RR_SHARED_PTR<GenericPipeServer> GenericSkel::pipe(const std::string & name) const
{
  const auto it = pipes_.find(name);
  return it == pipes_.end() ? RR_SHARED_PTR<GenericPipeServer>() : it->second;
}

RR_SHARED_PTR<GenericWireServer> GenericSkel::wire(const std::string & name) const
{
  const auto it = wires_.find(name);
  return it == wires_.end() ? RR_SHARED_PTR<GenericWireServer>() : it->second;
}

// ---------------------------------------------------------------------------
// Pipes

GenericPipeEndpoint::GenericPipeEndpoint(
  const RR_SHARED_PTR<RR::PipeBase> & parent_arg, int32_t index_arg, uint32_t endpoint_arg,
  bool unreliable_arg, RR::MemberDefinition_Direction direction_arg)
: PipeEndpointBase(parent_arg, index_arg, endpoint_arg, unreliable_arg, direction_arg) {}

void GenericPipeEndpoint::set_packet_callback(std::function<void(const ElementPtr &)> cb)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    packet_cb_ = std::move(cb);
  }
  fire_PacketReceivedEvent();  // packets that arrived before
}

void GenericPipeEndpoint::set_closed_callback(std::function<void()> cb)
{
  std::lock_guard<std::mutex> lock(mutex_);
  closed_cb_ = std::move(cb);
}

void GenericPipeEndpoint::async_send(const ElementPtr & packet, std::function<void()> done)
{
  AsyncSendPacketBase(
    packet, [done](uint32_t, const RRErrorPtr &) {
      if (done) {
        done();
      }
    });
}

void GenericPipeEndpoint::fire_PipeEndpointClosedCallback()
{
  std::function<void()> cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cb = closed_cb_;
  }
  if (cb) {
    cb();
  }
}

void GenericPipeEndpoint::fire_PacketReceivedEvent()
{
  std::function<void(const ElementPtr &)> cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cb = packet_cb_;
  }
  if (!cb || Direction() == RR::MemberDefinition_Direction_writeonly) {
    return;  // kept queued until a callback is set
  }
  RR_INTRUSIVE_PTR<RR::RRValue> v;
  while (TryReceivePacketBaseWait(v, 0, false)) {
    cb(RR::rr_cast<RR::MessageElement>(v));
  }
}

void GenericPipeEndpoint::fire_PacketAckReceivedEvent(uint32_t) {}

GenericPipeServer::GenericPipeServer(
  const std::string & name_arg, const RR_SHARED_PTR<RR::ServiceSkel> & skel_arg,
  bool unreliable_arg, RR::MemberDefinition_Direction direction_arg)
: PipeServerBase(name_arg, skel_arg, unreliable_arg, direction_arg)
{
  rawelements = true;  // packets stay MessageElements
}

void GenericPipeServer::set_connect_callback(
  std::function<void(const RR_SHARED_PTR<GenericPipeEndpoint> &)> cb)
{
  std::lock_guard<std::mutex> lock(mutex_);
  connect_cb_ = std::move(cb);
}

void GenericPipeServer::Shutdown()
{
  PipeServerBase::Shutdown();
  std::lock_guard<std::mutex> lock(mutex_);
  connect_cb_ = nullptr;
}

RR_SHARED_PTR<RR::PipeEndpointBase> GenericPipeServer::CreateNewPipeEndpoint(
  int32_t index_arg, uint32_t endpoint_arg, bool unreliable_arg,
  RR::MemberDefinition_Direction direction_arg)
{
  return RR_MAKE_SHARED<GenericPipeEndpoint>(
    RR_STATIC_POINTER_CAST<RR::PipeBase>(shared_from_this()), index_arg, endpoint_arg,
    unreliable_arg, direction_arg);
}

void GenericPipeServer::fire_PipeConnectCallback(const RR_SHARED_PTR<RR::PipeEndpointBase> & e)
{
  std::function<void(const RR_SHARED_PTR<GenericPipeEndpoint> &)> cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cb = connect_cb_;
  }
  if (cb) {
    cb(RR_STATIC_POINTER_CAST<GenericPipeEndpoint>(e));
  }
}

PipeFanout::PipeFanout(size_t max_backlog)
: max_backlog_(max_backlog) {}

void PipeFanout::attach(const RR_SHARED_PTR<GenericPipeServer> & pipe)
{
  std::weak_ptr<PipeFanout> weak = shared_from_this();
  pipe->set_connect_callback(
    [weak](const RR_SHARED_PTR<GenericPipeEndpoint> & ep) {
      auto self = weak.lock();
      if (!self) {
        return;
      }
      auto entry = std::make_shared<Endpoint>();
      entry->ep = ep;
      {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->endpoints_.push_back(entry);
      }
      ep->set_closed_callback(
        [weak, entry]() {
          if (auto s = weak.lock()) {
            std::lock_guard<std::mutex> lock(s->mutex_);
            auto & v = s->endpoints_;
            v.erase(std::remove(v.begin(), v.end(), entry), v.end());
          }
        });
    });
}

void PipeFanout::send(const ElementPtr & packet)
{
  std::vector<std::pair<std::shared_ptr<Endpoint>, RR_SHARED_PTR<GenericPipeEndpoint>>> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto & e : endpoints_) {
      auto ep = e->ep.lock();
      if (ep && e->in_flight < max_backlog_) {
        ++e->in_flight;
        targets.emplace_back(e, ep);
      }
    }
  }
  std::weak_ptr<PipeFanout> weak = shared_from_this();
  for (auto & t : targets) {
    auto entry = t.first;
    try {
      t.second->async_send(
        copy_element(packet), [weak, entry]() {
          if (auto s = weak.lock()) {
            std::lock_guard<std::mutex> lock(s->mutex_);
            --entry->in_flight;
          }
        });
    } catch (const std::exception &) {
      std::lock_guard<std::mutex> lock(mutex_);
      --entry->in_flight;  // closed meanwhile
    }
  }
}

size_t PipeFanout::connected()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return endpoints_.size();
}

// ---------------------------------------------------------------------------
// Wires

GenericWireConnection::GenericWireConnection(
  const RR_SHARED_PTR<RR::WireBase> & parent_arg, uint32_t endpoint_arg,
  RR::MemberDefinition_Direction direction_arg)
: WireConnectionBase(parent_arg, endpoint_arg, direction_arg) {}

void GenericWireConnection::set_value_callback(std::function<void(const ElementPtr &)> cb)
{
  std::lock_guard<std::mutex> lock(mutex_);
  value_cb_ = std::move(cb);
}

void GenericWireConnection::set_closed_callback(std::function<void()> cb)
{
  std::lock_guard<std::mutex> lock(mutex_);
  closed_cb_ = std::move(cb);
}

void GenericWireConnection::set_out_value(const ElementPtr & value) {SetOutValueBase(value);}

void GenericWireConnection::fire_WireValueChanged(
  const RR_INTRUSIVE_PTR<RR::RRValue> & value, RR::TimeSpec)
{
  std::function<void(const ElementPtr &)> cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cb = value_cb_;
  }
  if (cb) {
    cb(RR::rr_cast<RR::MessageElement>(value));
  }
}

void GenericWireConnection::fire_WireClosedCallback()
{
  std::function<void()> cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cb = closed_cb_;
  }
  if (cb) {
    cb();
  }
}

GenericWireServer::GenericWireServer(
  const std::string & name_arg, const RR_SHARED_PTR<RR::ServiceSkel> & skel_arg,
  RR::MemberDefinition_Direction direction_arg)
: WireServerBase(name_arg, skel_arg, direction_arg)
{
  rawelements = true;
}

void GenericWireServer::set_connect_callback(
  std::function<void(const RR_SHARED_PTR<GenericWireConnection> &)> cb)
{
  std::lock_guard<std::mutex> lock(mutex_);
  connect_cb_ = std::move(cb);
}

void GenericWireServer::set_peek_poke(PeekPoke pp)
{
  std::lock_guard<std::mutex> lock(mutex_);
  peek_poke_ = std::move(pp);
}

void GenericWireServer::Shutdown()
{
  WireServerBase::Shutdown();
  std::lock_guard<std::mutex> lock(mutex_);
  connect_cb_ = nullptr;
  peek_poke_ = PeekPoke();
}

RR_SHARED_PTR<RR::WireConnectionBase> GenericWireServer::CreateNewWireConnection(
  uint32_t e, RR::MemberDefinition_Direction direction_arg)
{
  return RR_MAKE_SHARED<GenericWireConnection>(
    RR_STATIC_POINTER_CAST<RR::WireBase>(shared_from_this()), e, direction_arg);
}

void GenericWireServer::fire_WireConnectCallback(const RR_SHARED_PTR<RR::WireConnectionBase> & e)
{
  std::function<void(const RR_SHARED_PTR<GenericWireConnection> &)> cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cb = connect_cb_;
  }
  if (cb) {
    cb(RR_STATIC_POINTER_CAST<GenericWireConnection>(e));
  }
}

RR_INTRUSIVE_PTR<RR::RRValue> GenericWireServer::do_PeekInValue(const uint32_t &)
{
  std::function<ElementPtr()> f;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    f = peek_poke_.peek_in;
  }
  if (!f) {
    throw RR::InvalidOperationException("PeekInValue is not available");
  }
  return f();
}

RR_INTRUSIVE_PTR<RR::RRValue> GenericWireServer::do_PeekOutValue(const uint32_t &)
{
  std::function<ElementPtr()> f;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    f = peek_poke_.peek_out;
  }
  if (!f) {
    throw RR::InvalidOperationException("PeekOutValue is not available");
  }
  return f();
}

void GenericWireServer::do_PokeOutValue(
  const RR_INTRUSIVE_PTR<RR::RRValue> & value, const RR::TimeSpec &, const uint32_t &)
{
  std::function<void(const ElementPtr &)> f;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    f = peek_poke_.poke_out;
  }
  if (!f) {
    throw RR::InvalidOperationException("PokeOutValue is not available");
  }
  f(RR::rr_cast<RR::MessageElement>(value));
}

void WireFanout::attach(const RR_SHARED_PTR<GenericWireServer> & wire)
{
  std::weak_ptr<WireFanout> weak = shared_from_this();
  RR_WEAK_PTR<GenericWireServer> weak_wire = wire;
  wire->set_connect_callback(
    [weak, weak_wire](const RR_SHARED_PTR<GenericWireConnection> & c) {
      auto self = weak.lock();
      if (!self) {
        return;
      }
      ElementPtr last;
      {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->connections_.push_back(c);
        last = self->last_;
      }
      RR_WEAK_PTR<GenericWireConnection> wc = c;
      // The client creates its connection only when the connect response
      // arrives, which is sent after this callback, and drops wire packets
      // that come earlier (RR master 7cd1610c; RR's WireBroadcaster sends
      // here too). So send the current value again shortly after.
      if (auto w = weak_wire.lock()) {
        if (auto n = w->rr_node()) {
          // A started timer keeps itself alive until it fires or the node
          // stops (RR's WallTimer binds shared_from_this() into its wait);
          // the handler must not own the timer (a cycle that outlives a
          // node stopped before it fires: LeakSanitizer).
          for (int ms : {50, 1000}) {
            auto timer = n->CreateTimer(
              boost::posix_time::milliseconds(ms),
              [weak, wc](const RR::TimerEvent & ev) {
                if (ev.stopped) {
                  return;
                }
                auto s = weak.lock();
                auto conn = wc.lock();
                ElementPtr value;
                if (s) {
                  std::lock_guard<std::mutex> lock(s->mutex_);
                  value = s->last_;
                }
                if (conn && value) {
                  try {
                    conn->set_out_value(copy_element(value));
                  } catch (const std::exception &) {
                  }
                }
              }, true);
            timer->Start();
          }
        }
      }
      c->set_closed_callback(
        [weak, wc]() {
          if (auto s = weak.lock()) {
            std::lock_guard<std::mutex> lock(s->mutex_);
            auto & v = s->connections_;
            v.erase(
              std::remove_if(
                v.begin(), v.end(), [&wc](const RR_WEAK_PTR<GenericWireConnection> & x) {
                  return !x.owner_before(wc) && !wc.owner_before(x);
                }), v.end());
          }
        });
      if (last) {
        try {
          c->set_out_value(copy_element(last));
        } catch (const std::exception &) {
        }
      }
    });
  GenericWireServer::PeekPoke pp;
  pp.peek_in = [weak]() -> ElementPtr {
      auto s = weak.lock();
      if (!s) {
        throw RR::InvalidOperationException("Wire closed");
      }
      std::lock_guard<std::mutex> lock(s->mutex_);
      if (!s->last_) {
        throw RR::ValueNotSetException("Value not set");
      }
      return copy_element(s->last_);
    };
  wire->set_peek_poke(std::move(pp));
}

void WireFanout::set(const ElementPtr & value)
{
  std::vector<RR_SHARED_PTR<GenericWireConnection>> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_ = value;
    for (const auto & w : connections_) {
      if (auto c = w.lock()) {
        targets.push_back(c);
      }
    }
  }
  for (auto & c : targets) {
    try {
      c->set_out_value(copy_element(value));
    } catch (const std::exception &) {
      // closed meanwhile
    }
  }
}

size_t WireFanout::connected()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return connections_.size();
}

ElementPtr copy_element(const ElementPtr & e)
{
  if (!e) {
    return e;
  }
  auto out = RR::ShallowCopyMessageElement(e);
  // Structures, lists, maps and multidimensional arrays: nested element lists.
  RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> list(
    dynamic_cast<RR::MessageElementNestedElementList *>(e->GetData().get()));
  if (list) {
    std::vector<ElementPtr> items;
    items.reserve(list->Elements.size());
    for (const auto & i : list->Elements) {
      items.push_back(copy_element(i));
    }
    out->SetData(
      RR::CreateMessageElementNestedElementList(list->Type, list->TypeName, std::move(items)));
  }
  return out;
}

RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList> element_structure(const ElementPtr & e)
{
  if (!e || e->ElementType != RR::DataTypes_structure_t) {
    return RR_INTRUSIVE_PTR<RR::MessageElementNestedElementList>();
  }
  return e->CastDataToNestedList();
}

}  // namespace robotraconteur_ros2_debug_bridge
