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

/// \file
/// Header-only C++ helpers for the extension API (extension.h): find the API
/// of the running RMW without linking this library, and an RAII tap.
///
/// Typical use in a gateway (it works with any RMW; with another one,
/// find_api() returns nullptr and the gateway subscribes normally):
///
///     if (auto api = rmw_intraprocess_cpp::extension::find_api()) {
///       tap_ = std::make_unique<rmw_intraprocess_cpp::extension::Tap>(
///         api, "/chatter", [](const rmw_intraprocess_tap_sample_t & s) {...});
///     }

#ifndef RMW_INTRAPROCESS_CPP__EXTENSION_HPP_
#define RMW_INTRAPROCESS_CPP__EXTENSION_HPP_

#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "rmw/rmw.h"

#include "rmw_intraprocess_cpp/extension.h"

namespace rmw_intraprocess_cpp
{
namespace extension
{

/// The extension API of the running RMW, or nullptr if the RMW that
/// rmw_implementation loaded is not rmw_intraprocess_cpp (or does not support
/// `version`). It is reached through the identifier block of the RMW instance
/// in use (extension.h), never by loading the library by name, so it is the
/// same instance whatever the library search path holds.
inline const rmw_intraprocess_extension_api_t * find_api(
  uint32_t version = RMW_INTRAPROCESS_EXTENSION_API_VERSION)
{
  const char * identifier = rmw_get_implementation_identifier();
  if (identifier == nullptr || std::strcmp(identifier, "rmw_intraprocess_cpp") != 0) {
    return nullptr;
  }
  const auto * block = reinterpret_cast<const rmw_intraprocess_identifier_block_t *>(identifier);
  if (block->magic != RMW_INTRAPROCESS_IDENTIFIER_BLOCK_MAGIC ||
    block->get_extension_api == nullptr)
  {
    return nullptr;
  }
  return block->get_extension_api(version);
}

/// A registered tap; removed (waiting for calls in progress) on destruction.
class Tap
{
public:
  using Callback = std::function<void (const rmw_intraprocess_tap_sample_t &)>;

  /// topic: fully qualified name, or empty for every topic. Throws
  /// std::runtime_error if the tap cannot be added.
  Tap(const rmw_intraprocess_extension_api_t * api, const std::string & topic, Callback callback)
  : api_(api), callback_(std::make_unique<Callback>(std::move(callback)))
  {
    handle_ = api_->add_tap(
      topic.empty() ? nullptr : topic.c_str(),
      [](const rmw_intraprocess_tap_sample_t * sample, void * user_data) {
        (*static_cast<Callback *>(user_data))(*sample);
      },
      callback_.get());
    if (handle_ == 0) {
      throw std::runtime_error("rmw_intraprocess_cpp: could not add tap for '" + topic + "'");
    }
  }

  ~Tap()
  {
    api_->remove_tap(handle_);
  }

  Tap(const Tap &) = delete;
  Tap & operator=(const Tap &) = delete;

private:
  const rmw_intraprocess_extension_api_t * api_;
  std::unique_ptr<Callback> callback_;
  uint64_t handle_ = 0;
};

}  // namespace extension
}  // namespace rmw_intraprocess_cpp

#endif  // RMW_INTRAPROCESS_CPP__EXTENSION_HPP_
