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

#include <gtest/gtest.h>

#include <string>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/rmw.h"
#include "rmw/security_options.h"

#include "./rmw_fixture.hpp"

TEST(Identifier, ImplementationIdentifierAndFormat)
{
  EXPECT_STREQ("rmw_intraprocess_cpp", rmw_get_implementation_identifier());
  EXPECT_STREQ("cdr", rmw_get_serialization_format());
}

TEST(InitOptions, InitCopyFini)
{
  rmw_init_options_t src = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&src, rcutils_get_default_allocator()));
  EXPECT_STREQ("rmw_intraprocess_cpp", src.implementation_identifier);
  EXPECT_EQ(RMW_DEFAULT_DOMAIN_ID, src.domain_id);
  EXPECT_EQ(nullptr, src.enclave);
  // Initializing twice is rejected.
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_init_options_init(&src, rcutils_get_default_allocator()));
  rmw_reset_error();

  src.enclave = rcutils_strdup("/my_enclave", rcutils_get_default_allocator());
  src.domain_id = 42;
  rmw_init_options_t dst = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_copy(&src, &dst)) << rmw_get_error_string().str;
  ASSERT_NE(nullptr, dst.enclave);
  EXPECT_NE(src.enclave, dst.enclave);  // deep copy
  EXPECT_STREQ("/my_enclave", dst.enclave);
  EXPECT_EQ(42u, dst.domain_id);
  // Copy into an initialized destination is rejected.
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_init_options_copy(&src, &dst));
  rmw_reset_error();

  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&dst));
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&src));
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_init_options_fini(&src));
  rmw_reset_error();
}

TEST(InitOptions, BadArguments)
{
  rmw_init_options_t opts = rmw_get_zero_initialized_init_options();
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT, rmw_init_options_init(nullptr, rcutils_get_default_allocator()));
  rmw_reset_error();
  EXPECT_EQ(
    RMW_RET_INVALID_ARGUMENT,
    rmw_init_options_init(&opts, rcutils_get_zero_initialized_allocator()));
  rmw_reset_error();
  rmw_init_options_t dst = rmw_get_zero_initialized_init_options();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_init_options_copy(&opts, &dst));
  rmw_reset_error();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&opts, rcutils_get_default_allocator()));
  const char * id = opts.implementation_identifier;
  opts.implementation_identifier = "not-this-rmw";
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_init_options_copy(&opts, &dst));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_init_options_fini(&opts));
  rmw_reset_error();
  opts.implementation_identifier = id;
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&opts));
}

class InitShutdown : public RmwContextFixture {};

TEST_F(InitShutdown, LifecycleRules)
{
  // Second init on an initialized context fails.
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_init(&options, &context));
  rmw_reset_error();
  // fini before shutdown fails.
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_context_fini(&context));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));
  EXPECT_EQ(RMW_RET_OK, rmw_shutdown(&context));  // idempotent
  EXPECT_EQ(RMW_RET_OK, rmw_context_fini(&context));
  EXPECT_EQ(nullptr, context.implementation_identifier);
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_context_fini(&context));
  rmw_reset_error();
  // The same context object can be initialized again.
  ASSERT_EQ(RMW_RET_OK, rmw_init(&options, &context));
}

TEST_F(InitShutdown, ContextCopiesOptions)
{
  EXPECT_STREQ("rmw_intraprocess_cpp", context.implementation_identifier);
  ASSERT_NE(nullptr, context.options.enclave);
  EXPECT_NE(options.enclave, context.options.enclave);
  EXPECT_STREQ("/", context.options.enclave);
  EXPECT_EQ(0u, context.actual_domain_id);  // RMW_DEFAULT_DOMAIN_ID maps to 0
}

TEST(Init, DomainIdIsAcceptedAndIgnored)
{
  rmw_init_options_t opts = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&opts, rcutils_get_default_allocator()));
  opts.enclave = rcutils_strdup("/", rcutils_get_default_allocator());
  rmw_context_t a = rmw_get_zero_initialized_context();
  rmw_context_t b = rmw_get_zero_initialized_context();
  opts.domain_id = 7;
  ASSERT_EQ(RMW_RET_OK, rmw_init(&opts, &a));
  opts.domain_id = 99;
  ASSERT_EQ(RMW_RET_OK, rmw_init(&opts, &b));  // different domains, same process: fine
  EXPECT_EQ(7u, a.actual_domain_id);
  EXPECT_EQ(99u, b.actual_domain_id);
  for (rmw_context_t * c : {&a, &b}) {
    EXPECT_EQ(RMW_RET_OK, rmw_shutdown(c));
    EXPECT_EQ(RMW_RET_OK, rmw_context_fini(c));
  }
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&opts));
}

TEST(Init, BadArguments)
{
  rmw_init_options_t opts = rmw_get_zero_initialized_init_options();
  rmw_context_t ctx = rmw_get_zero_initialized_context();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_init(nullptr, &ctx));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_init(&opts, &ctx));  // zero-initialized options
  rmw_reset_error();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&opts, rcutils_get_default_allocator()));
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_init(&opts, &ctx));  // no enclave
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_init(&opts, nullptr));
  rmw_reset_error();
  opts.enclave = rcutils_strdup("/", rcutils_get_default_allocator());
  const char * id = opts.implementation_identifier;
  opts.implementation_identifier = "not-this-rmw";
  EXPECT_EQ(RMW_RET_INCORRECT_RMW_IMPLEMENTATION, rmw_init(&opts, &ctx));
  rmw_reset_error();
  opts.implementation_identifier = id;
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_shutdown(&ctx));  // never initialized
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_INVALID_ARGUMENT, rmw_shutdown(nullptr));
  rmw_reset_error();
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&opts));
}

TEST(Init, EnforcedSecurityIsUnsupported)
{
  rmw_init_options_t opts = rmw_get_zero_initialized_init_options();
  ASSERT_EQ(RMW_RET_OK, rmw_init_options_init(&opts, rcutils_get_default_allocator()));
  opts.enclave = rcutils_strdup("/", rcutils_get_default_allocator());
  opts.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_ENFORCE;
  rmw_context_t ctx = rmw_get_zero_initialized_context();
  EXPECT_EQ(RMW_RET_UNSUPPORTED, rmw_init(&opts, &ctx));
  EXPECT_NE(std::string::npos, std::string(rmw_get_error_string().str).find("security"));
  rmw_reset_error();
  EXPECT_EQ(nullptr, ctx.implementation_identifier);
  opts.security_options.enforce_security = RMW_SECURITY_ENFORCEMENT_PERMISSIVE;
  EXPECT_EQ(RMW_RET_OK, rmw_init_options_fini(&opts));
}
