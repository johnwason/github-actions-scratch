package robotraconteur

import (
	"errors"
	"fmt"
	"testing"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

func TestErrorCodesMatchNative(t *testing.T) {
	native := map[ErrorCode]swig.RobotRaconteurMessageErrorType{
		CodeNone: swig.MessageErrorType_None, CodeConnectionError: swig.MessageErrorType_ConnectionError,
		CodeUnknownError: swig.MessageErrorType_UnknownError, CodeInvalidState: swig.MessageErrorType_InvalidState,
		CodeRemoteError: swig.MessageErrorType_RemoteError, CodeOperationCancelled: swig.MessageErrorType_OperationCancelled,
		CodeAuthenticationError: swig.MessageErrorType_AuthenticationError, CodePermissionDenied: swig.MessageErrorType_PermissionDenied,
	}
	for c, n := range native {
		if uint32(c) != uint32(n) {
			t.Errorf("%v = %d, native %d", c, c, n)
		}
	}
}

func TestNativeErrorRoundTrip(t *testing.T) {
	for code, name := range builtinErrorNames {
		err := callVoid(func() { swig.ThrowError(uint(code), name, "msg "+name) })
		var rerr *Error
		if !errors.As(err, &rerr) {
			t.Fatalf("%s: got %v", name, err)
		}
		if rerr.Code != code || rerr.Name != name || rerr.Message != "msg "+name {
			t.Errorf("%s: got %+v", name, rerr)
		}
		if !errors.Is(err, &Error{Code: code}) {
			t.Errorf("%s: errors.Is failed", name)
		}
	}
	err := callVoid(func() { swig.ThrowError(uint(CodeRemoteError), "my.service.MyError", "remote") })
	if !errors.Is(err, ErrRemoteError) || !errors.Is(err, NewRemoteError("my.service.MyError", "")) ||
		errors.Is(err, NewRemoteError("my.service.Other", "")) {
		t.Errorf("remote error matching: %v", err)
	}
	if !errors.Is(callVoid(func() { swig.ThrowStd("x") }), ErrUnknownError) {
		t.Error("std::exception should be UnknownError")
	}
	if v, err := call(func() int { return swig.ReturnOrThrow(4) }); err != nil || v != 8 {
		t.Errorf("ReturnOrThrow = %v, %v", v, err)
	}
	if _, err := call(func() int { return swig.ReturnOrThrow(-4) }); !errors.Is(err, ErrInvalidArgument) {
		t.Errorf("expected InvalidArgument, got %v", err)
	}
}

type errDirector struct{ fn func() }

func (d *errDirector) Handler(swig.HandlerErrorInfo) {
	defer recoverDirector()
	d.fn()
}

func TestDirectorErrors(t *testing.T) {
	cases := []struct {
		fn   func()
		want string
	}{
		{func() {}, ""},
		{func() { setDirectorError(NewInvalidOperation("nope")) }, "RobotRaconteur.InvalidOperation: nope"},
		{func() { setDirectorError(fmt.Errorf("wrapped: %w", NewKeyNotFound("k"))) }, "RobotRaconteur.KeyNotFound: k"},
		{func() { setDirectorError(errors.New("plain")) }, "RobotRaconteur.UnknownError: plain"},
		{func() { panic("boom") }, "RobotRaconteur.UnknownError: panic in Go callback: boom"},
	}
	for _, c := range cases {
		d := swig.NewDirectorAsyncVoidReturnDirector(&errDirector{c.fn})
		got, err := call(func() string { return swig.CallVoidDirector(d) })
		swig.DeleteDirectorAsyncVoidReturnDirector(d)
		if err != nil || got != c.want {
			t.Errorf("got %q, %v want %q", got, err, c.want)
		}
	}
}
