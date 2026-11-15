// RRGoTestSupport.i - small native helpers used by the Go binding's own tests
// to exercise the error paths (C++ throw -> Go error, Go director error ->
// C++ exception). Not part of the public API.
%inline %{
namespace rrgo_test {
// Throws a RobotRaconteur exception of the given error code.
void ThrowError(uint32_t code, const std::string& name, const std::string& message)
{
    boost::intrusive_ptr<RobotRaconteur::MessageEntry> m = RobotRaconteur::CreateMessageEntry();
    RobotRaconteur::HandlerErrorInfo(code, name, message).ToMessageEntry(m);
    RobotRaconteur::RobotRaconteurExceptionUtil::ThrowMessageEntryException(m);
}
void ThrowStd(const std::string& message) { throw std::runtime_error(message); }
int32_t ReturnOrThrow(int32_t v) { if (v < 0) throw RobotRaconteur::InvalidArgumentException("negative"); return v * 2; }
// Calls the director through DIRECTOR_CALL2 like the wrapped RR code does and
// returns "" on success or "<errorname>: <message>" of the rethrown exception.
std::string CallVoidDirector(RobotRaconteur::AsyncVoidReturnDirector* d)
{
    try
    {
        RobotRaconteur::HandlerErrorInfo err;
        DIRECTOR_CALL2(d->handler(err));
        return "";
    }
    catch (RobotRaconteur::RobotRaconteurException& e)
    {
        return e.Error + ": " + e.Message;
    }
}
}
%}
