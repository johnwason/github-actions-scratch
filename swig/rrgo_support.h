// rrgo_support.h - native support code for the Robot Raconteur Go bindings.
//
// Error reporting (C++ -> Go):
//   %exception (GoExceptionTypemaps.i) catches every C++ exception thrown by a
//   wrapped call, stores it in a thread-local pending error slot and returns a
//   zero value. The Go runtime calls the wrapped function and then
//   rrgo::TakeError() on the same OS thread (runtime.LockOSThread), turning a
//   pending error into a Go error value. Nothing panics.
#pragma once

#include <cstring>
#include <exception>
#include <string>
#include <boost/type_traits/is_pod.hpp>
#include <boost/utility/enable_if.hpp>

namespace rrgo
{

struct PendingError
{
    bool pending;
    RobotRaconteur::HandlerErrorInfo info;
    PendingError() : pending(false) {}
};

inline PendingError& pending_error()
{
    static thread_local PendingError e;
    return e;
}

inline void SetError(const RobotRaconteur::HandlerErrorInfo& info)
{
    PendingError& e = pending_error();
    e.info = info;
    e.pending = true;
}

inline void SetError(uint32_t code, const std::string& name, const std::string& message)
{
    SetError(RobotRaconteur::HandlerErrorInfo(code, name, message));
}

inline void ClearError()
{
    PendingError& e = pending_error();
    if (e.pending)
    {
        e.pending = false;
        e.info = RobotRaconteur::HandlerErrorInfo();
    }
}

inline bool ErrorPending() { return pending_error().pending; }

// Returns a newly allocated copy of the pending error (owned by the caller)
// and clears it, or NULL when no error is pending.
inline RobotRaconteur::HandlerErrorInfo* TakeError()
{
    PendingError& e = pending_error();
    if (!e.pending)
        return NULL;
    RobotRaconteur::HandlerErrorInfo* ret = new RobotRaconteur::HandlerErrorInfo(e.info);
    e.pending = false;
    e.info = RobotRaconteur::HandlerErrorInfo();
    return ret;
}

// Early return from SWIG Go wrappers on error. Every C wrapper either returns
// void or a plain C type (_swig_go_result). rrgo_no_result is a namespace
// scope stand-in for _swig_go_result in void wrappers; overload resolution
// selects the void-returning version there.
struct no_result
{};

inline void zero(no_result&) {}

template <typename T>
inline T zero(T&)
{
    T v;
    std::memset(static_cast<void*>(&v), 0, sizeof(v));
    return v;
}

} // namespace rrgo

static rrgo::no_result _swig_go_result;

#define RRGO_RETURN_ERROR return rrgo::zero(_swig_go_result)

#define RRGO_CATCH_ALL                                                                                                 \
    catch (RobotRaconteur::RobotRaconteurException & e)                                                                \
    {                                                                                                                  \
        rrgo::SetError(RobotRaconteur::HandlerErrorInfo(e));                                                           \
        RRGO_RETURN_ERROR;                                                                                             \
    }                                                                                                                  \
    catch (std::exception & e)                                                                                         \
    {                                                                                                                  \
        rrgo::SetError(RobotRaconteur::MessageErrorType_UnknownError, "std::exception", e.what());                    \
        RRGO_RETURN_ERROR;                                                                                             \
    }                                                                                                                  \
    catch (...)                                                                                                        \
    {                                                                                                                  \
        rrgo::SetError(RobotRaconteur::MessageErrorType_UnknownError, "unknown", "Unknown C++ exception");            \
        RRGO_RETURN_ERROR;                                                                                             \
    }
