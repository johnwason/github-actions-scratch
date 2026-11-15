package robotraconteur

import (
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// Async operations take a handler func that is called exactly once. Handlers
// are invoked on a new goroutine, never on a Robot Raconteur thread pool
// thread, so they may block or make synchronous calls.

// TimeoutInfinite disables a timeout.
const TimeoutInfinite time.Duration = -1

// TimeoutDefault uses the node's default timeout.
const TimeoutDefault time.Duration = -2

func timeoutMs(t time.Duration) int32 {
	switch {
	case t == TimeoutDefault:
		return -2
	case t < 0:
		return -1
	}
	ms := t.Milliseconds()
	if ms > 1<<31-1 {
		return 1<<31 - 1
	}
	return int32(ms)
}

// ---- AsyncRequestDirector: returns a message element ----

type asyncRequest struct {
	handler func(*Element, error)
}

func (d *asyncRequest) Handler(ret swig.MessageElement, info swig.HandlerErrorInfo) {
	// Handles passed to directors are owned by Go.
	if ret != nil && ret.Swigcptr() != 0 {
		defer swig.DeleteMessageElement(ret)
	}
	err := errorFromInfo(info)
	var e *Element
	if err == nil && ret != nil && ret.Swigcptr() != 0 {
		e, err = fromNative(ret)
	}
	go d.handler(e, err)
}

// newAsyncRequest creates a director and heap id for an async request.
func newAsyncRequest(handler func(*Element, error)) (swig.AsyncRequestDirector, int32) {
	impl := &asyncRequest{handler: handler}
	d := swig.NewDirectorAsyncRequestDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorAsyncRequestDirector(d) })
	return d, id
}

// ---- AsyncVoidReturnDirector ----

type asyncVoid struct{ handler func(error) }

func (d *asyncVoid) Handler(info swig.HandlerErrorInfo) {
	err := errorFromInfo(info)
	go d.handler(err)
}

func newAsyncVoid(handler func(error)) (swig.AsyncVoidReturnDirector, int32) {
	impl := &asyncVoid{handler: handler}
	d := swig.NewDirectorAsyncVoidReturnDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorAsyncVoidReturnDirector(d) })
	return d, id
}

// ---- AsyncVoidNoErrReturnDirector ----

type asyncVoidNoErr struct{ handler func() }

func (d *asyncVoidNoErr) Handler() { go d.handler() }

func newAsyncVoidNoErr(handler func()) (swig.AsyncVoidNoErrReturnDirector, int32) {
	impl := &asyncVoidNoErr{handler: handler}
	d := swig.NewDirectorAsyncVoidNoErrReturnDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorAsyncVoidNoErrReturnDirector(d) })
	return d, id
}

// ---- AsyncStringReturnDirector ----

type asyncString struct{ handler func(string, error) }

func (d *asyncString) Handler(ret string, info swig.HandlerErrorInfo) {
	err := errorFromInfo(info)
	go d.handler(ret, err)
}

func newAsyncString(handler func(string, error)) (swig.AsyncStringReturnDirector, int32) {
	impl := &asyncString{handler: handler}
	d := swig.NewDirectorAsyncStringReturnDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorAsyncStringReturnDirector(d) })
	return d, id
}

// ---- AsyncStubReturnDirector ----

type asyncStub struct {
	node    *Node
	handler func(any, error)
}

func (d *asyncStub) Handler(stub swig.WrappedServiceStub, info swig.HandlerErrorInfo) {
	err := errorFromInfo(info)
	var obj any
	if err == nil {
		obj, err = d.node.stubFromNative(stub)
	} else if stub != nil && stub.Swigcptr() != 0 {
		swig.DeleteWrappedServiceStub(stub)
	}
	go d.handler(obj, err)
}

func newAsyncStub(node *Node, handler func(any, error)) (swig.AsyncStubReturnDirector, int32) {
	impl := &asyncStub{node: node, handler: handler}
	d := swig.NewDirectorAsyncStubReturnDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorAsyncStubReturnDirector(d) })
	return d, id
}
