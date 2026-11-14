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

#ifndef IDENTIFIER_HPP_
#define IDENTIFIER_HPP_

// Users of kIdentifier check identifiers with RMW_CHECK_TYPE_IDENTIFIERS_MATCH.
#include "rmw/check_type_identifiers_match.h"

namespace rmw_intraprocess_cpp
{

/// Implementation identifier returned by rmw_get_implementation_identifier().
extern const char * const kIdentifier;

/// Serialization format used for serialized messages (CDR, as rmw_fastrtps).
extern const char * const kSerializationFormat;

}  // namespace rmw_intraprocess_cpp

#endif  // IDENTIFIER_HPP_
