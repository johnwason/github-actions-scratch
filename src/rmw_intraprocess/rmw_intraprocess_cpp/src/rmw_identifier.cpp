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

#include <cstring>

#include "rcutils/logging.h"
#include "rmw/error_handling.h"
#include "rmw/features.h"
#include "rmw/rmw.h"

#include "rmw_intraprocess_cpp/extension.h"

#include "identifier.hpp"

namespace rmw_intraprocess_cpp
{
namespace
{
/// The identifier lives in the extension API's identifier block (extension.h),
/// so a client can reach the extension API of this library instance from the
/// pointer rmw_get_implementation_identifier() returns. Constant-initialized.
const rmw_intraprocess_identifier_block_t kIdentifierBlock = {
  "rmw_intraprocess_cpp",
  RMW_INTRAPROCESS_IDENTIFIER_BLOCK_MAGIC,
  &rmw_intraprocess_cpp_get_extension_api,
};
}  // namespace

const char * const kIdentifier = kIdentifierBlock.identifier;
const char * const kSerializationFormat = "cdr";
}  // namespace rmw_intraprocess_cpp

using rmw_intraprocess_cpp::kIdentifier;

extern "C"
{
const char *
rmw_get_implementation_identifier(void)
{
  return kIdentifier;
}

const char *
rmw_get_serialization_format(void)
{
  return rmw_intraprocess_cpp::kSerializationFormat;
}

bool
rmw_feature_supported(rmw_feature_t feature)
{
  switch (feature) {
    case RMW_FEATURE_MESSAGE_INFO_PUBLICATION_SEQUENCE_NUMBER:
    case RMW_FEATURE_MESSAGE_INFO_RECEPTION_SEQUENCE_NUMBER:
      return true;
    default:
      return false;
  }
}

rmw_ret_t
rmw_set_log_severity(rmw_log_severity_t severity)
{
  switch (severity) {
    case RMW_LOG_SEVERITY_DEBUG:
    case RMW_LOG_SEVERITY_INFO:
    case RMW_LOG_SEVERITY_WARN:
    case RMW_LOG_SEVERITY_ERROR:
    case RMW_LOG_SEVERITY_FATAL:
      break;
    default:
      RMW_SET_ERROR_MSG("invalid log severity");
      return RMW_RET_INVALID_ARGUMENT;
  }
  if (rcutils_logging_set_logger_level(kIdentifier, static_cast<int>(severity)) !=
    RCUTILS_RET_OK)
  {
    return RMW_RET_ERROR;
  }
  return RMW_RET_OK;
}

rmw_ret_t
rmw_compare_gids_equal(const rmw_gid_t * gid1, const rmw_gid_t * gid2, bool * result)
{
  RMW_CHECK_ARGUMENT_FOR_NULL(gid1, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    gid1, gid1->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  RMW_CHECK_ARGUMENT_FOR_NULL(gid2, RMW_RET_INVALID_ARGUMENT);
  RMW_CHECK_TYPE_IDENTIFIERS_MATCH(
    gid2, gid2->implementation_identifier, kIdentifier,
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION);
  RMW_CHECK_ARGUMENT_FOR_NULL(result, RMW_RET_INVALID_ARGUMENT);
  *result = std::memcmp(gid1->data, gid2->data, RMW_GID_STORAGE_SIZE) == 0;
  return RMW_RET_OK;
}
}  // extern "C"
