// GoTypemaps.i - Go typemaps for Robot Raconteur types (counterpart of
// RobotRaconteurNET/NETTypemaps.i). Conversions to idiomatic Go types
// (time.Time, time.Duration, typed enums) happen in the robotraconteur
// package; at this level values cross as plain integers/strings.

// Fixed width integers keep their width in Go (SWIG Go maps int/unsigned int
// to Go int/uint by default).
%typemap(gotype) int32_t, const int32_t & "int32"
%typemap(gotype) uint32_t, const uint32_t & "uint32"
%typemap(gotype) int16_t, const int16_t & "int16"
%typemap(gotype) uint16_t, const uint16_t & "uint16"
%typemap(gotype) int8_t, const int8_t & "int8"
%typemap(gotype) uint8_t, const uint8_t & "uint8"

// boost::posix_time::ptime <-> int64 microseconds since the Unix epoch (UTC).
// Special values (not_a_date_time, +/- infinity) map to math.MinInt64/MaxInt64.
%{
static inline int64_t rrgo_ptime_to_us(const boost::posix_time::ptime& t)
{
    if (t.is_not_a_date_time() || t.is_neg_infinity()) return INT64_MIN;
    if (t.is_pos_infinity()) return INT64_MAX;
    return (t - boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1))).total_microseconds();
}
static inline boost::posix_time::ptime rrgo_us_to_ptime(int64_t us)
{
    if (us == INT64_MIN) return boost::posix_time::ptime(boost::posix_time::not_a_date_time);
    if (us == INT64_MAX) return boost::posix_time::ptime(boost::posix_time::pos_infin);
    return boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1)) + boost::posix_time::microseconds(us);
}
%}

%typemap(gotype) boost::posix_time::ptime, const boost::posix_time::ptime& "int64"
%typemap(imtype) boost::posix_time::ptime, const boost::posix_time::ptime& "int64"
%typemap(in) boost::posix_time::ptime %{ $1 = rrgo_us_to_ptime($input); %}
%typemap(in) const boost::posix_time::ptime& (boost::posix_time::ptime temp)
%{ temp = rrgo_us_to_ptime($input); $1 = &temp; %}
%typemap(out) boost::posix_time::ptime %{ $result = rrgo_ptime_to_us($1); %}
%typemap(out) const boost::posix_time::ptime& %{ $result = rrgo_ptime_to_us(*$1); %}
%typemap(directorin) boost::posix_time::ptime, const boost::posix_time::ptime& %{ $input = rrgo_ptime_to_us($1); %}
%typemap(directorout) boost::posix_time::ptime %{ $result = rrgo_us_to_ptime($input); %}

// boost::posix_time::time_duration <-> int32 milliseconds (as the C# binding;
// RR timeouts are milliseconds, RR_TIMEOUT_INFINITE = -1).
%typemap(gotype) boost::posix_time::time_duration, const boost::posix_time::time_duration& "int32"
%typemap(imtype) boost::posix_time::time_duration, const boost::posix_time::time_duration& "int32"
%typemap(in) boost::posix_time::time_duration %{ $1 = boost::posix_time::milliseconds($input); %}
%typemap(in) const boost::posix_time::time_duration& (boost::posix_time::time_duration temp)
%{ temp = boost::posix_time::milliseconds($input); $1 = &temp; %}
%typemap(out) boost::posix_time::time_duration %{ $result = (int32_t)$1.total_milliseconds(); %}
%typemap(out) const boost::posix_time::time_duration& %{ $result = (int32_t)$1->total_milliseconds(); %}
%typemap(directorin) boost::posix_time::time_duration, const boost::posix_time::time_duration&
%{ $input = (int32_t)$1.total_milliseconds(); %}
%typemap(directorout) boost::posix_time::time_duration %{ $result = boost::posix_time::milliseconds($input); %}

// weak_ptr parameters/returns behave like shared_ptr at the Go boundary
%define %rr_weak_ptr( TYPE )
%typemap(gotype) boost::weak_ptr< TYPE >, const boost::weak_ptr< TYPE >& = SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< TYPE >;
%typemap(in) boost::weak_ptr< TYPE > %{ { boost::shared_ptr< TYPE >* s = *(boost::shared_ptr< TYPE >**)&$input; if (s) $1 = *s; } %}
%typemap(in) const boost::weak_ptr< TYPE >& (boost::weak_ptr< TYPE > temp)
%{ { boost::shared_ptr< TYPE >* s = *(boost::shared_ptr< TYPE >**)&$input; if (s) temp = *s; $1 = &temp; } %}
%typemap(out) boost::weak_ptr< TYPE >
%{ { boost::shared_ptr< TYPE > s = $1.lock(); *(boost::shared_ptr< TYPE >**)&$result = s ? new boost::shared_ptr< TYPE >(s) : 0; } %}
%enddef

// RobotRaconteur::MessageStringPtr <-> Go string
namespace RobotRaconteur
{
	%naturalvar MessageStringPtr;
	class MessageStringPtr;

	%typemap(gotype) MessageStringPtr, const MessageStringPtr& "string"
	%typemap(imtype) MessageStringPtr, const MessageStringPtr& "string"
	%typemap(in) MessageStringPtr %{ $1 = RobotRaconteur::MessageStringPtr(std::string($input.p, $input.n)); %}
	%typemap(in) const MessageStringPtr& (RobotRaconteur::MessageStringPtr temp)
	%{ temp = RobotRaconteur::MessageStringPtr(std::string($input.p, $input.n)); $1 = &temp; %}
	%typemap(out, fragment="AllocateString") MessageStringPtr
	%{ { std::string temp_ret1 = $1.str().to_string(); $result = Swig_AllocateString(temp_ret1.data(), temp_ret1.length()); } %}
	%typemap(out, fragment="AllocateString") const MessageStringPtr&
	%{ { std::string temp_ret1 = $1->str().to_string(); $result = Swig_AllocateString(temp_ret1.data(), temp_ret1.length()); } %}
	%typemap(goout, fragment="CopyString") MessageStringPtr, const MessageStringPtr& %{ $result = swigCopyString($1) %}
	%typemap(directorin, fragment="AllocateString") MessageStringPtr, const MessageStringPtr&
	%{ { std::string temp_ret1 = $1.str().to_string(); $input = Swig_AllocateString(temp_ret1.data(), temp_ret1.length()); } %}
	%typemap(godirectorin, fragment="CopyString") MessageStringPtr, const MessageStringPtr& %{ $result = swigCopyString($input) %}
}

// boost::string_ref (used by IsStringName and friends) <-> Go string
namespace boost
{
	class string_ref;
	%typemap(gotype) string_ref, const string_ref& "string"
	%typemap(imtype) string_ref, const string_ref& "string"
	%typemap(in) string_ref (std::string temp) %{ temp.assign($input.p, $input.n); $1 = boost::string_ref(temp); %}
	%typemap(in) const string_ref& (std::string temp, boost::string_ref temp2)
	%{ temp.assign($input.p, $input.n); temp2 = boost::string_ref(temp); $1 = &temp2; %}
}

%{
#include <boost/filesystem/path.hpp>
%}

namespace boost
{
namespace filesystem
{
%rename(FilesystemPath) path;
class path
{
public:
path(const std::string& s);
std::string string();
};
}
}
