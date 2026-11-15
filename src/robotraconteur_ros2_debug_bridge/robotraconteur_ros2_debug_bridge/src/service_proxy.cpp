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

#include "service_proxy.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "proxy_executor.hpp"
#include "rclcpp/create_generic_client.hpp"
#include "rclcpp/generic_client.hpp"
#include "rcl/service.h"
#include "rosidl_typesupport_introspection_cpp/service_introspection.hpp"
#include "type_mapper.hpp"
#include "value_converter.hpp"

namespace robotraconteur_ros2_debug_bridge
{

namespace RR = RobotRaconteur;
namespace ti = rosidl_typesupport_introspection_cpp;

namespace
{

// A call that has had no response by then fails (the ROS server may be
// gone); the client's own request timeout is usually shorter.
constexpr std::chrono::seconds kCallDeadline{60};


RRErrorPtr error(const std::string & what)
{
  return RR_MAKE_SHARED<RR::OperationFailedException>(what);
}

ElementPtr bool_element(bool v)
{
  return RR::CreateMessageElement("return", RR::ScalarToRRArray<RR::rr_bool>(RR::rr_bool(v)));
}

struct ServiceTypes
{
  const ti::MessageMembers * request;
  const ti::MessageMembers * response;
  std::string request_struct;   // rosservice_<pkg>__<Srv>.<Srv>Request
  std::string response_struct;
};

ServiceTypes service_types(
  const std::string & definition, const std::string & type,
  const void * members)
{
  const auto * s = static_cast<const ti::ServiceMembers *>(members);
  const auto t = parse_interface_type(type, InterfaceKind::Service);
  return ServiceTypes{
    s->request_members_, s->response_members_,
    definition + "." + fixname(t.name + "Request"),
    definition + "." + fixname(t.name + "Response")};
}

// ---------------------------------------------------------------------------

class ClientObject : public ProxyObject, public RR_ENABLE_SHARED_FROM_THIS<ClientObject>
{
public:
  ClientObject(
    const std::string & definition, std::string service, const std::string & type,
    const void * members)
  : ProxyObject(definition + ".rosclient"), service_(std::move(service)), type_(type),
    types_(service_types(definition, type, members)) {}

  ~ClientObject() override {close();}

  void start(rclcpp::Node & node, const std::shared_ptr<ProxyExecutor> & proxy)
  {
    proxy_ = proxy;
    auto client = proxy->run(
      [&]() {
        return node.create_generic_client(
          service_, type_, rclcpp::ServicesQoS(), proxy->group());
      });
    std::lock_guard<std::mutex> lock(mutex_);
    client_ = client;
  }

  void call_function(
    const std::string & name, const std::vector<ElementPtr> & params, Completion done) override
  {
    if (name == "call") {
      call(params, done);
    } else if (name == "wait_for_service") {
      wait_for_service(params, done);
    } else {
      GenericObject::call_function(name, params, done);
    }
  }

  void close() override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    destroy_on_proxy_thread(proxy_, std::move(client_));
  }

private:
  static ElementPtr param(const std::vector<ElementPtr> & params, const char * name)
  {
    for (const auto & p : params) {
      if (p->ElementName.str() == name) {
        return p;
      }
    }
    throw RR::InvalidArgumentException(std::string("parameter ") + name + " missing");
  }

  // The client, on the proxy thread (null once closed).
  rclcpp::GenericClient::SharedPtr client()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return client_;
  }

  void call(const std::vector<ElementPtr> & params, const Completion & done)
  {
    const auto s = element_structure(param(params, "request"));
    if (!s) {
      throw RR::InvalidArgumentException("request: expected a " + types_.request_struct);
    }
    auto request = std::make_shared<DynamicMessage>(Flavor::Cpp, types_.request);
    try {
      rr_to_ros(Flavor::Cpp, types_.request, s, request->data(), types_.request_struct);
    } catch (const ConversionError & e) {
      throw RR::InvalidArgumentException(service_ + ": " + e.what());
    }
    RR_WEAK_PTR<ClientObject> weak = shared_from_this();
    auto proxy = proxy_;
    proxy->post(
      [weak, request, done, proxy]() {
        auto self = weak.lock();
        auto c = self ? self->client() : nullptr;
        if (!c) {
          done(ElementPtr(), error("client closed"));
          return;
        }
        if (!c->service_is_ready()) {
          done(ElementPtr(), error(self->service_ + ": service not available"));
          return;
        }
        auto sent = std::make_shared<rclcpp::GenericClient::FutureAndRequestId>(
          c->async_send_request(request->data()));
        const auto deadline = std::chrono::steady_clock::now() + kCallDeadline;
        proxy->add_poller(
          [weak, sent, done, deadline, proxy]() {
            auto o = weak.lock();
            if (!o || proxy->stopping()) {
              done(ElementPtr(), error("client closed"));
              return true;
            }
            if (sent->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
              std::shared_ptr<void> response = sent->future.get();
              try {
                done(
                  RR::CreateMessageElement(
                    "return", ros_to_rr(
                      Flavor::Cpp, o->types_.response, response.get(),
                      o->types_.response_struct)), RRErrorPtr());
              } catch (const std::exception & e) {
                done(ElementPtr(), error(e.what()));
              }
              return true;
            }
            if (std::chrono::steady_clock::now() > deadline) {
              if (auto c2 = o->client()) {
                c2->remove_pending_request(sent->request_id);
              }
              done(ElementPtr(), RR_MAKE_SHARED<RR::RequestTimeoutException>(
                o->service_ + ": no response"));
              return true;
            }
            return false;
          });
      });
  }

  void wait_for_service(const std::vector<ElementPtr> & params, const Completion & done)
  {
    const auto t = param(params, "timeout");
    if (t->ElementType != RR::DataTypes_double_t) {
      throw RR::InvalidArgumentException("timeout: expected a double");
    }
    const double seconds = RR::RRArrayToScalar(t->CastData<RR::RRArray<double>>());
    const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(seconds < 0 ? 0 : seconds));
    RR_WEAK_PTR<ClientObject> weak = shared_from_this();
    auto proxy = proxy_;
    proxy->add_poller(
      [weak, done, deadline, proxy]() {
        auto o = weak.lock();
        auto c = o ? o->client() : nullptr;
        if (!c || proxy->stopping()) {
          done(ElementPtr(), error("client closed"));
          return true;
        }
        if (c->service_is_ready()) {
          done(bool_element(true), RRErrorPtr());
          return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
          done(bool_element(false), RRErrorPtr());
          return true;
        }
        return false;
      });
  }

  const std::string service_;
  const std::string type_;
  const ServiceTypes types_;
  std::shared_ptr<ProxyExecutor> proxy_;
  std::mutex mutex_;
  rclcpp::GenericClient::SharedPtr client_;
};

// ---------------------------------------------------------------------------

// A ROS service server for a type known only at run time (rclcpp Jazzy has
// no generic service): requests are C++ message memory from introspection.
class DynamicService : public rclcpp::ServiceBase
{
public:
  /// The request is a DynamicMessage's data (kept alive by the pointer).
  using Handler = std::function<void (
        std::shared_ptr<rmw_request_id_t>, std::shared_ptr<void>)>;

  DynamicService(
    std::shared_ptr<rcl_node_t> node_handle, const std::string & name,
    const rosidl_service_type_support_t * handle, const ti::MessageMembers * request_members,
    Handler handler)
  : rclcpp::ServiceBase(node_handle), request_members_(request_members),
    handler_(std::move(handler))
  {
    service_handle_ = std::shared_ptr<rcl_service_t>(
      new rcl_service_t, [h = node_handle_](rcl_service_t * service) {
        if (rcl_service_fini(service, h.get()) != RCL_RET_OK) {
          rcl_reset_error();
        }
        delete service;
      });
    *service_handle_ = rcl_get_zero_initialized_service();
    rcl_service_options_t options = rcl_service_get_default_options();
    options.qos = rclcpp::ServicesQoS().get_rmw_qos_profile();
    const rcl_ret_t ret =
      rcl_service_init(service_handle_.get(), node_handle.get(), handle, name.c_str(), &options);
    if (ret != RCL_RET_OK) {
      rclcpp::exceptions::throw_from_rcl_error(ret, "could not create service");
    }
  }

  std::shared_ptr<void> create_request() override
  {
    auto m = std::make_shared<DynamicMessage>(Flavor::Cpp, request_members_);
    return std::shared_ptr<void>(m, m->data());  // owns m
  }

  std::shared_ptr<rmw_request_id_t> create_request_header() override
  {
    return std::make_shared<rmw_request_id_t>();
  }

  void handle_request(
    std::shared_ptr<rmw_request_id_t> header, std::shared_ptr<void> request) override
  {
    handler_(std::move(header), std::move(request));
  }

  /// On the proxy thread.
  void send_response(rmw_request_id_t & header, void * response)
  {
    if (rcl_send_response(service_handle_.get(), &header, response) != RCL_RET_OK) {
      rcl_reset_error();  // for example the client is gone
    }
  }

private:
  const ti::MessageMembers * request_members_;
  Handler handler_;
};

class ServerObject : public ProxyObject, public RR_ENABLE_SHARED_FROM_THIS<ServerObject>
{
public:
  ServerObject(
    const std::string & definition, std::string service, const std::string & type,
    const void * members, uint32_t owner, rclcpp::Logger logger)
  : ProxyObject(definition + ".rosservice"), service_(std::move(service)), type_(type),
    types_(service_types(definition, type, members)), owner_(owner), logger_(std::move(logger))
  {}

  ~ServerObject() override {close();}

  void start(
    rclcpp::Node & node, const rosidl_service_type_support_t * handle,
    const std::shared_ptr<ProxyExecutor> & proxy)
  {
    proxy_ = proxy;
    RR_WEAK_PTR<ServerObject> weak = shared_from_this();
    auto service = proxy->run(
      [&]() {
        auto s = std::make_shared<DynamicService>(
          node.get_node_base_interface()->get_shared_rcl_node_handle(), service_, handle,
          types_.request,
          [weak](std::shared_ptr<rmw_request_id_t> header, std::shared_ptr<void> request) {
            if (auto o = weak.lock()) {
              o->on_request(std::move(header), std::move(request));
            }
          });
        node.get_node_services_interface()->add_service(s, proxy->group());
        return s;
      });
    std::lock_guard<std::mutex> lock(mutex_);
    service_handle_ = service;
  }

  void attach(GenericSkel & skel) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    skel_ = RR_STATIC_POINTER_CAST<GenericSkel>(skel.shared_from_this());
  }

  void close() override
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      destroy_on_proxy_thread(proxy_, std::move(service_handle_));
    }
    std::thread worker;
    {
      std::lock_guard<std::mutex> lock(jobs_mutex_);
      worker_stop_ = true;
      worker.swap(worker_);
    }
    jobs_cv_.notify_all();
    if (worker.joinable()) {
      if (worker.get_id() == std::this_thread::get_id()) {
        worker.detach();  // the last reference went on the worker itself
      } else {
        worker.join();
      }
    }
  }

private:
  // Robot Raconteur master's AsyncProcessCallbackRequest completes every
  // request at once with neither a result nor an error (its send handler,
  // ServerContext::AsyncProcessCallbackRequest_err, does not check for a
  // null error; docs/upstream_issues.md). So the owner's servicefunction is
  // called with the blocking ProcessCallbackRequest, on this service's
  // worker thread: neither a Robot Raconteur nor a ROS thread waits.
  void worker_loop()
  {
    for (;; ) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lock(jobs_mutex_);
        jobs_cv_.wait(lock, [this]() {return worker_stop_ || !jobs_.empty();});
        if (worker_stop_) {
          return;
        }
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      job();
    }
  }

  void enqueue(std::function<void()> job)
  {
    {
      std::lock_guard<std::mutex> lock(jobs_mutex_);
      if (worker_stop_) {
        return;
      }
      if (!worker_.joinable()) {
        worker_ = std::thread([this]() {worker_loop();});
      }
      jobs_.push_back(std::move(job));
    }
    jobs_cv_.notify_one();
  }

  // On the proxy thread: hand the request to the owner's servicefunction
  // callback (on the worker); the response goes back via the proxy thread.
  void on_request(std::shared_ptr<rmw_request_id_t> header, std::shared_ptr<void> request)
  {
    RR_SHARED_PTR<GenericSkel> skel;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      skel = skel_.lock();
    }
    RR_SHARED_PTR<RR::ServerContext> ctx = skel ? skel->GetContext() : nullptr;
    if (!ctx) {
      RCLCPP_WARN(
        logger_, "%s: request dropped (the client has not opened the service object)",
        service_.c_str());
      return;
    }
    auto req = RR::CreateMessageEntry(RR::MessageEntryType_CallbackCallReq, "servicefunction");
    req->ServicePath = skel->GetServicePath();
    req->AddElement(
      "request",
      ros_to_rr(Flavor::Cpp, types_.request, request.get(), types_.request_struct));
    RR_WEAK_PTR<ServerObject> weak = shared_from_this();
    RR_WEAK_PTR<RR::ServerContext> weak_ctx = ctx;
    const uint32_t owner = owner_;
    enqueue(
      [weak, weak_ctx, req, header, owner]() {
        auto c = weak_ctx.lock();
        RR_INTRUSIVE_PTR<RR::MessageEntry> ret;
        RRErrorPtr err;
        try {
          if (!c) {
            throw RR::InvalidOperationException("service closed");
          }
          ret = c->ProcessCallbackRequest(req, owner);
        } catch (const RR::RobotRaconteurException & e) {
          err = RR_MAKE_SHARED<RR::OperationFailedException>(e.what());
        } catch (const std::exception & e) {
          err = RR_MAKE_SHARED<RR::OperationFailedException>(e.what());
        }
        if (auto o = weak.lock()) {
          o->on_response(header, ret, err);
        }
      });
  }

  // On the worker thread.
  void on_response(
    const std::shared_ptr<rmw_request_id_t> & header,
    const RR_INTRUSIVE_PTR<RR::MessageEntry> & ret,
    const RRErrorPtr & err)
  {
    if (err || !ret) {
      // ROS services cannot return errors: no response (the ROS client
      // times out), as when a ROS server fails.
      RCLCPP_WARN(
        logger_, "%s: servicefunction failed: %s", service_.c_str(),
        err ? err->what() : "no response");
      return;
    }
    auto response = std::make_shared<DynamicMessage>(Flavor::Cpp, types_.response);
    try {
      RR::MessageElementPtr r;
      if (!ret->TryFindElement("return", r)) {
        throw ConversionError("no return value");
      }
      rr_to_ros(
        Flavor::Cpp, types_.response, element_structure(r), response->data(),
        types_.response_struct);
    } catch (const std::exception & e) {
      RCLCPP_WARN(logger_, "%s: bad response: %s", service_.c_str(), e.what());
      return;
    }
    RR_WEAK_PTR<ServerObject> weak = shared_from_this();
    try {
      proxy_->post(
        [weak, header, response]() {
          auto o = weak.lock();
          if (!o) {
            return;
          }
          std::shared_ptr<DynamicService> s;
          {
            std::lock_guard<std::mutex> lock(o->mutex_);
            s = o->service_handle_;
          }
          if (s) {
            s->send_response(*header, response->data());
          }
        });
    } catch (const std::exception &) {
      // shutting down
    }
  }

  const std::string service_;
  const std::string type_;
  const ServiceTypes types_;
  const uint32_t owner_;
  rclcpp::Logger logger_;
  std::shared_ptr<ProxyExecutor> proxy_;
  std::mutex mutex_;
  std::shared_ptr<DynamicService> service_handle_;
  RR_WEAK_PTR<GenericSkel> skel_;

  std::mutex jobs_mutex_;
  std::condition_variable jobs_cv_;
  std::deque<std::function<void()>> jobs_;
  bool worker_stop_ = false;
  std::thread worker_;
};

}  // namespace

RR_SHARED_PTR<ProxyObject> make_service_server(
  ProxyManager & manager, const std::string & definition, const std::string & service,
  const std::string & type, const void * members, const rosidl_service_type_support_t * handle,
  uint32_t owner)
{
  auto obj = RR_MAKE_SHARED<ServerObject>(
    definition, service, type, members, owner, manager.node().get_logger());
  obj->start(manager.proxy_executor()->node(), handle, manager.proxy_executor());
  return obj;
}

RR_SHARED_PTR<ProxyObject> make_service_client(
  ProxyManager & manager, const std::string & definition, const std::string & service,
  const std::string & type, const void * members)
{
  auto obj = RR_MAKE_SHARED<ClientObject>(definition, service, type, members);
  obj->start(manager.proxy_executor()->node(), manager.proxy_executor());
  return obj;
}

}  // namespace robotraconteur_ros2_debug_bridge
