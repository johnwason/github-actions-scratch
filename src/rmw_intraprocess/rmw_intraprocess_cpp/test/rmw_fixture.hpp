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

#ifndef RMW_FIXTURE_HPP_
#define RMW_FIXTURE_HPP_

#include <gtest/gtest.h>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"

/// Initialized init options plus an initialized (not shut down) context.
class RmwContextFixture : public ::testing::Test
{
protected:
  void SetUp() override
  {
    options = rmw_get_zero_initialized_init_options();
    ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&options, rcutils_get_default_allocator()));
    options.enclave = rcutils_strdup("/", rcutils_get_default_allocator());
    ASSERT_NE(nullptr, options.enclave);
    context = rmw_get_zero_initialized_context();
    ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context)) << rmw_get_error_string().str;
  }

  void TearDown() override
  {
    if (context.implementation_identifier != nullptr) {
      EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context)) << rmw_get_error_string().str;
      EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context)) << rmw_get_error_string().str;
    }
    EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&options)) << rmw_get_error_string().str;
    rmw_reset_error();
  }

  rmw_init_options_t options;
  rmw_context_t context;
};

#endif  // RMW_FIXTURE_HPP_
