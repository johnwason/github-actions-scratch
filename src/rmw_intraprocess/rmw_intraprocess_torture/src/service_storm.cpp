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

// ServiceStorm: several servers share one service name; client threads send
// requests back to back and check that each response belongs to their own
// request; a churn thread keeps creating and destroying an extra server (a
// sink that never answers) and clients destroyed with a request in flight, on
// a private node (see churn_loop).
//
// Parameters:
//   service (string, "storm"), servers (int, 2), clients (int, 4)
//   requests (int, 200): per client; 0 = until stopped
//   churn (bool, true): the extra server / abandoned client thread
//   serve (bool, true), call (bool, true): run the servers / the clients
//
// Stats: requests, responses, served, wrong_response, timeouts,
// churn_servers, abandoned_requests.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "test_msgs/srv/basic_types.hpp"

#include "./common.hpp"

namespace rmw_intraprocess_torture
{

using test_msgs::srv::BasicTypes;

class ServiceStorm : public TortureNode
{
public:
  explicit ServiceStorm(const rclcpp::NodeOptions & options)
  : TortureNode("service_storm", options)
  {
    service_ = declare_parameter("service", std::string("storm"));
    const auto servers = declare_parameter("servers", 2);
    const auto clients = declare_parameter("clients", 4);
    requests_ = declare_parameter("requests", 200);
    const bool churn = declare_parameter("churn", true);
    churn_iterations_ = declare_parameter("churn_iterations", 200);
    const bool serve = declare_parameter("serve", true);
    const bool call = declare_parameter("call", true);
    if (serve) {
      for (int64_t i = 0; i < servers; ++i) {
        servers_.push_back(make_server(static_cast<int32_t>(i)));
      }
    }
    if (call) {
      for (int64_t i = 0; i < clients; ++i) {
        auto client = create_client<BasicTypes>(service_, qos_, group_);
        clients_.push_back(client);
        threads_.emplace_back([this, client]() {call_loop(client);});
      }
      if (churn) {
        threads_.emplace_back([this]() {churn_loop();});
      }
    }
  }

  ~ServiceStorm() override
  {
    abort_ = true;
    stop_ = true;
    for (auto & t : threads_) {
      t.join();
    }
  }

protected:
  void on_stop() override {stop_ = true;}

private:
  rclcpp::Service<BasicTypes>::SharedPtr make_server(int32_t tag)
  {
    return create_service<BasicTypes>(
      service_,
      [this, tag](const std::shared_ptr<BasicTypes::Request> req,
      std::shared_ptr<BasicTypes::Response> res) {
        // Echo the request identity; tag says which server answered.
        res->int32_value = req->int32_value;
        res->int64_value = req->int64_value;
        res->uint64_value = req->uint64_value * 3;
        res->int16_value = static_cast<int16_t>(tag);
        add("served");
      },
      qos_, group_);
  }

  void call_loop(const rclcpp::Client<BasicTypes>::SharedPtr & client)
  {
    const int32_t id = static_cast<int32_t>(next_id());
    try {
      while (!stop_ && !client->wait_for_service(std::chrono::milliseconds(100))) {
      }
      for (int64_t seq = 1; !stop_ && (requests_ == 0 || seq <= requests_); ++seq) {
        auto req = std::make_shared<BasicTypes::Request>();
        req->int32_value = id;
        req->int64_value = seq;
        req->uint64_value = static_cast<uint64_t>(seq) * 7u;
        // The response callback runs on the executor; hand over the fields
        // the check needs through a promise set in this (instrumented) code.
        auto promise = std::make_shared<std::promise<BasicTypes::Response>>();
        auto future = promise->get_future();
        client->async_send_request(
          req, [promise](rclcpp::Client<BasicTypes>::SharedFuture f) {
            promise->set_value(*f.get());
          });
        add("requests");
        if (!wait_or_stop(future, std::chrono::seconds(60), abort_)) {
          if (abort_) {
            return;  // torn down (e.g. shutdown): no reply will come
          }
          add("timeouts");
          error("request " + std::to_string(seq) + " of client " + std::to_string(id) +
            " timed out");
          continue;
        }
        const auto res = future.get();
        add("responses");
        if (res.int32_value != id || res.int64_value != seq ||
          res.uint64_value != req->uint64_value * 3)
        {
          add("wrong_response");
          error("client " + std::to_string(id) + " got a response for client " +
            std::to_string(res.int32_value) + " seq " + std::to_string(res.int64_value));
        }
      }
    } catch (const std::exception & e) {
      if (rclcpp::ok(get_node_base_interface()->get_context())) {
        error(std::string("client: ") + e.what());
      }
    }
  }

  void churn_loop()
  {
    try {
      // A private node used only by this thread: rcl entity init/fini on one
      // node is not thread-safe (rcl's per-node type description cache), so
      // the churned entities never share a node with entities other threads
      // (or the executor) create or destroy. The node is not spun: its
      // server is a sink that dies with requests queued, and its client is
      // destroyed with a request in flight.
      auto node = std::make_shared<rclcpp::Node>(
        std::string(get_name()) + "_churn", get_namespace(),
        rclcpp::NodeOptions().context(get_node_base_interface()->get_context())
        .start_parameter_services(false).start_parameter_event_publisher(false));
      for (int64_t i = 0; !stop_ && (churn_iterations_ == 0 || i < churn_iterations_); ++i) {
        auto server = node->create_service<BasicTypes>(
          service_,
          [](const std::shared_ptr<BasicTypes::Request>, std::shared_ptr<BasicTypes::Response>) {},
          qos_);
        add("churn_servers");
        auto client = node->create_client<BasicTypes>(service_, qos_);
        if (client->wait_for_service(std::chrono::milliseconds(100))) {
          auto req = std::make_shared<BasicTypes::Request>();
          req->int32_value = -1;
          client->async_send_request(req, [](rclcpp::Client<BasicTypes>::SharedFuture) {});
          add("abandoned_requests");
        }
        client.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        server.reset();
      }
    } catch (const std::exception & e) {
      if (rclcpp::ok(get_node_base_interface()->get_context())) {
        error(std::string("churn: ") + e.what());
      }
    }
  }

  std::string service_;
  int64_t requests_ = 200;
  // Request/response queues deep enough for every request in flight: with the
  // default ServicesQoS (KEEP_LAST 10) a busy server legitimately drops the
  // oldest requests (any RMW), which this test would report as timeouts.
  rclcpp::QoS qos_ = rclcpp::ServicesQoS().keep_last(10000);
  rclcpp::CallbackGroup::SharedPtr group_ =
    create_callback_group(rclcpp::CallbackGroupType::Reentrant);
  std::vector<rclcpp::Service<BasicTypes>::SharedPtr> servers_;
  std::vector<rclcpp::Client<BasicTypes>::SharedPtr> clients_;
  int64_t churn_iterations_ = 200;
  std::atomic<bool> stop_{false};
  std::vector<std::thread> threads_;
};

}  // namespace rmw_intraprocess_torture

RCLCPP_COMPONENTS_REGISTER_NODE(rmw_intraprocess_torture::ServiceStorm)
