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

#ifndef CONTEXT_HPP_
#define CONTEXT_HPP_

#include <atomic>
#include <cstddef>

// rmw_context_impl_t is the opaque type declared by rmw/init.h; the
// implementation defines it.
struct rmw_context_impl_s
{
  std::atomic<bool> is_shutdown{false};
  std::atomic<std::size_t> node_count{0};
};

#endif  // CONTEXT_HPP_
