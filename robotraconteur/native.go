package robotraconteur

import (
	"runtime"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// Every call into the native layer that can fail goes through call/callVoid.
//
// The SWIG %exception wrapper (swig/GoExceptionTypemaps.i) catches C++
// exceptions and stores them in a thread-local pending error slot. The
// goroutine is locked to its OS thread for the duration so the error is read
// back on the same thread that made the cgo call.

func takeError() error {
	h := swig.Rrgo_take_error()
	if h == nil || h.Swigcptr() == 0 {
		return nil
	}
	defer swig.DeleteHandlerErrorInfo(h)
	if err := errorFromInfo(h); err != nil {
		return err
	}
	return &Error{Code: CodeUnknownError, Name: "RobotRaconteur.UnknownError", Message: "unknown native error"}
}

func call[T any](f func() T) (T, error) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	swig.Rrgo_clear_error()
	v := f()
	if err := takeError(); err != nil {
		var zero T
		return zero, err
	}
	return v, nil
}

func callVoid(f func()) error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	swig.Rrgo_clear_error()
	f()
	return takeError()
}
