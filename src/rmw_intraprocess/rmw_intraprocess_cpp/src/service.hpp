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

#ifndef SERVICE_HPP_
#define SERVICE_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "rcutils/time.h"
#include "rmw/event_callback_type.h"
#include "rmw/types.h"

#include "new_data_callback.hpp"
#include "node.hpp"
#include "type_support.hpp"
#include "waitable.hpp"

namespace rmw_intraprocess_cpp
{

/// A request or a response in flight.
struct ServiceMessage
{
  std::shared_ptr<const std::vector<std::uint8_t>> cdr;
  rmw_request_id_t request_id{};
  rcutils_time_point_value_t source_timestamp = 0;
  rcutils_time_point_value_t received_timestamp = 0;
};

/// FIFO of ServiceMessage that rmw_wait can wait on, with a new-data callback.
/// Same locking and callback rules as SubscriptionImpl (docs/design.md).
class MessageQueue : public Waitable
{
public:
  /// Enqueues (KEEP_LAST drops the oldest) and notifies; callback outside the lock.
  void deliver(ServiceMessage message, const rmw_qos_profile_t & qos);
  bool take(ServiceMessage & out);
  bool has_data() const;
  void set_callback(rmw_event_callback_t callback, const void * user_data);
  void mark_destroyed();

private:
  std::deque<ServiceMessage> queue_;  // guarded by Waitable::mutex_
  bool destroyed_ = false;  // guarded by Waitable::mutex_
  NewDataCallback new_data_callback_;
};

/// Common to services and clients.
struct ServiceEndpoint
{
  NodeImpl * node = nullptr;
  const rmw_context_t * context = nullptr;
  std::string service_name;
  ServiceTypeSupport type;
  rmw_qos_profile_t qos{};
};

/// Behind rmw_service_t::data (MessageQueue holds requests). Owned by the registry.
class ServiceImpl : public ServiceEndpoint, public MessageQueue {};

/// Behind rmw_client_t::data (MessageQueue holds responses). Owned by the registry.
class ClientImpl : public ServiceEndpoint, public MessageQueue
{
public:
  rmw_gid_t gid{};
  std::atomic<std::int64_t> sequence_number{0};
};

/// A client and a service match if they have the same name and type.
bool service_endpoints_match(const ClientImpl & client, const ServiceImpl & service);

}  // namespace rmw_intraprocess_cpp

#endif  // SERVICE_HPP_
