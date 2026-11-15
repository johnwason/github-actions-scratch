// GoExceptionTypemaps.i
//
// C++ exceptions never cross into Go and never cause a Go panic. Every
// wrapped call catches all exceptions, records them in a thread-local pending
// error (rrgo_support.h) and returns a zero value. The Go runtime package
// collects the pending error right after the call on the same OS thread and
// returns it as an idiomatic Go error.

%{
#include "rrgo_support.h"
%}

%exception %{
try {
  $action
}
RRGO_CATCH_ALL
%}

// Accessors for the pending error slot. These must not themselves use the
// %exception wrapper (they cannot fail).
%exception rrgo_take_error;
%exception rrgo_clear_error;
%exception rrgo_error_pending;

%newobject rrgo_take_error;
%inline %{
RobotRaconteur::HandlerErrorInfo* rrgo_take_error() { return rrgo::TakeError(); }
void rrgo_clear_error() { rrgo::ClearError(); }
bool rrgo_error_pending() { return rrgo::ErrorPending(); }
%}

//RRDirectorExceptionHelper: used by Go director implementations to report
//errors back to C++ (rethrown by the DIRECTOR_CALL macros).
%rr_intrusive_ptr(RobotRaconteur::MessageEntry)
namespace RobotRaconteur {
class MessageEntry;
class RRDirectorExceptionHelper
{
public:
	static void Reset();
	static void SetError(const boost::intrusive_ptr<RobotRaconteur::MessageEntry>& err, const std::string& exception_str);
	static bool IsErrorPending();
	static boost::intrusive_ptr<MessageEntry> GetError();
};
}

// Report a Go error from a director callback using HandlerErrorInfo (avoids
// building a MessageEntry on the Go side).
%exception rrgo_director_set_error;
%inline %{
void rrgo_director_set_error(const RobotRaconteur::HandlerErrorInfo& err)
{
    boost::intrusive_ptr<RobotRaconteur::MessageEntry> m = RobotRaconteur::CreateMessageEntry();
    err.ToMessageEntry(m);
    RobotRaconteur::RRDirectorExceptionHelper::SetError(m, err.errormessage);
}
%}
