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

#include "value_converter.hpp"

#include <RobotRaconteur/DataTypes.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "rosidl_runtime_c/string.h"
#include "rosidl_runtime_c/string_functions.h"
#include "rosidl_runtime_c/u16string.h"
#include "rosidl_runtime_c/u16string_functions.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "type_mapper.hpp"

namespace robotraconteur_ros2_debug_bridge
{

namespace
{

namespace RR = RobotRaconteur;

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16 (wstring). Strict decoding of what a client sends;
// lenient (U+FFFD) encoding of what ROS holds.

std::u16string utf8_to_utf16(const std::string & s, const std::string & field)
{
  std::u16string out;
  out.reserve(s.size());
  size_t i = 0;
  auto bad = [&]() {
      return ConversionError("field '" + field + "': invalid UTF-8");
    };
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    uint32_t cp = 0;
    size_t n = 0;
    if (c < 0x80) {
      cp = c;
      n = 1;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1Fu;
      n = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0Fu;
      n = 3;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07u;
      n = 4;
    } else {
      throw bad();
    }
    if (i + n > s.size()) {
      throw bad();
    }
    for (size_t k = 1; k < n; ++k) {
      const auto cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) {
        throw bad();
      }
      cp = (cp << 6) | (cc & 0x3Fu);
    }
    static const uint32_t min_for_length[] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < min_for_length[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      throw bad();
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
    i += n;
  }
  return out;
}

std::string utf16_to_utf8(const char16_t * s, size_t size)
{
  std::string out;
  out.reserve(size);
  auto put = [&out](uint32_t cp) {
      if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
      } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
    };
  for (size_t i = 0; i < size; ++i) {
    const uint32_t u = s[i];
    if (u >= 0xD800 && u <= 0xDBFF && i + 1 < size && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
      put(0x10000 + ((u - 0xD800) << 10) + (static_cast<uint32_t>(s[i + 1]) - 0xDC00));
      ++i;
    } else if (u >= 0xD800 && u <= 0xDFFF) {
      put(0xFFFD);  // lone surrogate
    } else {
      put(u);
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// The two introspection flavors.

struct CppTraits
{
  using Members = rosidl_typesupport_introspection_cpp::MessageMembers;
  using Member = rosidl_typesupport_introspection_cpp::MessageMember;
  static constexpr Flavor flavor = Flavor::Cpp;

  static std::string get_string(const void * p) {return *static_cast<const std::string *>(p);}
  static void set_string(void * p, const std::string & s) {*static_cast<std::string *>(p) = s;}
  static std::string get_wstring(const void * p)
  {
    const auto & w = *static_cast<const std::u16string *>(p);
    return utf16_to_utf8(w.data(), w.size());
  }
  static void set_wstring(void * p, const std::u16string & s)
  {
    *static_cast<std::u16string *>(p) = s;
  }
  static void resize(const Member & m, void * field, size_t n, const std::string &)
  {
    m.resize_function(field, n);
  }
};

struct CTraits
{
  using Members = rosidl_typesupport_introspection_c__MessageMembers;
  using Member = rosidl_typesupport_introspection_c__MessageMember;
  static constexpr Flavor flavor = Flavor::C;

  static std::string get_string(const void * p)
  {
    const auto * s = static_cast<const rosidl_runtime_c__String *>(p);
    return s->data ? std::string(s->data, s->size) : std::string();
  }
  static void set_string(void * p, const std::string & s)
  {
    if (!rosidl_runtime_c__String__assignn(
        static_cast<rosidl_runtime_c__String *>(p), s.data(), s.size()))
    {
      throw std::bad_alloc();
    }
  }
  static std::string get_wstring(const void * p)
  {
    const auto * s = static_cast<const rosidl_runtime_c__U16String *>(p);
    return s->data ? utf16_to_utf8(reinterpret_cast<const char16_t *>(s->data), s->size) :
           std::string();
  }
  static void set_wstring(void * p, const std::u16string & s)
  {
    if (!rosidl_runtime_c__U16String__assignn(
        static_cast<rosidl_runtime_c__U16String *>(p),
        reinterpret_cast<const uint16_t *>(s.data()), s.size()))
    {
      throw std::bad_alloc();
    }
  }
  static void resize(const Member & m, void * field, size_t n, const std::string & path)
  {
    if (!m.resize_function(field, n)) {
      throw ConversionError("field '" + path + "': cannot resize to " + std::to_string(n));
    }
  }
};

// The type ids are the same numbers in both flavors.
namespace ft = rosidl_typesupport_introspection_cpp;

std::string join(const std::string & path, const char * name)
{
  return path.empty() ? std::string(name) : path + "." + name;
}

template<class Tr>
std::string struct_type_of(const typename Tr::Members * m)
{
  // "pkg::msg" (C++) or "pkg__msg" (C)
  std::string ns = m->message_namespace_;
  auto sep = ns.find("::");
  if (sep == std::string::npos) {
    sep = ns.find("__");
  }
  const std::string package = ns.substr(0, sep);
  return message_definition_name(package, m->message_name_) + "." + fixname(m->message_name_);
}

bool placeholder_only(const char * name, uint32_t count)
{
  return count == 1 && std::strcmp(name, "structure_needs_at_least_one_member") == 0;
}

// Element storage size and the Robot Raconteur element type of a numeric
// ROS type; 0 if not numeric.
struct NumericInfo
{
  size_t size;
  RR::DataTypes rr;
};

NumericInfo numeric_info(uint8_t type_id)
{
  switch (type_id) {
    case ft::ROS_TYPE_FLOAT: return {4, RR::DataTypes_single_t};
    case ft::ROS_TYPE_DOUBLE: return {8, RR::DataTypes_double_t};
    case ft::ROS_TYPE_CHAR: return {1, RR::DataTypes_uint8_t};
    case ft::ROS_TYPE_WCHAR: return {2, RR::DataTypes_uint16_t};
    case ft::ROS_TYPE_BOOLEAN: return {sizeof(bool), RR::DataTypes_bool_t};
    case ft::ROS_TYPE_OCTET: return {1, RR::DataTypes_uint8_t};
    case ft::ROS_TYPE_UINT8: return {1, RR::DataTypes_uint8_t};
    case ft::ROS_TYPE_INT8: return {1, RR::DataTypes_int8_t};
    case ft::ROS_TYPE_UINT16: return {2, RR::DataTypes_uint16_t};
    case ft::ROS_TYPE_INT16: return {2, RR::DataTypes_int16_t};
    case ft::ROS_TYPE_UINT32: return {4, RR::DataTypes_uint32_t};
    case ft::ROS_TYPE_INT32: return {4, RR::DataTypes_int32_t};
    case ft::ROS_TYPE_UINT64: return {8, RR::DataTypes_uint64_t};
    case ft::ROS_TYPE_INT64: return {8, RR::DataTypes_int64_t};
    default: return {0, RR::DataTypes_void_t};
  }
}

const char * rr_type_name(RR::DataTypes t)
{
  switch (t) {
    case RR::DataTypes_single_t: return "single";
    case RR::DataTypes_double_t: return "double";
    case RR::DataTypes_bool_t: return "bool";
    case RR::DataTypes_uint8_t: return "uint8";
    case RR::DataTypes_int8_t: return "int8";
    case RR::DataTypes_uint16_t: return "uint16";
    case RR::DataTypes_int16_t: return "int16";
    case RR::DataTypes_uint32_t: return "uint32";
    case RR::DataTypes_int32_t: return "int32";
    case RR::DataTypes_uint64_t: return "uint64";
    case RR::DataTypes_int64_t: return "int64";
    case RR::DataTypes_string_t: return "string";
    case RR::DataTypes_structure_t: return "structure";
    case RR::DataTypes_list_t: return "list";
    case RR::DataTypes_void_t: return "null";
    default: return "another type";
  }
}

// ---------------------------------------------------------------------------
// ROS -> RR

template<class Tr>
StructurePtr struct_to_rr(
  const typename Tr::Members * m, const void * msg, const std::string & type);

template<class Tr>
RR::MessageElementDataPtr field_to_rr(const typename Tr::Member & f, const void * field)
{
  if (f.type_id_ == ft::ROS_TYPE_MESSAGE) {
    const auto * nested = static_cast<const typename Tr::Members *>(f.members_->data);
    const std::string type = struct_type_of<Tr>(nested);
    if (!f.is_array_) {
      return struct_to_rr<Tr>(nested, field, type);
    }
    const size_t n = f.size_function(field);
    std::vector<RR::MessageElementPtr> items;
    items.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      items.push_back(
        RR::CreateMessageElement(
          static_cast<int32_t>(i), struct_to_rr<Tr>(nested, f.get_const_function(field, i), type)));
    }
    return RR::CreateMessageElementNestedElementList(RR::DataTypes_list_t, "", std::move(items));
  }
  if (f.type_id_ == ft::ROS_TYPE_STRING || f.type_id_ == ft::ROS_TYPE_WSTRING) {
    const bool wide = f.type_id_ == ft::ROS_TYPE_WSTRING;
    auto get = [wide](const void * p) {
        return RR::stringToRRArray(wide ? Tr::get_wstring(p) : Tr::get_string(p));
      };
    if (!f.is_array_) {
      return get(field);
    }
    const size_t n = f.size_function(field);
    std::vector<RR::MessageElementPtr> items;
    items.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      items.push_back(
        RR::CreateMessageElement(static_cast<int32_t>(i), get(f.get_const_function(field, i))));
    }
    return RR::CreateMessageElementNestedElementList(RR::DataTypes_list_t, "", std::move(items));
  }
  const NumericInfo info = numeric_info(f.type_id_);
  if (info.size == 0) {
    throw ConversionError(
            std::string("field '") + f.name_ + "' has an unsupported type (" +
            std::to_string(f.type_id_) + ")");
  }
  const size_t n = f.is_array_ ? f.size_function(field) : 1;
  auto arr = RR::AllocateRRArrayByType(info.rr, n);
  if (n == 0) {
    return arr;
  }
  auto * out = static_cast<uint8_t *>(arr->void_ptr());
  if (f.type_id_ == ft::ROS_TYPE_BOOLEAN) {
    // rr_bool is one byte. Read the bools as bytes (a stored byte other
    // than 0/1 must not be undefined behavior), except a C++ bool sequence:
    // std::vector<bool> is not contiguous.
    const bool cpp_vector = Tr::flavor == Flavor::Cpp && f.is_array_ &&
      (f.is_upper_bound_ || f.array_size_ == 0);
    if (cpp_vector) {
      for (size_t i = 0; i < n; ++i) {
        bool b = false;
        f.fetch_function(field, i, &b);
        out[i] = b ? 1 : 0;
      }
    } else {
      const auto * bytes =
        static_cast<const uint8_t *>(f.is_array_ ? f.get_const_function(field, 0) : field);
      for (size_t i = 0; i < n; ++i) {
        out[i] = bytes[i] != 0 ? 1 : 0;
      }
    }
    return arr;
  }
  const void * src = f.is_array_ ? f.get_const_function(field, 0) : field;
  std::memcpy(out, src, n * info.size);
  return arr;
}

template<class Tr>
StructurePtr struct_to_rr(
  const typename Tr::Members * m, const void * msg, const std::string & type)
{
  std::vector<RR::MessageElementPtr> fields;
  if (!(m->member_count_ == 1 && placeholder_only(m->members_[0].name_, 1))) {
    fields.reserve(m->member_count_);
    for (uint32_t i = 0; i < m->member_count_; ++i) {
      const auto & f = m->members_[i];
      const void * field = static_cast<const uint8_t *>(msg) + f.offset_;
      fields.push_back(RR::CreateMessageElement(fixname(f.name_), field_to_rr<Tr>(f, field)));
    }
  }
  return RR::CreateMessageElementNestedElementList(
    RR::DataTypes_structure_t, type, std::move(fields));
}

// ---------------------------------------------------------------------------
// RR -> ROS

template<class Tr>
void struct_from_rr(
  const typename Tr::Members * m, const StructurePtr & in, void * msg,
  const std::string & type, const std::string & path);

[[noreturn]] void type_error(
  const std::string & path, const std::string & expected, RR::DataTypes got)
{
  throw ConversionError(
          "field '" + path + "': expected " + expected + ", got " + rr_type_name(got));
}

template<class Tr>
void check_count(const typename Tr::Member & f, size_t n, const std::string & path)
{
  const bool fixed = !f.is_upper_bound_ && f.array_size_ > 0;
  if (fixed && n != f.array_size_) {
    throw ConversionError(
            "field '" + path + "': fixed length " + std::to_string(f.array_size_) + ", got " +
            std::to_string(n));
  }
  if (f.is_upper_bound_ && n > f.array_size_) {
    throw ConversionError(
            "field '" + path + "': at most " + std::to_string(f.array_size_) +
            " elements, got " + std::to_string(n));
  }
}

template<class Tr>
void prepare_sequence(
  const typename Tr::Member & f, void * field, size_t n,
  const std::string & path)
{
  check_count<Tr>(f, n, path);
  if (f.is_upper_bound_ || f.array_size_ == 0) {
    Tr::resize(f, field, n, path);
  }
}

// The list's items in order; checks the element numbers.
std::vector<RR::MessageElementPtr> list_items(
  const RR::MessageElementPtr & e, const std::string & path)
{
  if (e->ElementType != RR::DataTypes_list_t) {
    type_error(path, "list", e->ElementType);
  }
  auto list = e->CastDataToNestedList();
  std::vector<RR::MessageElementPtr> out = list->Elements;
  for (size_t i = 0; i < out.size(); ++i) {
    int32_t number = -1;
    if (!RR::MessageElement_GetElementNumber(out[i], number) || number != static_cast<int32_t>(i)) {
      throw ConversionError("field '" + path + "': invalid list (element numbering)");
    }
  }
  return out;
}

template<class Tr>
void set_string_field(
  const typename Tr::Member & f, void * p, const RR::MessageElementPtr & e,
  const std::string & path)
{
  if (e->ElementType != RR::DataTypes_string_t) {
    type_error(path, "string", e->ElementType);
  }
  const std::string s = RR::RRArrayToString(e->CastData<RR::RRArray<char>>());
  if (f.type_id_ == ft::ROS_TYPE_WSTRING) {
    const std::u16string w = utf8_to_utf16(s, path);
    if (f.string_upper_bound_ > 0 && w.size() > f.string_upper_bound_) {
      throw ConversionError(
              "field '" + path + "': at most " + std::to_string(f.string_upper_bound_) +
              " characters, got " + std::to_string(w.size()));
    }
    Tr::set_wstring(p, w);
  } else {
    if (f.string_upper_bound_ > 0 && s.size() > f.string_upper_bound_) {
      throw ConversionError(
              "field '" + path + "': at most " + std::to_string(f.string_upper_bound_) +
              " characters, got " + std::to_string(s.size()));
    }
    Tr::set_string(p, s);
  }
}

template<class Tr>
void field_from_rr(
  const typename Tr::Member & f, void * field, const RR::MessageElementPtr & e,
  const std::string & path)
{
  if (e->ElementType == RR::DataTypes_void_t) {
    return;  // null: keep the message's value
  }
  if (f.type_id_ == ft::ROS_TYPE_MESSAGE) {
    const auto * nested = static_cast<const typename Tr::Members *>(f.members_->data);
    const std::string type = struct_type_of<Tr>(nested);
    if (!f.is_array_) {
      if (e->ElementType != RR::DataTypes_structure_t) {
        type_error(path, type, e->ElementType);
      }
      struct_from_rr<Tr>(nested, e->CastDataToNestedList(), field, type, path);
      return;
    }
    const auto items = list_items(e, path);
    prepare_sequence<Tr>(f, field, items.size(), path);
    for (size_t i = 0; i < items.size(); ++i) {
      const std::string p = path + "[" + std::to_string(i) + "]";
      if (items[i]->ElementType != RR::DataTypes_structure_t) {
        type_error(p, type, items[i]->ElementType);
      }
      void * element = f.get_function(field, i);
      struct_from_rr<Tr>(nested, items[i]->CastDataToNestedList(), element, type, p);
    }
    return;
  }
  if (f.type_id_ == ft::ROS_TYPE_STRING || f.type_id_ == ft::ROS_TYPE_WSTRING) {
    if (!f.is_array_) {
      set_string_field<Tr>(f, field, e, path);
      return;
    }
    const auto items = list_items(e, path);
    prepare_sequence<Tr>(f, field, items.size(), path);
    for (size_t i = 0; i < items.size(); ++i) {
      set_string_field<Tr>(
        f, f.get_function(field, i), items[i], path + "[" + std::to_string(i) + "]");
    }
    return;
  }
  const NumericInfo info = numeric_info(f.type_id_);
  if (info.size == 0) {
    throw ConversionError("field '" + path + "' has an unsupported type");
  }
  if (e->ElementType != info.rr) {
    type_error(path, rr_type_name(info.rr), e->ElementType);
  }
  auto arr = e->CastData<RR::RRBaseArray>();
  const size_t n = arr ? arr->size() : 0;
  const auto * src = arr ? static_cast<const uint8_t *>(arr->void_ptr()) : nullptr;
  if (!f.is_array_) {
    if (n != 1) {
      throw ConversionError(
              "field '" + path + "': expected a scalar, got " + std::to_string(n) + " values");
    }
    if (f.type_id_ == ft::ROS_TYPE_BOOLEAN) {
      *static_cast<bool *>(field) = src[0] != 0;
    } else {
      std::memcpy(field, src, info.size);
    }
    return;
  }
  prepare_sequence<Tr>(f, field, n, path);
  if (n == 0) {
    return;
  }
  if (f.type_id_ == ft::ROS_TYPE_BOOLEAN) {
    for (size_t i = 0; i < n; ++i) {
      const bool b = src[i] != 0;
      f.assign_function(field, i, &b);
    }
    return;
  }
  std::memcpy(f.get_function(field, 0), src, n * info.size);
}

template<class Tr>
void struct_from_rr(
  const typename Tr::Members * m, const StructurePtr & in, void * msg,
  const std::string & type, const std::string & path)
{
  if (!in) {
    return;
  }
  if (in->Type != RR::DataTypes_structure_t) {
    type_error(path.empty() ? "(value)" : path, type, in->Type);
  }
  const std::string got = in->TypeName.str().to_string();
  if (got != type) {
    throw ConversionError(
            "field '" + (path.empty() ? std::string("(value)") : path) + "': expected " + type +
            ", got " + got);
  }
  if (m->member_count_ == 1 && placeholder_only(m->members_[0].name_, 1)) {
    return;
  }
  for (uint32_t i = 0; i < m->member_count_; ++i) {
    const auto & f = m->members_[i];
    RR::MessageElementPtr e;
    if (!RR::MessageElement::TryFindElement(in->Elements, fixname(f.name_), e)) {
      continue;  // missing: keep the message's value
    }
    field_from_rr<Tr>(f, static_cast<uint8_t *>(msg) + f.offset_, e, join(path, f.name_));
  }
}

}  // namespace

std::string message_struct_type(Flavor flavor, const void * members)
{
  return flavor == Flavor::Cpp ?
         struct_type_of<CppTraits>(static_cast<const CppTraits::Members *>(members)) :
         struct_type_of<CTraits>(static_cast<const CTraits::Members *>(members));
}

StructurePtr ros_to_rr(
  Flavor flavor, const void * members, const void * message, const std::string & struct_type)
{
  const std::string type = struct_type.empty() ? message_struct_type(flavor, members) :
    struct_type;
  if (flavor == Flavor::Cpp) {
    return struct_to_rr<CppTraits>(static_cast<const CppTraits::Members *>(members), message, type);
  }
  return struct_to_rr<CTraits>(static_cast<const CTraits::Members *>(members), message, type);
}

void rr_to_ros(
  Flavor flavor, const void * members, const StructurePtr & value, void * message,
  const std::string & struct_type)
{
  const std::string type = struct_type.empty() ? message_struct_type(flavor, members) :
    struct_type;
  if (flavor == Flavor::Cpp) {
    struct_from_rr<CppTraits>(
      static_cast<const CppTraits::Members *>(members), value, message, type, std::string());
  } else {
    struct_from_rr<CTraits>(
      static_cast<const CTraits::Members *>(members), value, message, type, std::string());
  }
}

}  // namespace robotraconteur_ros2_debug_bridge
