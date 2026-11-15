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

#ifndef NODE_HPP_
#define NODE_HPP_

#include <string>

#include "rmw/types.h"

#include "context.hpp"
#include "guard_condition.hpp"

namespace rmw_intraprocess_cpp
{

/// Implementation data behind rmw_node_t::data.
struct NodeImpl
{
  NodeImpl(rmw_context_t * context_in, const char * name_in, const char * namespace_in)
  : context(context_in), name(name_in), namespace_(namespace_in)
  {
  }

  rmw_context_t * context;
  std::string name;
  std::string namespace_;
  std::string enclave;
  GuardCondition graph_guard_condition;
  rmw_guard_condition_t graph_guard_condition_handle{};
};

}  // namespace rmw_intraprocess_cpp

#endif  // NODE_HPP_
