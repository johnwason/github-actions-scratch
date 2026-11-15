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

#include "type_mapper.hpp"

#include <RobotRaconteur/ServiceDefinition.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <locale>
#include <regex>
#include <set>
#include <sstream>
#include <utility>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "ament_index_cpp/get_resource.hpp"
#include "ament_index_cpp/get_resources.hpp"
#include "rclcpp/typesupport_helpers.hpp"
#include "rcpputils/shared_library.hpp"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "rosidl_typesupport_introspection_cpp/service_introspection.hpp"

namespace robotraconteur_ros2_debug_bridge
{

namespace
{

namespace ti = rosidl_typesupport_introspection_cpp;

const char kIndent[] = "    ";

std::string lower(std::string s)
{
  std::transform(
    s.begin(), s.end(), s.begin(),
    [](unsigned char c) {return static_cast<char>(std::tolower(c));});
  return s;
}

bool starts_with(const std::string & s, const std::string & prefix)
{
  return s.compare(0, prefix.size(), prefix) == 0;
}

std::string trim(const std::string & s)
{
  const auto b = s.find_first_not_of(' ');
  if (b == std::string::npos) {
    return std::string();
  }
  const auto e = s.find_last_not_of(' ');
  return s.substr(b, e - b + 1);
}

std::string rtrim(const std::string & s)
{
  const auto e = s.find_last_not_of(" \r\n");
  return e == std::string::npos ? std::string() : s.substr(0, e + 1);
}

// ---------------------------------------------------------------------------
// Constants (from the interface file, parsed like rosidl_adapter's
// parse_message_string; introspection does not carry them).

struct Constant
{
  std::string type;
  std::string name;
  std::string value;
};

using Section = std::vector<Constant>;

Section parse_constants(const std::vector<std::string> & lines)
{
  Section out;
  for (std::string line : lines) {
    std::replace(line.begin(), line.end(), '\t', ' ');
    const auto hash = line.find('#');
    if (hash != std::string::npos) {
      line.erase(hash);
    }
    line = rtrim(line);
    if (line.empty() || line.find_first_not_of(' ') == std::string::npos) {
      continue;
    }
    const auto sp = line.find(' ');
    if (sp == std::string::npos) {
      continue;  // rosidl rejects this; the type support exists, so ignore
    }
    const std::string type = line.substr(0, sp);
    std::string rest = line.substr(sp + 1);
    rest.erase(0, rest.find_first_not_of(' '));
    const auto eq = rest.find('=');
    if (eq == std::string::npos) {
      continue;  // a field
    }
    Constant c;
    c.type = type;
    c.name = rtrim(rest.substr(0, eq));
    c.value = rest.substr(eq + 1);
    c.value.erase(0, c.value.find_first_not_of(' '));
    out.push_back(std::move(c));
  }
  return out;
}

// The interface file split at "---" lines: one section for a message, two
// for a service, three for an action. Missing file (an interface defined
// only in .idl): no constants.
std::vector<Section> read_constants(const InterfaceType & t)
{
  std::string share;
  try {
    share = ament_index_cpp::get_package_share_directory(t.package);
  } catch (const std::exception &) {
    return {};
  }
  std::ifstream in(share + "/" + t.subfolder() + "/" + t.name + "." + t.subfolder());
  if (!in) {
    return {};
  }
  std::vector<Section> sections;
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line == "---") {
      sections.push_back(parse_constants(lines));
      lines.clear();
    } else {
      lines.push_back(line);
    }
  }
  sections.push_back(parse_constants(lines));
  return sections;
}

// Python int(s) / int(s, 0) as rosidl uses them, to a decimal string.
std::optional<std::string> parse_integer(
  std::string s, bool is_signed, unsigned bits)
{
  s = trim(s);
  bool negative = false;
  if (!s.empty() && (s[0] == '+' || s[0] == '-')) {
    negative = s[0] == '-';
    s.erase(0, 1);
  }
  int base = 10;
  if (s.size() > 2 && s[0] == '0') {
    const char p = static_cast<char>(std::tolower(static_cast<unsigned char>(s[1])));
    if (p == 'x') {
      base = 16;
    } else if (p == 'o') {
      base = 8;
    } else if (p == 'b') {
      base = 2;
    }
    if (base != 10) {
      s.erase(0, 2);
    }
  }
  s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
  if (s.empty()) {
    return std::nullopt;
  }
  unsigned long long magnitude = 0;  // NOLINT(runtime/int)
  for (char ch : s) {
    int d = -1;
    if (ch >= '0' && ch <= '9') {
      d = ch - '0';
    } else if (ch >= 'a' && ch <= 'f') {
      d = ch - 'a' + 10;
    } else if (ch >= 'A' && ch <= 'F') {
      d = ch - 'A' + 10;
    }
    if (d < 0 || d >= base) {
      return std::nullopt;
    }
    const auto ub = static_cast<unsigned long long>(base);  // NOLINT(runtime/int)
    const auto ud = static_cast<unsigned long long>(d);  // NOLINT(runtime/int)
    if (magnitude > (std::numeric_limits<unsigned long long>::max() - ud) / ub) {  // NOLINT
      return std::nullopt;
    }
    magnitude = magnitude * ub + ud;
  }
  using U = unsigned long long;  // NOLINT(runtime/int)
  const U unsigned_max = bits >= 64 ? std::numeric_limits<U>::max() : (U(1) << bits) - 1;
  const U positive_max = is_signed ? unsigned_max >> 1 : unsigned_max;
  const U negative_max = is_signed ? positive_max + 1 : 0;  // magnitude
  if (magnitude > (negative ? negative_max : positive_max)) {
    return std::nullopt;
  }
  if (magnitude == 0) {
    return std::string("0");
  }
  return (negative ? "-" : "") + std::to_string(magnitude);
}

std::optional<std::string> parse_float(const std::string & raw, bool single)
{
  std::string s = trim(raw);
  s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
  static const std::regex rr_float(
    R"(^[+\-]?(?:(?:0|[1-9]\d*)(?:\.\d*)?|(?:\.\d+))(?:[eE][+\-]?\d+)?$)");
  std::istringstream in(s);
  in.imbue(std::locale::classic());
  double v = 0.0;
  in >> v;
  if (in.fail() || !in.eof() || !std::isfinite(v)) {
    return std::nullopt;  // inf / nan have no Robot Raconteur constant form
  }
  if (single && std::fabs(v) > static_cast<double>(std::numeric_limits<float>::max())) {
    return std::nullopt;
  }
  if (std::regex_match(s, rr_float)) {
    return s;
  }
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out.precision(single ? 9 : 17);
  out << v;
  return out.str();
}

// rosidl's string constant rule: matching outer quotes are removed and
// escaped inner quotes unescaped.
std::optional<std::string> parse_string(std::string s)
{
  for (char quote : {'"', '\''}) {
    if (s.size() >= 2 && s.front() == quote && s.back() == quote) {
      s = s.substr(1, s.size() - 2);
      std::string out;
      for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == quote) {
          return std::nullopt;  // unescaped inner quote: rosidl rejects
        }
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == quote) {
          out += quote;
          ++i;
        } else {
          out += s[i];
        }
      }
      s = out;
      break;
    }
  }
  return "\"" + RobotRaconteur::ConstantDefinition::EscapeString(s) + "\"";
}

// "constant <type> <NAME> <value>", or nothing for a constant Robot
// Raconteur cannot express (non-finite floats).
std::optional<std::string> constant_line(const Constant & c)
{
  struct IntType
  {
    const char * ros;
    const char * rr;
    bool is_signed;
    unsigned bits;
  };
  // 8-bit constants are widened to 16 bits: Robot Raconteur master verifies
  // int8/uint8 constant values with lexical_cast into a char type, which
  // accepts only one-character values (docs/upstream_issues.md).
  static const IntType ints[] = {
    {"byte", "uint16", false, 8},
    {"char", "uint16", false, 8},
    {"uint8", "uint16", false, 8},
    {"int8", "int16", true, 8},
    {"uint16", "uint16", false, 16},
    {"int16", "int16", true, 16},
    {"uint32", "uint32", false, 32},
    {"int32", "int32", true, 32},
    {"uint64", "uint64", false, 64},
    {"int64", "int64", true, 64},
  };
  const std::string name = fixname(c.name);
  std::optional<std::string> value;
  std::string rr_type;
  if (c.type == "bool") {
    // Robot Raconteur has no bool constants.
    const std::string v = lower(trim(c.value));
    if (v == "true" || v == "1") {
      value = "1";
    } else if (v == "false" || v == "0") {
      value = "0";
    }
    rr_type = "uint16";
  } else if (c.type == "float32" || c.type == "float64") {
    rr_type = c.type == "float32" ? "single" : "double";
    value = parse_float(c.value, c.type == "float32");
  } else if (c.type == "string" || c.type == "wstring") {
    rr_type = "string";
    value = parse_string(c.value);
  } else {
    for (const auto & it : ints) {
      if (c.type == it.ros) {
        rr_type = it.rr;
        value = parse_integer(c.value, it.is_signed, it.bits);
        break;
      }
    }
    if (rr_type.empty()) {
      return std::nullopt;  // rosidl allows only primitive constant types
    }
  }
  if (!value) {
    return std::nullopt;
  }
  return std::string("constant ") + rr_type + " " + name + " " + *value;
}

// ---------------------------------------------------------------------------

std::pair<std::string, std::string> split_namespace(const ti::MessageMembers * m)
{
  // "pkg::msg" -> ("pkg", "msg")
  const std::string ns = m->message_namespace_;
  const auto sep = ns.find("::");
  if (sep == std::string::npos) {
    throw TypeMapperError("unexpected message namespace '" + ns + "'");
  }
  return {ns.substr(0, sep), ns.substr(sep + 2)};
}

bool is_placeholder_only(const ti::MessageMembers * m)
{
  // rosidl gives empty messages one placeholder member.
  return m->member_count_ == 1 &&
         std::string(m->members_[0].name_) == "structure_needs_at_least_one_member";
}

}  // namespace

// ---------------------------------------------------------------------------

const char * InterfaceType::subfolder() const
{
  switch (kind) {
    case InterfaceKind::Message: return "msg";
    case InterfaceKind::Service: return "srv";
    case InterfaceKind::Action: return "action";
  }
  return "msg";
}

std::string InterfaceType::full_name() const
{
  return package + "/" + subfolder() + "/" + name;
}

InterfaceType parse_interface_type(
  const std::string & type, std::optional<InterfaceKind> default_kind)
{
  static const std::regex re(R"(^([a-z][a-z0-9_]*)/(?:(msg|srv|action)/)?([A-Z][A-Za-z0-9]*)$)");
  std::smatch m;
  if (!std::regex_match(type, m, re)) {
    throw TypeMapperError("invalid ROS interface type '" + type + "'");
  }
  InterfaceType t;
  t.package = m[1];
  t.name = m[3];
  const std::string kind = m[2];
  if (kind == "msg") {
    t.kind = InterfaceKind::Message;
  } else if (kind == "srv") {
    t.kind = InterfaceKind::Service;
  } else if (kind == "action") {
    t.kind = InterfaceKind::Action;
  } else if (default_kind) {
    t.kind = *default_kind;
  } else {
    throw TypeMapperError("interface type '" + type + "' needs msg/, srv/ or action/");
  }
  if (default_kind && t.kind != *default_kind) {
    throw TypeMapperError("interface type '" + type + "' is not of the expected kind");
  }
  return t;
}

std::string fixname(const std::string & name)
{
  // The union of the upstream bridge's list (which includes "as") and the
  // names Robot Raconteur's VerifyName rejects.
  static const std::set<std::string> reserved = {
    "object", "end", "option", "service", "struct", "import", "implements", "field",
    "property", "function", "event", "objref", "pipe", "callback", "wire", "memory",
    "void", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64",
    "single", "double", "varvalue", "varobject", "exception", "using", "as",
    "constant", "enum", "pod", "namedarray", "cdouble", "csingle", "bool", "stdver",
    "string", "this", "self", "Me"};
  const std::string l = lower(name);
  if (reserved.count(name) != 0 || starts_with(l, "rr") || starts_with(l, "robotraconteur") ||
    starts_with(l, "get_") || starts_with(l, "set_") || starts_with(l, "async_"))
  {
    return "ros" + name;
  }
  return name;
}

std::string message_definition_name(const std::string & package, const std::string & name)
{
  return "rosmsg_" + package + "__" + name;
}

std::string topic_definition_name(const std::string & package, const std::string & name)
{
  return "rostopic_" + package + "__" + name;
}

std::string service_definition_name(const std::string & package, const std::string & name)
{
  return "rosservice_" + package + "__" + name;
}

std::string action_definition_name(const std::string & package, const std::string & name)
{
  return "rosaction_" + package + "__" + name;
}

std::vector<std::string> package_interfaces(const std::string & package)
{
  std::string content;
  if (!ament_index_cpp::get_resource("rosidl_interfaces", package, content)) {
    throw TypeMapperError("package '" + package + "' has no ROS interfaces in the ament index");
  }
  // Lines such as "msg/Bool.idl", "msg/Bool.msg"; every interface has .idl.
  std::set<std::string> out;
  std::istringstream in(content);
  std::string line;
  static const std::regex re(R"(^(msg|srv|action)/([A-Za-z0-9]+)\.idl\r?$)");
  while (std::getline(in, line)) {
    std::smatch m;
    if (std::regex_match(line, m, re)) {
      out.insert(package + "/" + m[1].str() + "/" + m[2].str());
    }
  }
  return {out.begin(), out.end()};
}

std::vector<std::string> interface_packages()
{
  std::vector<std::string> out;
  for (const auto & kv : ament_index_cpp::get_resources("rosidl_interfaces")) {
    out.push_back(kv.first);
  }
  std::sort(out.begin(), out.end());
  return out;
}

void verify_definitions(const std::vector<std::string> & texts)
{
  std::vector<RobotRaconteur::ServiceDefinitionPtr> defs;
  try {
    for (const auto & text : texts) {
      auto d = RR_MAKE_SHARED<RobotRaconteur::ServiceDefinition>();
      d->FromString(text);
      defs.push_back(d);
    }
    RobotRaconteur::VerifyServiceDefinitions(defs);
  } catch (const std::exception & e) {
    throw TypeMapperError(std::string("Robot Raconteur rejects the definition: ") + e.what());
  }
}

// ---------------------------------------------------------------------------

namespace
{

class Generator
{
public:
  using MessageFn = std::function<std::string(const ti::MessageMembers *)>;

  explicit Generator(MessageFn message)
  : message_(std::move(message)) {}

  // "struct <name> ... end" for the members, recording imports.
  std::string struct_text(
    const ti::MessageMembers * m, const std::string & name, const Section & constants)
  {
    std::ostringstream o;
    o << "struct " << fixname(name) << "\n";
    std::set<std::string> used;
    for (const auto & c : constants) {
      const auto line = constant_line(c);
      if (!line) {
        continue;
      }
      if (!used.insert(fixname(c.name)).second) {
        throw TypeMapperError("duplicate member name '" + fixname(c.name) + "' in " + name);
      }
      o << kIndent << *line << "\n";
    }
    if (!is_placeholder_only(m)) {
      for (uint32_t i = 0; i < m->member_count_; ++i) {
        const auto & f = m->members_[i];
        const std::string fname = fixname(f.name_);
        if (!used.insert(fname).second) {
          throw TypeMapperError("duplicate member name '" + fname + "' in " + name);
        }
        o << kIndent << "field " << field_type(f, name) << " " << fname << "\n";
      }
    }
    o << "end\n";
    return o.str();
  }

  std::string imports_text() const
  {
    std::string s;
    for (const auto & i : imports) {
      s += "import " + i + "\n";
    }
    return s.empty() ? s : s + "\n";
  }

  std::vector<std::string> imports;

private:
  std::string field_type(const ti::MessageMember & f, const std::string & owner)
  {
    std::string base;
    bool numeric = true;
    switch (f.type_id_) {
      case ti::ROS_TYPE_FLOAT: base = "single"; break;
      case ti::ROS_TYPE_DOUBLE: base = "double"; break;
      case ti::ROS_TYPE_CHAR: base = "uint8"; break;
      case ti::ROS_TYPE_WCHAR: base = "uint16"; break;
      case ti::ROS_TYPE_BOOLEAN: base = "bool"; break;
      case ti::ROS_TYPE_OCTET: base = "uint8"; break;
      case ti::ROS_TYPE_UINT8: base = "uint8"; break;
      case ti::ROS_TYPE_INT8: base = "int8"; break;
      case ti::ROS_TYPE_UINT16: base = "uint16"; break;
      case ti::ROS_TYPE_INT16: base = "int16"; break;
      case ti::ROS_TYPE_UINT32: base = "uint32"; break;
      case ti::ROS_TYPE_INT32: base = "int32"; break;
      case ti::ROS_TYPE_UINT64: base = "uint64"; break;
      case ti::ROS_TYPE_INT64: base = "int64"; break;
      case ti::ROS_TYPE_STRING:
      case ti::ROS_TYPE_WSTRING:
        base = "string";
        numeric = false;
        break;
      case ti::ROS_TYPE_MESSAGE: {
          const auto * nested = static_cast<const ti::MessageMembers *>(f.members_->data);
          const std::string def = message_(nested);
          if (std::find(imports.begin(), imports.end(), def) == imports.end()) {
            imports.push_back(def);
          }
          base = def + "." + fixname(nested->message_name_);
          numeric = false;
          break;
        }
      default:
        throw TypeMapperError(
          std::string("field '") + f.name_ + "' of " + owner + " has a type (" +
          std::to_string(f.type_id_) + ") that Robot Raconteur cannot represent");
    }
    if (!f.is_array_) {
      return base;
    }
    if (!numeric) {
      return base + "{list}";
    }
    if (!f.is_upper_bound_ && f.array_size_ > 0) {
      return base + "[" + std::to_string(f.array_size_) + "]";
    }
    return base + "[]";  // unbounded or bounded sequence; bounds checked on write
  }

  MessageFn message_;
};

std::string header(const std::string & def_name)
{
  return "service " + def_name + "\n\nstdver 0.10\n\n";
}

}  // namespace

TypeMapper::TypeMapper() = default;
TypeMapper::~TypeMapper() = default;

std::shared_ptr<rcpputils::SharedLibrary> TypeMapper::library_locked(const InterfaceType & t)
{
  auto & lib = libraries_[t.package];
  if (!lib) {
    try {
      lib = rclcpp::get_typesupport_library(t.full_name(), ti::typesupport_identifier);
    } catch (const std::exception & e) {
      libraries_.erase(t.package);
      throw TypeMapperError(
              "cannot load the type support for '" + t.full_name() + "': " + e.what());
    }
  }
  return lib;
}

const void * TypeMapper::load_message_locked(const InterfaceType & t, const std::string & name)
{
  auto lib = library_locked(t);
  const std::string full = t.package + "/" + t.subfolder() + "/" + name;
  try {
    return rclcpp::get_message_typesupport_handle(full, ti::typesupport_identifier, *lib)->data;
  } catch (const std::exception & e) {
    throw TypeMapperError("unknown ROS type '" + full + "': " + e.what());
  }
}

std::string TypeMapper::message(const std::string & type)
{
  const auto t = parse_interface_type(type, InterfaceKind::Message);
  std::lock_guard<std::mutex> lock(mutex_);
  return message_locked(t);
}

std::string TypeMapper::message_locked(const InterfaceType & t)
{
  const std::string def_name = message_definition_name(t.package, t.name);
  if (definitions_.count(def_name) != 0) {
    return def_name;
  }
  return members_locked(load_message_locked(t, t.name));
}

// Nested types are reached through the parent's type support.
std::string TypeMapper::members_locked(const void * members)
{
  const auto * m = static_cast<const ti::MessageMembers *>(members);
  const auto [package, subfolder] = split_namespace(m);
  if (subfolder != "msg") {
    throw TypeMapperError(
            "nested type " + package + "/" + subfolder + "/" + m->message_name_ +
            " is not a message");
  }
  InterfaceType t;
  t.package = package;
  t.name = m->message_name_;
  const std::string def_name = message_definition_name(t.package, t.name);
  if (definitions_.count(def_name) != 0) {
    return def_name;
  }
  const auto sections = read_constants(t);
  Generator g([this](const ti::MessageMembers * mm) {return members_locked(mm);});
  const std::string body = g.struct_text(m, t.name, sections.empty() ? Section() : sections[0]);
  definitions_[def_name] = Entry{header(def_name) + g.imports_text() + body, g.imports};
  return def_name;
}

std::string TypeMapper::topic(const std::string & message_type)
{
  const auto t = parse_interface_type(message_type, InterfaceKind::Message);
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string def_name = topic_definition_name(t.package, t.name);
  if (definitions_.count(def_name) != 0) {
    return def_name;
  }
  const std::string msg = message_locked(t);
  const std::string ty = msg + "." + fixname(t.name);
  std::ostringstream o;
  o << header(def_name) << "import " << msg << "\n\n"
    << "object subscriber\n"
    << kIndent << "pipe " << ty << " subscriberpipe [readonly]\n"
    << kIndent << "wire " << ty << " subscriberwire [readonly]\n"
    << kIndent << "function void unsubscribe()\n"
    << "end\n\n"
    << "object publisher\n"
    << kIndent << "function void publish(" << ty << " m)\n"
    << kIndent << "pipe " << ty << " publisherpipe [writeonly]\n"
    << kIndent << "wire " << ty << " publisherwire [writeonly]\n"
    << "end\n";
  definitions_[def_name] = Entry{o.str(), {msg}};
  return def_name;
}

std::string TypeMapper::service(const std::string & type)
{
  const auto t = parse_interface_type(type, InterfaceKind::Service);
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string def_name = service_definition_name(t.package, t.name);
  if (definitions_.count(def_name) != 0) {
    return def_name;
  }
  auto lib = library_locked(t);
  const rosidl_service_type_support_t * ts = nullptr;
  try {
    ts = rclcpp::get_service_typesupport_handle(t.full_name(), ti::typesupport_identifier, *lib);
  } catch (const std::exception & e) {
    throw TypeMapperError("unknown ROS type '" + t.full_name() + "': " + e.what());
  }
  const auto * s = static_cast<const ti::ServiceMembers *>(ts->data);
  const auto sections = read_constants(t);
  auto section = [&](size_t i) {return i < sections.size() ? sections[i] : Section();};

  Generator g([this](const ti::MessageMembers * mm) {return members_locked(mm);});
  const std::string req = fixname(t.name + "Request");
  const std::string res = fixname(t.name + "Response");
  const std::string structs =
    g.struct_text(s->request_members_, t.name + "Request", section(0)) + "\n" +
    g.struct_text(s->response_members_, t.name + "Response", section(1));
  std::ostringstream o;
  o << header(def_name) << g.imports_text() << structs << "\n"
    << "object rosclient\n"
    << kIndent << "function " << res << " call(" << req << " request)\n"
    << kIndent << "function bool wait_for_service(double timeout)\n"
    << "end\n\n"
    << "object rosservice\n"
    << kIndent << "callback " << res << " servicefunction(" << req << " request)\n"
    << "end\n";
  definitions_[def_name] = Entry{o.str(), g.imports};
  return def_name;
}

std::string TypeMapper::action(const std::string & type)
{
  const auto t = parse_interface_type(type, InterfaceKind::Action);
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string def_name = action_definition_name(t.package, t.name);
  if (definitions_.count(def_name) != 0) {
    return def_name;
  }
  auto part = [&](const char * suffix) {
      return static_cast<const ti::MessageMembers *>(load_message_locked(t, t.name + suffix));
    };
  const auto * goal = part("_Goal");
  const auto * result = part("_Result");
  const auto * feedback = part("_Feedback");
  const auto sections = read_constants(t);
  auto section = [&](size_t i) {return i < sections.size() ? sections[i] : Section();};

  Generator g([this](const ti::MessageMembers * mm) {return members_locked(mm);});
  const std::string goal_s = fixname(t.name + "Goal");
  const std::string result_s = fixname(t.name + "Result");
  const std::string feedback_s = fixname(t.name + "Feedback");
  const std::string status_s = fixname(t.name + "GoalStatus");
  const std::string structs =
    g.struct_text(goal, t.name + "Goal", section(0)) + "\n" +
    g.struct_text(result, t.name + "Result", section(1)) + "\n" +
    g.struct_text(feedback, t.name + "Feedback", section(2));
  std::ostringstream o;
  o << header(def_name) << g.imports_text() << structs << "\n"
    // One goal's progress, as yielded by the send_goal generator (B8). The
    // status values are action_msgs/GoalStatus's.
    << "struct " << status_s << "\n"
    << kIndent << "constant int16 STATUS_UNKNOWN 0\n"
    << kIndent << "constant int16 STATUS_ACCEPTED 1\n"
    << kIndent << "constant int16 STATUS_EXECUTING 2\n"
    << kIndent << "constant int16 STATUS_CANCELING 3\n"
    << kIndent << "constant int16 STATUS_SUCCEEDED 4\n"
    << kIndent << "constant int16 STATUS_CANCELED 5\n"
    << kIndent << "constant int16 STATUS_ABORTED 6\n"
    << kIndent << "field uint8[16] goal_id\n"
    << kIndent << "field int8 status\n"
    << kIndent << "field " << feedback_s << " feedback\n"
    << kIndent << "field " << result_s << " result\n"
    << "end\n\n"
    << "object rosactionclient\n"
    << kIndent << "function " << status_s << "{generator} send_goal(" << goal_s << " goal)\n"
    << kIndent << "function bool wait_for_server(double timeout)\n"
    << "end\n";
  definitions_[def_name] = Entry{o.str(), g.imports};
  return def_name;
}

std::string TypeMapper::interface(const std::string & type)
{
  const auto t = parse_interface_type(type);
  switch (t.kind) {
    case InterfaceKind::Message: return topic(type);
    case InterfaceKind::Service: return service(type);
    case InterfaceKind::Action: return action(type);
  }
  return std::string();
}

std::string TypeMapper::definition(const std::string & definition_name) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = definitions_.find(definition_name);
  if (it == definitions_.end()) {
    throw TypeMapperError("no generated definition '" + definition_name + "'");
  }
  return it->second.text;
}

std::vector<std::string> TypeMapper::closure(const std::string & definition_name) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> out;
  closure_locked(definition_name, out);
  return out;
}

void TypeMapper::closure_locked(const std::string & name, std::vector<std::string> & out) const
{
  if (std::find(out.begin(), out.end(), name) != out.end()) {
    return;
  }
  const auto it = definitions_.find(name);
  if (it == definitions_.end()) {
    throw TypeMapperError("no generated definition '" + name + "'");
  }
  for (const auto & i : it->second.imports) {
    closure_locked(i, out);
  }
  out.push_back(name);
}

}  // namespace robotraconteur_ros2_debug_bridge
