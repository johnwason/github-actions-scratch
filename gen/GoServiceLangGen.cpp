// Copyright 2011-2026 Wason Technology, LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "GoServiceLangGen.h"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace RobotRaconteur;

namespace RobotRaconteurGen
{
namespace
{

// ---------------------------------------------------------------- context

enum FieldContext
{
    CtxNormal,    // structures, members: arrays are slices
    CtxPodField,  // pod fields: fixed length arrays are Go arrays
    CtxNamedArray // namedarray fields: arrays are Go arrays
};

struct GenContext
{
    RR_SHARED_PTR<ServiceDefinition> def;
    std::vector<RR_SHARED_PTR<ServiceDefinition> > alldefs;
    GoServiceLangGen::Options options;
    std::set<std::string> imports; // service names imported by generated code
    bool uses_time;
    bool uses_strconv;
    GenContext() : uses_time(false), uses_strconv(false) {}
};

const char* go_keywords[] = {"break",     "case",   "chan",   "const",  "continue", "default", "defer",
                             "else",      "fallthrough", "for", "func",   "go",       "goto",    "if",
                             "import",    "interface", "map",   "package", "range",   "return",  "select",
                             "struct",    "switch", "type",   "var",    "string",   "error",   "any",
                             "bool",      "int",    "uint",   "len",    "cap",      "make",    "new",
                             "nil",       "true",   "false",  "append", "copy",     "byte",    "rune"};

std::string fix_local(const std::string& name)
{
    BOOST_FOREACH (const char* k, go_keywords)
    {
        if (name == k)
            return name + "_";
    }
    if (boost::starts_with(name, "rr_"))
        return name + "_";
    return name;
}

std::string exported(const std::string& name)
{
    if (name.empty())
        return name;
    std::string o = name;
    if (o[0] == '_')
        o = "X" + o;
    o[0] = static_cast<char>(toupper(o[0]));
    return o;
}

std::string lower_first(const std::string& name)
{
    if (name.empty())
        return name;
    std::string o = name;
    o[0] = static_cast<char>(tolower(o[0]));
    return o;
}

std::string go_string_literal(const std::string& s)
{
    std::ostringstream o;
    o << "\"";
    for (size_t i = 0; i < s.size(); i++)
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c)
        {
        case '"':
            o << "\\\"";
            break;
        case '\\':
            o << "\\\\";
            break;
        case '\n':
            o << "\\n";
            break;
        case '\r':
            o << "\\r";
            break;
        case '\t':
            o << "\\t";
            break;
        default:
            if (c < 0x20 || c == 0x7f)
            {
                static const char* hex = "0123456789abcdef";
                o << "\\x" << hex[c >> 4] << hex[c & 0xf];
            }
            else
            {
                o << s[i];
            }
        }
    }
    o << "\"";
    return o.str();
}

// raw string literal for the embedded service definition
std::string go_raw_string(const std::string& s)
{
    if (s.find('`') == std::string::npos)
        return "`" + s + "`";
    std::string o = "`";
    BOOST_FOREACH (char c, s)
    {
        if (c == '`')
            o += "` + \"`\" + `";
        else
            o += c;
    }
    return o + "`";
}

std::string qualified_name(const RR_SHARED_PTR<ServiceDefinition>& d, const std::string& name)
{
    return d->Name + "." + name;
}

RR_SHARED_PTR<NamedTypeDefinition> resolve(GenContext& ctx, const TypeDefinition& t)
{
    RR_SHARED_PTR<NamedTypeDefinition> nt = t.ResolveNamedType_cache.lock();
    if (nt)
        return nt;
    TypeDefinition& t2 = const_cast<TypeDefinition&>(t);
    return t2.ResolveNamedType(ctx.alldefs);
}

// Go reference to a named type (struct/pod/namedarray/enum/object) from the
// generated package, adding an import if it is defined in another service.
std::string go_named_ref(GenContext& ctx, const std::string& service_name, const std::string& type_name)
{
    if (service_name == ctx.def->Name)
        return exported(type_name);
    ctx.imports.insert(service_name);
    return GoServiceLangGen::package_name(service_name) + "." + exported(type_name);
}

std::string go_named_ref(GenContext& ctx, const RR_SHARED_PTR<NamedTypeDefinition>& nt)
{
    std::string q = nt->ResolveQualifiedName();
    std::string ns, name;
    size_t p = q.rfind('.');
    ns = q.substr(0, p);
    name = q.substr(p + 1);
    return go_named_ref(ctx, ns, name);
}

std::string go_object_ref(GenContext& ctx, const std::string& objecttype, const RR_SHARED_PTR<ServiceDefinition>& d)
{
    if (objecttype == "varobject")
        return "any";
    std::string q = objecttype;
    if (q.find('.') == std::string::npos)
    {
        // resolve "using" names
        BOOST_FOREACH (const RR_SHARED_PTR<UsingDefinition>& u, d->Using)
        {
            if (u->UnqualifiedName == q)
            {
                q = u->QualifiedName;
                break;
            }
        }
    }
    if (q.find('.') == std::string::npos)
        return exported(q);
    size_t p = q.rfind('.');
    return go_named_ref(ctx, q.substr(0, p), q.substr(p + 1));
}

std::string qualified_object_type(const std::string& objecttype, const RR_SHARED_PTR<ServiceDefinition>& d)
{
    if (objecttype == "varobject")
        return "";
    if (objecttype.find('.') != std::string::npos)
        return objecttype;
    BOOST_FOREACH (const RR_SHARED_PTR<UsingDefinition>& u, d->Using)
    {
        if (u->UnqualifiedName == objecttype)
            return u->QualifiedName;
    }
    return d->Name + "." + objecttype;
}

std::string go_primitive(DataTypes t)
{
    switch (t)
    {
    case DataTypes_double_t:
        return "float64";
    case DataTypes_single_t:
        return "float32";
    case DataTypes_int8_t:
        return "int8";
    case DataTypes_uint8_t:
        return "uint8";
    case DataTypes_int16_t:
        return "int16";
    case DataTypes_uint16_t:
        return "uint16";
    case DataTypes_int32_t:
        return "int32";
    case DataTypes_uint32_t:
        return "uint32";
    case DataTypes_int64_t:
        return "int64";
    case DataTypes_uint64_t:
        return "uint64";
    case DataTypes_cdouble_t:
        return "complex128";
    case DataTypes_csingle_t:
        return "complex64";
    case DataTypes_bool_t:
        return "bool";
    case DataTypes_string_t:
        return "string";
    default:
        throw DataTypeException("Invalid primitive type");
    }
}

bool is_primitive_number(DataTypes t) { return IsTypeNumeric(t); }

int32_t array_count(const TypeDefinition& t)
{
    int32_t n = 1;
    BOOST_FOREACH (int32_t l, t.ArrayLength)
    {
        n *= l;
    }
    return n;
}

std::string dims_literal(const TypeDefinition& t)
{
    std::vector<std::string> d;
    BOOST_FOREACH (int32_t l, t.ArrayLength)
    {
        d.push_back(boost::lexical_cast<std::string>(l));
    }
    return boost::join(d, ", ");
}

bool use_go_array(const TypeDefinition& t, FieldContext fc)
{
    return (fc == CtxPodField || fc == CtxNamedArray) && t.ArrayType == DataTypes_ArrayTypes_array &&
           !t.ArrayVarLength && t.ContainerType == DataTypes_ContainerTypes_none && !t.ArrayLength.empty() &&
           t.ArrayLength.at(0) > 0;
}

// Go type of a value of type t
std::string go_type(GenContext& ctx, const TypeDefinition& t, FieldContext fc = CtxNormal)
{
    TypeDefinition t1;
    t.CopyTo(t1);
    t1.RemoveContainers();
    std::string base;

    if (t1.Type == DataTypes_varvalue_t)
    {
        base = "any";
    }
    else if (t1.Type == DataTypes_void_t)
    {
        base = "";
    }
    else if (t1.Type == DataTypes_namedtype_t)
    {
        RR_SHARED_PTR<NamedTypeDefinition> nt = resolve(ctx, t1);
        std::string ref = go_named_ref(ctx, nt);
        switch (nt->RRDataType())
        {
        case DataTypes_structure_t:
            base = "*" + ref;
            break;
        case DataTypes_enum_t:
            base = ref;
            break;
        case DataTypes_pod_t:
            if (t1.ArrayType == DataTypes_ArrayTypes_multidimarray)
                base = "*rr.PodMultiDimArray[" + ref + "]";
            else if (t1.ArrayType == DataTypes_ArrayTypes_array)
                base = use_go_array(t1, fc) ? "[" + boost::lexical_cast<std::string>(t1.ArrayLength.at(0)) + "]" + ref
                                            : "[]" + ref;
            else
                base = ref;
            break;
        case DataTypes_namedarray_t:
            if (t1.ArrayType == DataTypes_ArrayTypes_multidimarray)
                base = "*rr.NamedMultiDimArray[" + ref + "]";
            else if (t1.ArrayType == DataTypes_ArrayTypes_array)
                base = use_go_array(t1, fc) ? "[" + boost::lexical_cast<std::string>(t1.ArrayLength.at(0)) + "]" + ref
                                            : "[]" + ref;
            else
                base = ref;
            break;
        default:
            throw DataTypeException("Invalid named type for value");
        }
    }
    else if (t1.Type == DataTypes_string_t)
    {
        base = "string";
    }
    else
    {
        std::string p = go_primitive(t1.Type);
        if (t1.ArrayType == DataTypes_ArrayTypes_multidimarray)
            base = "*rr.MultiDimArray[" + p + "]";
        else if (t1.ArrayType == DataTypes_ArrayTypes_array)
            base = use_go_array(t1, fc) ? "[" + boost::lexical_cast<std::string>(t1.ArrayLength.at(0)) + "]" + p
                                        : "[]" + p;
        else
            base = p;
    }

    switch (t.ContainerType)
    {
    case DataTypes_ContainerTypes_none:
        return base;
    case DataTypes_ContainerTypes_list:
        return "[]" + base;
    case DataTypes_ContainerTypes_map_int32:
        return "map[int32]" + base;
    case DataTypes_ContainerTypes_map_string:
        return "map[string]" + base;
    default:
        throw DataTypeException("Invalid container type");
    }
}

// numeric element type of a namedarray
std::string namedarray_element_type(GenContext& ctx, const RR_SHARED_PTR<ServiceEntryDefinition>& e);
int namedarray_element_count(GenContext& ctx, const RR_SHARED_PTR<ServiceEntryDefinition>& e);

RR_SHARED_PTR<ServiceEntryDefinition> resolve_entry(GenContext& ctx, const TypeDefinition& t)
{
    RR_SHARED_PTR<ServiceEntryDefinition> e = RR_DYNAMIC_POINTER_CAST<ServiceEntryDefinition>(resolve(ctx, t));
    if (!e)
        throw DataTypeException("Expected service entry type " + t.TypeString);
    return e;
}

std::string namedarray_element_type(GenContext& ctx, const RR_SHARED_PTR<ServiceEntryDefinition>& e)
{
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (!p)
            continue;
        if (p->Type->Type == DataTypes_namedtype_t)
            return namedarray_element_type(ctx, resolve_entry(ctx, *p->Type));
        return go_primitive(p->Type->Type);
    }
    throw DataTypeException("Empty namedarray " + e->Name);
}

int namedarray_element_count(GenContext& ctx, const RR_SHARED_PTR<ServiceEntryDefinition>& e)
{
    int n = 0;
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (!p)
            continue;
        int c = p->Type->ArrayType == DataTypes_ArrayTypes_none ? 1 : array_count(*p->Type);
        if (p->Type->Type == DataTypes_namedtype_t)
            c *= namedarray_element_count(ctx, resolve_entry(ctx, *p->Type));
        n += c;
    }
    return n;
}

std::string len_args(const TypeDefinition& t)
{
    if (t.ArrayType != DataTypes_ArrayTypes_array || t.ArrayLength.empty() || t.ArrayLength.at(0) == 0)
        return "0, false";
    return boost::lexical_cast<std::string>(t.ArrayLength.at(0)) + ", " + (t.ArrayVarLength ? "true" : "false");
}

// Codec expression (rr.Codec[T]) for the slice/non-array form of t
std::string go_codec(GenContext& ctx, const TypeDefinition& t)
{
    if (t.ContainerType != DataTypes_ContainerTypes_none)
    {
        TypeDefinition t1;
        t.CopyTo(t1);
        t1.RemoveContainers();
        std::string inner = go_codec(ctx, t1);
        switch (t.ContainerType)
        {
        case DataTypes_ContainerTypes_list:
            return "rr.ListCodec(" + inner + ")";
        case DataTypes_ContainerTypes_map_int32:
            return "rr.MapInt32Codec(" + inner + ")";
        case DataTypes_ContainerTypes_map_string:
            return "rr.MapStringCodec(" + inner + ")";
        default:
            throw DataTypeException("Invalid container type");
        }
    }

    if (t.Type == DataTypes_varvalue_t)
        return "rr.VarValueCodec()";
    if (t.Type == DataTypes_string_t)
        return "rr.StringCodec()";
    if (is_primitive_number(t.Type))
    {
        std::string p = go_primitive(t.Type);
        switch (t.ArrayType)
        {
        case DataTypes_ArrayTypes_none:
            return "rr.ScalarCodec[" + p + "]()";
        case DataTypes_ArrayTypes_array:
            return "rr.ArrayCodec[" + p + "](" + len_args(t) + ")";
        case DataTypes_ArrayTypes_multidimarray:
            return "rr.MultiDimArrayCodec[" + p + "](" + (t.ArrayVarLength ? "" : dims_literal(t)) + ")";
        default:
            throw DataTypeException("Invalid array type");
        }
    }
    if (t.Type == DataTypes_namedtype_t)
    {
        RR_SHARED_PTR<NamedTypeDefinition> nt = resolve(ctx, t);
        std::string ref = go_named_ref(ctx, nt);
        switch (nt->RRDataType())
        {
        case DataTypes_structure_t:
            return "rr.StructureCodec[" + ref + "]()";
        case DataTypes_enum_t:
            return "rr.EnumCodec[" + ref + "]()";
        case DataTypes_pod_t:
            switch (t.ArrayType)
            {
            case DataTypes_ArrayTypes_none:
                return "rr.PodCodec[" + ref + "]()";
            case DataTypes_ArrayTypes_array:
                return "rr.PodArrayCodec[" + ref + "](" + len_args(t) + ")";
            case DataTypes_ArrayTypes_multidimarray:
                return "rr.PodMultiDimArrayCodec[" + ref + "](" + (t.ArrayVarLength ? "" : dims_literal(t)) + ")";
            default:
                throw DataTypeException("Invalid array type");
            }
        case DataTypes_namedarray_t: {
            std::string el = namedarray_element_type(ctx, RR_DYNAMIC_POINTER_CAST<ServiceEntryDefinition>(nt));
            switch (t.ArrayType)
            {
            case DataTypes_ArrayTypes_none:
                return "rr.NamedArrayCodec[" + ref + ", " + el + "]()";
            case DataTypes_ArrayTypes_array:
                return "rr.NamedArrayArrayCodec[" + ref + ", " + el + "](" + len_args(t) + ")";
            case DataTypes_ArrayTypes_multidimarray:
                return "rr.NamedArrayMultiDimArrayCodec[" + ref + ", " + el + "](" +
                       (t.ArrayVarLength ? "" : dims_literal(t)) + ")";
            default:
                throw DataTypeException("Invalid array type");
            }
        }
        default:
            throw DataTypeException("Invalid named type");
        }
    }
    throw DataTypeException("Invalid type for codec: " + const_cast<TypeDefinition&>(t).ToString());
}

std::string doc_comment(const std::string& doc, const std::string& indent = "")
{
    if (doc.empty())
        return "";
    std::vector<std::string> lines;
    boost::split(lines, doc, boost::is_any_of("\n"));
    std::string o;
    BOOST_FOREACH (const std::string& l, lines)
    {
        o += indent + "// " + boost::trim_right_copy(l) + "\n";
    }
    return o;
}

// unique exported field names within an entry
std::map<std::string, std::string> exported_names(const std::vector<RR_SHARED_PTR<MemberDefinition> >& members)
{
    std::map<std::string, std::string> o;
    std::set<std::string> used;
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, members)
    {
        std::string n = exported(m->Name);
        while (used.count(n))
            n += "_";
        used.insert(n);
        o[m->Name] = n;
    }
    return o;
}

// ---------------------------------------------------------------- data types

void GenerateStructure(GenContext& ctx, const RR_SHARED_PTR<ServiceEntryDefinition>& e, std::ostream& w)
{
    std::string name = exported(e->Name);
    std::string qname = qualified_name(ctx.def, e->Name);
    std::map<std::string, std::string> fields = exported_names(e->Members);
    w << doc_comment(e->DocString);
    w << "type " << name << " struct {\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (!p)
            continue;
        w << doc_comment(p->DocString, "\t");
        w << "\t" << fields[p->Name] << " " << go_type(ctx, *p->Type) << "\n";
    }
    w << "}\n\n";
    w << "// RRType returns the Robot Raconteur type name.\n";
    w << "func (s *" << name << ") RRType() string { return " << go_string_literal(qname) << " }\n\n";
    w << "// RRPack packs the structure fields.\n";
    w << "func (s *" << name << ") RRPack() ([]*rr.Element, error) {\n\tvar b rr.ElementBuilder\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (!p)
            continue;
        w << "\trr.PackField(&b, " << go_string_literal(p->Name) << ", s." << fields[p->Name] << ", "
          << go_codec(ctx, *p->Type) << ")\n";
    }
    w << "\treturn b.Result()\n}\n\n";
    w << "// RRUnpack unpacks the structure fields.\n";
    w << "func (s *" << name << ") RRUnpack(elems []*rr.Element) error {\n\tvar err error\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (!p)
            continue;
        w << "\tif s." << fields[p->Name] << ", err = rr.UnpackField(elems, " << go_string_literal(p->Name) << ", "
          << go_codec(ctx, *p->Type) << "); err != nil {\n\t\treturn err\n\t}\n";
    }
    w << "\treturn nil\n}\n\n";
}

// pack/unpack statements for a pod or namedarray field
void GeneratePodFieldPack(GenContext& ctx, const RR_SHARED_PTR<PropertyDefinition>& p, const std::string& field,
                          std::ostream& w)
{
    if (use_go_array(*p->Type, CtxPodField))
        w << "\trr.PackField(&b, " << go_string_literal(p->Name) << ", s." << field << "[:], " << go_codec(ctx, *p->Type)
          << ")\n";
    else
        w << "\trr.PackField(&b, " << go_string_literal(p->Name) << ", s." << field << ", " << go_codec(ctx, *p->Type)
          << ")\n";
}

void GeneratePodFieldUnpack(GenContext& ctx, const RR_SHARED_PTR<PropertyDefinition>& p, const std::string& field,
                            std::ostream& w)
{
    if (use_go_array(*p->Type, CtxPodField))
        w << "\tif err := rr.UnpackFieldInto(elems, " << go_string_literal(p->Name) << ", " << go_codec(ctx, *p->Type)
          << ", s." << field << "[:]); err != nil {\n\t\treturn err\n\t}\n";
    else
        w << "\tif s." << field << ", err = rr.UnpackField(elems, " << go_string_literal(p->Name) << ", "
          << go_codec(ctx, *p->Type) << "); err != nil {\n\t\treturn err\n\t}\n";
}

void GeneratePod(GenContext& ctx, const RR_SHARED_PTR<ServiceEntryDefinition>& e, std::ostream& w)
{
    std::string name = exported(e->Name);
    std::string qname = qualified_name(ctx.def, e->Name);
    std::map<std::string, std::string> fields = exported_names(e->Members);
    w << doc_comment(e->DocString);
    w << "type " << name << " struct {\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (!p)
            continue;
        w << doc_comment(p->DocString, "\t");
        w << "\t" << fields[p->Name] << " " << go_type(ctx, *p->Type, CtxPodField) << "\n";
    }
    w << "}\n\n";
    w << "// RRType returns the Robot Raconteur type name.\n";
    w << "func (" << name << ") RRType() string { return " << go_string_literal(qname) << " }\n\n";
    w << "// RRPackPod packs the pod fields.\n";
    w << "func (s " << name << ") RRPackPod() ([]*rr.Element, error) {\n\tvar b rr.ElementBuilder\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (p)
            GeneratePodFieldPack(ctx, p, fields[p->Name], w);
    }
    w << "\treturn b.Result()\n}\n\n";
    w << "// RRUnpackPod unpacks the pod fields.\n";
    w << "func (s *" << name << ") RRUnpackPod(elems []*rr.Element) error {\n\tvar err error\n\t_ = err\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (p)
            GeneratePodFieldUnpack(ctx, p, fields[p->Name], w);
    }
    w << "\treturn nil\n}\n\n";
}

void GenerateNamedArray(GenContext& ctx, const RR_SHARED_PTR<ServiceEntryDefinition>& e, std::ostream& w)
{
    std::string name = exported(e->Name);
    std::string qname = qualified_name(ctx.def, e->Name);
    std::map<std::string, std::string> fields = exported_names(e->Members);
    std::string el = namedarray_element_type(ctx, e);
    int count = namedarray_element_count(ctx, e);
    w << doc_comment(e->DocString);
    w << "type " << name << " struct {\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (!p)
            continue;
        w << doc_comment(p->DocString, "\t");
        w << "\t" << fields[p->Name] << " " << go_type(ctx, *p->Type, CtxNamedArray) << "\n";
    }
    w << "}\n\n";
    w << "// RRType returns the Robot Raconteur type name.\n";
    w << "func (" << name << ") RRType() string { return " << go_string_literal(qname) << " }\n\n";
    w << "// RRElementCount returns the number of " << el << " elements in the namedarray.\n";
    w << "func (" << name << ") RRElementCount() int { return " << count << " }\n\n";

    // RRToArray
    w << "// RRToArray writes the value to a numeric array.\n";
    w << "func (s " << name << ") RRToArray(a []" << el << ") {\n";
    int off = 0;
    std::ostringstream from;
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m);
        if (!p)
            continue;
        std::string f = fields[p->Name];
        bool arr = p->Type->ArrayType != DataTypes_ArrayTypes_none;
        int n = arr ? array_count(*p->Type) : 1;
        if (p->Type->Type == DataTypes_namedtype_t)
        {
            RR_SHARED_PTR<ServiceEntryDefinition> ne = resolve_entry(ctx, *p->Type);
            int c = namedarray_element_count(ctx, ne);
            if (arr)
            {
                for (int i = 0; i < n; i++)
                {
                    w << "\ts." << f << "[" << i << "].RRToArray(a[" << off + i * c << ":" << off + (i + 1) * c
                      << "])\n";
                    from << "\ts." << f << "[" << i << "].RRFromArray(a[" << off + i * c << ":" << off + (i + 1) * c
                         << "])\n";
                }
            }
            else
            {
                w << "\ts." << f << ".RRToArray(a[" << off << ":" << off + c << "])\n";
                from << "\ts." << f << ".RRFromArray(a[" << off << ":" << off + c << "])\n";
            }
            off += n * c;
        }
        else if (arr)
        {
            w << "\tcopy(a[" << off << ":" << off + n << "], s." << f << "[:])\n";
            from << "\tcopy(s." << f << "[:], a[" << off << ":" << off + n << "])\n";
            off += n;
        }
        else
        {
            w << "\ta[" << off << "] = s." << f << "\n";
            from << "\ts." << f << " = a[" << off << "]\n";
            off += 1;
        }
    }
    w << "}\n\n";
    w << "// RRFromArray reads the value from a numeric array.\n";
    w << "func (s *" << name << ") RRFromArray(a []" << el << ") {\n" << from.str() << "}\n\n";
}

// ---------------------------------------------------------------- constants and enums

std::string ConstantGoName(const std::string& prefix, const std::string& name) { return prefix + exported(name); }

void GenerateConstant(GenContext& ctx, const RR_SHARED_PTR<ConstantDefinition>& c,
                      const std::vector<RR_SHARED_PTR<ConstantDefinition> >& scope, const std::string& prefix,
                      std::ostream& w)
{
    RR_SHARED_PTR<TypeDefinition> t = c->Type;
    std::string name = ConstantGoName(prefix, c->Name);
    w << doc_comment(c->DocString);
    if (t->Type == DataTypes_namedtype_t)
    {
        std::vector<ConstantDefinition_StructField> fields = c->ValueToStructFields();
        std::ostringstream tdef, val;
        BOOST_FOREACH (const ConstantDefinition_StructField& f, fields)
        {
            RR_SHARED_PTR<ConstantDefinition> c3 = TryFindByName(scope, f.ConstantRefName);
            if (!c3)
                throw ServiceException("Invalid structure constant " + c->Name);
            std::string ctype;
            if (c3->Type->Type == DataTypes_namedtype_t)
                ctype = ConstantGoName(prefix, c3->Name) + "Type";
            else if (c3->Type->Type == DataTypes_string_t)
                ctype = "string";
            else if (c3->Type->ArrayType == DataTypes_ArrayTypes_array)
                ctype = "[]" + go_primitive(c3->Type->Type);
            else
                ctype = go_primitive(c3->Type->Type);
            tdef << "\t" << exported(f.Name) << " " << ctype << "\n";
            val << "\t" << exported(f.Name) << ": " << ConstantGoName(prefix, c3->Name) << ",\n";
        }
        w << "type " << name << "Type struct {\n" << tdef.str() << "}\n\n";
        w << "var " << name << " = " << name << "Type{\n" << val.str() << "}\n\n";
        return;
    }
    if (t->Type == DataTypes_string_t)
    {
        w << "const " << name << " = " << go_string_literal(c->ValueToString()) << "\n\n";
        return;
    }
    std::string p = go_primitive(t->Type);
    if (t->ArrayType == DataTypes_ArrayTypes_none)
    {
        w << "const " << name << " " << p << " = " << boost::trim_copy(c->Value) << "\n\n";
    }
    else
    {
        std::string v = boost::trim_copy(c->Value);
        if (boost::starts_with(v, "{") && boost::ends_with(v, "}"))
            v = v.substr(1, v.size() - 2);
        w << "var " << name << " = []" << p << "{" << v << "}\n\n";
    }
}

void GenerateEnum(GenContext& ctx, const RR_SHARED_PTR<EnumDefinition>& e, std::ostream& w)
{
    std::string name = exported(e->Name);
    w << doc_comment(e->DocString);
    w << "type " << name << " int32\n\n";
    w << "// Values of " << name << ".\nconst (\n";
    BOOST_FOREACH (const EnumDefinitionValue& v, e->Values)
    {
        w << doc_comment(v.DocString, "\t");
        w << "\t" << name << "_" << v.Name << " " << name << " = " << v.Value << "\n";
    }
    w << ")\n\n";
}

void GenerateException(GenContext& ctx, const RR_SHARED_PTR<ExceptionDefinition>& e, std::ostream& w)
{
    std::string name = exported(e->Name);
    std::string qname = qualified_name(ctx.def, e->Name);
    w << doc_comment(e->DocString);
    w << "type " << name << " struct {\n\tE *rr.Error\n}\n\n";
    w << "// New" << name << " returns a new " << qname << " error.\n";
    w << "func New" << name << "(message string) *" << name << " {\n";
    w << "\treturn &" << name << "{E: &rr.Error{Code: rr.CodeRemoteError, Name: " << go_string_literal(qname)
      << ", Message: message}}\n}\n\n";
    w << "func (e *" << name << ") Error() string { return e.E.Error() }\n\n";
    w << "// Unwrap returns the underlying *rr.Error.\n";
    w << "func (e *" << name << ") Unwrap() error { return e.E }\n\n";
}

// ---------------------------------------------------------------- objects

std::string params_decl(GenContext& ctx, const std::vector<RR_SHARED_PTR<TypeDefinition> >& params)
{
    std::vector<std::string> o;
    BOOST_FOREACH (const RR_SHARED_PTR<TypeDefinition>& p, params)
    {
        o.push_back(fix_local(p->Name) + " " + go_type(ctx, *p));
    }
    return boost::join(o, ", ");
}

std::string params_names(const std::vector<RR_SHARED_PTR<TypeDefinition> >& params)
{
    std::vector<std::string> o;
    BOOST_FOREACH (const RR_SHARED_PTR<TypeDefinition>& p, params)
    {
        o.push_back(fix_local(p->Name));
    }
    return boost::join(o, ", ");
}

std::string params_types(GenContext& ctx, const std::vector<RR_SHARED_PTR<TypeDefinition> >& params)
{
    std::vector<std::string> o;
    BOOST_FOREACH (const RR_SHARED_PTR<TypeDefinition>& p, params)
    {
        o.push_back(fix_local(p->Name) + " " + go_type(ctx, *p));
    }
    return boost::join(o, ", ");
}

bool is_void(const RR_SHARED_PTR<TypeDefinition>& t) { return !t || t->Type == DataTypes_void_t; }

std::string ret_sig(GenContext& ctx, const RR_SHARED_PTR<TypeDefinition>& rt)
{
    if (is_void(rt))
        return "error";
    return "(" + go_type(ctx, *rt) + ", error)";
}

std::string event_func_type(GenContext& ctx, const RR_SHARED_PTR<EventDefinition>& ev)
{
    return "func(" + params_types(ctx, ev->Parameters) + ")";
}

std::string callback_func_type(GenContext& ctx, const RR_SHARED_PTR<CallbackDefinition>& cb)
{
    return "func(" + params_types(ctx, cb->Parameters) + ") " + ret_sig(ctx, cb->ReturnType);
}

bool objref_index_type(const RR_SHARED_PTR<ObjRefDefinition>& o, std::string& ind)
{
    switch (o->ArrayType)
    {
    case DataTypes_ArrayTypes_none:
        switch (o->ContainerType)
        {
        case DataTypes_ContainerTypes_none:
            return false;
        case DataTypes_ContainerTypes_map_int32:
            ind = "int32";
            return true;
        case DataTypes_ContainerTypes_map_string:
            ind = "string";
            return true;
        default:
            throw DataTypeException("Invalid objref container type");
        }
    case DataTypes_ArrayTypes_array:
        ind = "int32";
        return true;
    default:
        throw DataTypeException("Invalid objref array type");
    }
}

void GenerateObjectInterface(GenContext& ctx, const RR_SHARED_PTR<ServiceEntryDefinition>& e, std::ostream& w)
{
    std::string name = exported(e->Name);
    std::map<std::string, std::string> mn = exported_names(e->Members);
    w << doc_comment(e->DocString);
    if (e->DocString.empty())
        w << "// " << name << " is the " << qualified_name(ctx.def, e->Name) << " object type.\n";
    w << "type " << name << " interface {\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        std::string n = mn[m->Name];
        w << doc_comment(m->DocString, "\t");
        if (RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m))
        {
            std::string t = go_type(ctx, *p->Type);
            if (p->Direction() != MemberDefinition_Direction_writeonly)
                w << "\tGet" << n << "() (" << t << ", error)\n";
            if (p->Direction() != MemberDefinition_Direction_readonly)
                w << "\tSet" << n << "(value " << t << ") error\n";
        }
        else if (RR_SHARED_PTR<FunctionDefinition> f = RR_DYNAMIC_POINTER_CAST<FunctionDefinition>(m))
        {
            if (f->IsGenerator())
                continue; // TODO(generators)
            w << "\t" << n << "(" << params_decl(ctx, f->Parameters) << ") " << ret_sig(ctx, f->ReturnType) << "\n";
        }
        else if (RR_SHARED_PTR<EventDefinition> ev = RR_DYNAMIC_POINTER_CAST<EventDefinition>(m))
        {
            w << "\t" << n << "() *rr.Event[" << event_func_type(ctx, ev) << "]\n";
        }
        else if (RR_SHARED_PTR<ObjRefDefinition> o = RR_DYNAMIC_POINTER_CAST<ObjRefDefinition>(m))
        {
            std::string ind;
            std::string ot = go_object_ref(ctx, o->ObjectType, ctx.def);
            if (objref_index_type(o, ind))
                w << "\tGet" << n << "(index " << ind << ") (" << ot << ", error)\n";
            else
                w << "\tGet" << n << "() (" << ot << ", error)\n";
        }
        else if (RR_SHARED_PTR<CallbackDefinition> cb = RR_DYNAMIC_POINTER_CAST<CallbackDefinition>(m))
        {
            w << "\t" << n << "() *rr.Callback[" << callback_func_type(ctx, cb) << "]\n";
        }
        // TODO(pipes, wires, memories)
    }
    w << "}\n\n";

    // async interface (client stubs)
    ctx.uses_time = true;
    w << "// Async" << name << " provides asynchronous access to " << name << " (client object references).\n";
    w << "type Async" << name << " interface {\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        std::string n = mn[m->Name];
        if (RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m))
        {
            std::string t = go_type(ctx, *p->Type);
            if (p->Direction() != MemberDefinition_Direction_writeonly)
                w << "\tAsyncGet" << n << "(rr_timeout time.Duration, rr_handler func(" << t << ", error))\n";
            if (p->Direction() != MemberDefinition_Direction_readonly)
                w << "\tAsyncSet" << n << "(value " << t << ", rr_timeout time.Duration, rr_handler func(error))\n";
        }
        else if (RR_SHARED_PTR<FunctionDefinition> f = RR_DYNAMIC_POINTER_CAST<FunctionDefinition>(m))
        {
            if (f->IsGenerator())
                continue;
            std::string pd = params_decl(ctx, f->Parameters);
            std::string h = is_void(f->ReturnType) ? "func(error)" : "func(" + go_type(ctx, *f->ReturnType) + ", error)";
            w << "\tAsync" << n << "(" << pd << (pd.empty() ? "" : ", ") << "rr_timeout time.Duration, rr_handler " << h
              << ")\n";
        }
        else if (RR_SHARED_PTR<ObjRefDefinition> o = RR_DYNAMIC_POINTER_CAST<ObjRefDefinition>(m))
        {
            std::string ind;
            std::string ot = go_object_ref(ctx, o->ObjectType, ctx.def);
            if (objref_index_type(o, ind))
                w << "\tAsyncGet" << n << "(index " << ind << ", rr_timeout time.Duration, rr_handler func(" << ot
                  << ", error))\n";
            else
                w << "\tAsyncGet" << n << "(rr_timeout time.Duration, rr_handler func(" << ot << ", error))\n";
        }
    }
    w << "}\n\n";
}

std::string pack_args(GenContext& ctx, const std::vector<RR_SHARED_PTR<TypeDefinition> >& params,
                      const std::string& indent = "\t")
{
    std::ostringstream o;
    o << indent << "var rr_b rr.ElementBuilder\n";
    BOOST_FOREACH (const RR_SHARED_PTR<TypeDefinition>& p, params)
    {
        o << indent << "rr.PackField(&rr_b, " << go_string_literal(p->Name) << ", " << fix_local(p->Name) << ", "
          << go_codec(ctx, *p) << ")\n";
    }
    return o.str();
}

std::string unpack_args(GenContext& ctx, const std::vector<RR_SHARED_PTR<TypeDefinition> >& params,
                        const std::string& indent, const std::string& on_error)
{
    std::ostringstream o;
    BOOST_FOREACH (const RR_SHARED_PTR<TypeDefinition>& p, params)
    {
        o << indent << fix_local(p->Name) << ", rr_err := rr.UnpackField(rr_args, " << go_string_literal(p->Name) << ", "
          << go_codec(ctx, *p) << ")\n";
        o << indent << "if rr_err != nil {\n" << indent << "\t" << on_error << "\n" << indent << "}\n";
    }
    return o.str();
}

void GenerateStub(GenContext& ctx, const RR_SHARED_PTR<ServiceEntryDefinition>& e, std::ostream& w)
{
    std::string name = exported(e->Name);
    std::string sn = lower_first(name) + "Stub";
    std::map<std::string, std::string> mn = exported_names(e->Members);

    w << "type " << sn << " struct {\n\t*rr.StubBase\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        if (RR_SHARED_PTR<EventDefinition> ev = RR_DYNAMIC_POINTER_CAST<EventDefinition>(m))
            w << "\trr_" << ev->Name << " rr.Event[" << event_func_type(ctx, ev) << "]\n";
        if (RR_SHARED_PTR<CallbackDefinition> cb = RR_DYNAMIC_POINTER_CAST<CallbackDefinition>(m))
            w << "\trr_" << cb->Name << " rr.Callback[" << callback_func_type(ctx, cb) << "]\n";
    }
    w << "}\n\n";
    w << "var _ " << name << " = (*" << sn << ")(nil)\n";
    w << "var _ Async" << name << " = (*" << sn << ")(nil)\n\n";

    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        std::string n = mn[m->Name];
        std::string mname = go_string_literal(m->Name);
        if (RR_SHARED_PTR<PropertyDefinition> p = RR_DYNAMIC_POINTER_CAST<PropertyDefinition>(m))
        {
            std::string t = go_type(ctx, *p->Type);
            std::string c = go_codec(ctx, *p->Type);
            if (p->Direction() != MemberDefinition_Direction_writeonly)
            {
                w << "func (rr_s *" << sn << ") Get" << n << "() (" << t << ", error) {\n\treturn rr.GetProperty(rr_s.StubBase, "
                  << mname << ", " << c << ")\n}\n\n";
                w << "func (rr_s *" << sn << ") AsyncGet" << n << "(rr_timeout time.Duration, rr_handler func(" << t
                  << ", error)) {\n\trr.AsyncGetProperty(rr_s.StubBase, " << mname << ", " << c
                  << ", rr_timeout, rr_handler)\n}\n\n";
            }
            if (p->Direction() != MemberDefinition_Direction_readonly)
            {
                w << "func (rr_s *" << sn << ") Set" << n << "(value " << t << ") error {\n\treturn rr.SetProperty(rr_s.StubBase, "
                  << mname << ", value, " << c << ")\n}\n\n";
                w << "func (rr_s *" << sn << ") AsyncSet" << n << "(value " << t
                  << ", rr_timeout time.Duration, rr_handler func(error)) {\n\trr.AsyncSetProperty(rr_s.StubBase, " << mname
                  << ", value, " << c << ", rr_timeout, rr_handler)\n}\n\n";
            }
        }
        else if (RR_SHARED_PTR<FunctionDefinition> f = RR_DYNAMIC_POINTER_CAST<FunctionDefinition>(m))
        {
            if (f->IsGenerator())
                continue;
            std::string pd = params_decl(ctx, f->Parameters);
            w << "func (rr_s *" << sn << ") " << n << "(" << pd << ") " << ret_sig(ctx, f->ReturnType) << " {\n";
            w << pack_args(ctx, f->Parameters);
            if (is_void(f->ReturnType))
                w << "\treturn rr.CallFunctionVoid(rr_s.StubBase, " << mname << ", &rr_b)\n}\n\n";
            else
                w << "\treturn rr.CallFunction(rr_s.StubBase, " << mname << ", &rr_b, " << go_codec(ctx, *f->ReturnType)
                  << ")\n}\n\n";
            std::string h = is_void(f->ReturnType) ? "func(error)" : "func(" + go_type(ctx, *f->ReturnType) + ", error)";
            w << "func (rr_s *" << sn << ") Async" << n << "(" << pd << (pd.empty() ? "" : ", ")
              << "rr_timeout time.Duration, rr_handler " << h << ") {\n";
            w << pack_args(ctx, f->Parameters);
            if (is_void(f->ReturnType))
                w << "\trr.AsyncCallFunctionVoid(rr_s.StubBase, " << mname << ", &rr_b, rr_timeout, rr_handler)\n}\n\n";
            else
                w << "\trr.AsyncCallFunction(rr_s.StubBase, " << mname << ", &rr_b, " << go_codec(ctx, *f->ReturnType)
                  << ", rr_timeout, rr_handler)\n}\n\n";
        }
        else if (RR_SHARED_PTR<EventDefinition> ev = RR_DYNAMIC_POINTER_CAST<EventDefinition>(m))
        {
            w << "func (rr_s *" << sn << ") " << n << "() *rr.Event[" << event_func_type(ctx, ev) << "] { return &rr_s.rr_"
              << ev->Name << " }\n\n";
        }
        else if (RR_SHARED_PTR<CallbackDefinition> cb = RR_DYNAMIC_POINTER_CAST<CallbackDefinition>(m))
        {
            w << "func (rr_s *" << sn << ") " << n << "() *rr.Callback[" << callback_func_type(ctx, cb)
              << "] { return &rr_s.rr_" << cb->Name << " }\n\n";
        }
        else if (RR_SHARED_PTR<ObjRefDefinition> o = RR_DYNAMIC_POINTER_CAST<ObjRefDefinition>(m))
        {
            std::string ind;
            std::string ot = go_object_ref(ctx, o->ObjectType, ctx.def);
            std::string qt = go_string_literal(qualified_object_type(o->ObjectType, ctx.def));
            if (objref_index_type(o, ind))
            {
                std::string conv = ind == "int32" ? "strconv.Itoa(int(index))" : "index";
                if (ind == "int32")
                    ctx.uses_strconv = true;
                w << "func (rr_s *" << sn << ") Get" << n << "(index " << ind << ") (" << ot
                  << ", error) {\n\treturn rr.FindObjRefTyped[" << ot << "](rr_s.StubBase, " << mname << ", " << conv << ", "
                  << qt << ")\n}\n\n";
                w << "func (rr_s *" << sn << ") AsyncGet" << n << "(index " << ind << ", rr_timeout time.Duration, rr_handler func("
                  << ot << ", error)) {\n\trr.AsyncFindObjRefTyped(rr_s.StubBase, " << mname << ", " << conv << ", " << qt
                  << ", rr_timeout, rr_handler)\n}\n\n";
            }
            else
            {
                w << "func (rr_s *" << sn << ") Get" << n << "() (" << ot << ", error) {\n\treturn rr.FindObjRefTyped["
                  << ot << "](rr_s.StubBase, " << mname << ", \"\", " << qt << ")\n}\n\n";
                w << "func (rr_s *" << sn << ") AsyncGet" << n << "(rr_timeout time.Duration, rr_handler func(" << ot
                  << ", error)) {\n\trr.AsyncFindObjRefTyped(rr_s.StubBase, " << mname << ", \"\", " << qt
                  << ", rr_timeout, rr_handler)\n}\n\n";
            }
        }
    }

    // events
    w << "func (rr_s *" << sn << ") RRDispatchEvent(rr_name string, rr_args []*rr.Element) {\n\tswitch rr_name {\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<EventDefinition> ev = RR_DYNAMIC_POINTER_CAST<EventDefinition>(m);
        if (!ev)
            continue;
        w << "\tcase " << go_string_literal(ev->Name) << ":\n";
        w << unpack_args(ctx, ev->Parameters, "\t\t", "return");
        w << "\t\tfor _, rr_h := range rr_s.rr_" << ev->Name << ".Handlers() {\n\t\t\trr_h(" << params_names(ev->Parameters)
          << ")\n\t\t}\n";
    }
    w << "\t}\n\t_ = rr_args\n}\n\n";

    // callbacks
    w << "func (rr_s *" << sn << ") RRCallbackCall(rr_name string, rr_args []*rr.Element) (*rr.Element, error) {\n\tswitch rr_name {\n";
    BOOST_FOREACH (const RR_SHARED_PTR<MemberDefinition>& m, e->Members)
    {
        RR_SHARED_PTR<CallbackDefinition> cb = RR_DYNAMIC_POINTER_CAST<CallbackDefinition>(m);
        if (!cb)
            continue;
        w << "\tcase " << go_string_literal(cb->Name) << ":\n";
        w << "\t\trr_f, rr_ok := rr_s.rr_" << cb->Name << ".Function()\n";
        w << "\t\tif !rr_ok {\n\t\t\treturn nil, rr.NewInvalidOperation(\"callback " << cb->Name << " not set\")\n\t\t}\n";
        w << unpack_args(ctx, cb->Parameters, "\t\t", "return nil, rr_err");
        if (is_void(cb->ReturnType))
        {
            w << "\t\treturn nil, rr_f(" << params_names(cb->Parameters) << ")\n";
        }
        else
        {
            w << "\t\trr_ret, rr_err := rr_f(" << params_names(cb->Parameters) << ")\n";
            w << "\t\tif rr_err != nil {\n\t\t\treturn nil, rr_err\n\t\t}\n";
            w << "\t\treturn " << go_codec(ctx, *cb->ReturnType) << ".Pack(\"return\", rr_ret)\n";
        }
    }
    w << "\t}\n\t_ = rr_args\n\treturn nil, rr.NewMemberNotFound(\"callback \" + rr_name + \" not found\")\n}\n\n";
}

void GenerateFactory(GenContext& ctx, const std::string& defstring, std::ostream& w)
{
    w << "const serviceDefinition = " << go_raw_string(defstring) << "\n\n";
    w << "type serviceFactory struct{}\n\n";
    w << "// Factory is the service factory for " << ctx.def->Name << ".\n";
    w << "var Factory rr.ServiceFactory = serviceFactory{}\n\n";
    w << "func (serviceFactory) ServiceName() string { return " << go_string_literal(ctx.def->Name) << " }\n\n";
    w << "func (serviceFactory) DefString() string { return serviceDefinition }\n\n";
    w << "func (serviceFactory) CreateStub(objectType string, b *rr.StubBase) (any, error) {\n\tswitch objectType {\n";
    BOOST_FOREACH (const RR_SHARED_PTR<ServiceEntryDefinition>& e, ctx.def->Objects)
    {
        std::string sn = lower_first(exported(e->Name)) + "Stub";
        w << "\tcase " << go_string_literal(qualified_name(ctx.def, e->Name)) << ":\n";
        w << "\t\ts := &" << sn << "{StubBase: b}\n\t\tif err := b.RRInit(s); err != nil {\n\t\t\treturn nil, err\n\t\t}\n";
        w << "\t\treturn s, nil\n";
    }
    w << "\t}\n\treturn nil, rr.NewServiceError(\"unknown object type \" + objectType)\n}\n\n";
    w << "func (serviceFactory) CreateSkel(objectType string, obj any, b *rr.SkelBase) (rr.ServiceSkel, error) {\n";
    w << "\treturn nil, rr.NewNotImplementedError(\"service skeletons not implemented\")\n}\n\n";
    w << "func (serviceFactory) DownCastError(e *rr.Error) error {\n\tswitch e.Name {\n";
    BOOST_FOREACH (const RR_SHARED_PTR<ExceptionDefinition>& ex, ctx.def->Exceptions)
    {
        w << "\tcase " << go_string_literal(qualified_name(ctx.def, ex->Name)) << ":\n\t\treturn &"
          << exported(ex->Name) << "{E: e}\n";
    }
    w << "\t}\n\treturn nil\n}\n\n";
    w << "func init() {\n\trr.AddServiceFactory(Factory)\n";
    BOOST_FOREACH (const RR_SHARED_PTR<ServiceEntryDefinition>& e, ctx.def->Structures)
    {
        w << "\trr.RegisterStructureType[" << exported(e->Name) << "]()\n";
    }
    BOOST_FOREACH (const RR_SHARED_PTR<ServiceEntryDefinition>& e, ctx.def->Pods)
    {
        w << "\trr.RegisterPodType[" << exported(e->Name) << "]()\n";
    }
    BOOST_FOREACH (const RR_SHARED_PTR<ServiceEntryDefinition>& e, ctx.def->NamedArrays)
    {
        w << "\trr.RegisterNamedArrayType[" << exported(e->Name) << ", " << namedarray_element_type(ctx, e) << "]()\n";
    }
    w << "}\n";
}

} // namespace

std::string GoServiceLangGen::package_name(const std::string& service_name)
{
    return boost::to_lower_copy(boost::replace_all_copy(service_name, ".", "__"));
}

void GoServiceLangGen::GenerateFile(const RR_SHARED_PTR<ServiceDefinition>& d, const std::string& servicedef,
                                    const std::vector<RR_SHARED_PTR<ServiceDefinition> >& alldefs,
                                    const Options& options, std::ostream* w)
{
    GenContext ctx;
    ctx.def = d;
    ctx.alldefs = alldefs;
    ctx.options = options;

    std::ostringstream body;

    if (!d->Constants.empty())
    {
        BOOST_FOREACH (const RR_SHARED_PTR<ConstantDefinition>& c, d->Constants)
        {
            GenerateConstant(ctx, c, d->Constants, "", body);
        }
    }
    BOOST_FOREACH (const RR_SHARED_PTR<EnumDefinition>& e, d->Enums)
    {
        GenerateEnum(ctx, e, body);
    }
    BOOST_FOREACH (const RR_SHARED_PTR<ExceptionDefinition>& e, d->Exceptions)
    {
        GenerateException(ctx, e, body);
    }
    BOOST_FOREACH (const RR_SHARED_PTR<ServiceEntryDefinition>& e, d->Structures)
    {
        GenerateStructure(ctx, e, body);
    }
    BOOST_FOREACH (const RR_SHARED_PTR<ServiceEntryDefinition>& e, d->Pods)
    {
        GeneratePod(ctx, e, body);
    }
    BOOST_FOREACH (const RR_SHARED_PTR<ServiceEntryDefinition>& e, d->NamedArrays)
    {
        GenerateNamedArray(ctx, e, body);
    }
    std::vector<RR_SHARED_PTR<ServiceEntryDefinition> > entries;
    entries.insert(entries.end(), d->Structures.begin(), d->Structures.end());
    entries.insert(entries.end(), d->Pods.begin(), d->Pods.end());
    entries.insert(entries.end(), d->NamedArrays.begin(), d->NamedArrays.end());
    entries.insert(entries.end(), d->Objects.begin(), d->Objects.end());
    BOOST_FOREACH (const RR_SHARED_PTR<ServiceEntryDefinition>& e, entries)
    {
        BOOST_FOREACH (const RR_SHARED_PTR<ConstantDefinition>& c, e->Constants)
        {
            GenerateConstant(ctx, c, e->Constants, exported(e->Name) + "_", body);
        }
    }
    BOOST_FOREACH (const RR_SHARED_PTR<ServiceEntryDefinition>& e, d->Objects)
    {
        GenerateObjectInterface(ctx, e, body);
        GenerateStub(ctx, e, body);
    }
    GenerateFactory(ctx, servicedef, body);

    std::ostream& o = *w;
    o << "// Code generated by RobotRaconteurGenGo from " << d->Name << ".robdef; DO NOT EDIT.\n\n";
    o << doc_comment(d->DocString);
    o << "// Package " << package_name(d->Name) << " implements the Robot Raconteur service " << d->Name << ".\n";
    o << "package " << package_name(d->Name) << "\n\n";
    o << "import (\n";
    if (ctx.uses_strconv)
        o << "\t\"strconv\"\n";
    if (ctx.uses_time)
        o << "\t\"time\"\n";
    o << "\n\trr \"" << options.runtime_import << "\"\n";
    BOOST_FOREACH (const std::string& imp, ctx.imports)
    {
        std::string path = options.import_prefix.empty() ? package_name(imp)
                                                         : options.import_prefix + "/" + package_name(imp);
        o << "\t" << package_name(imp) << " \"" << path << "\"\n";
    }
    o << ")\n\n";
    o << body.str();
}

void GoServiceLangGen::GenerateFiles(const RR_SHARED_PTR<ServiceDefinition>& d, const std::string& servicedef,
                                     const std::vector<RR_SHARED_PTR<ServiceDefinition> >& alldefs,
                                     const Options& options, const std::string& path)
{
    namespace fs = boost::filesystem;
    fs::path dir = fs::path(path) / package_name(d->Name);
    fs::create_directories(dir);
    fs::path file = dir / (package_name(d->Name) + ".go");
    std::ofstream f(file.string().c_str(), std::ios::out | std::ios::binary);
    if (!f)
        throw SystemResourceException("Could not open " + file.string() + " for writing");
    GenerateFile(d, servicedef, alldefs, options, &f);
}

} // namespace RobotRaconteurGen
