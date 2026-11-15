package robotraconteur

import (
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// StubImpl is implemented by generated client stubs to receive events and
// callback calls from the service.
type StubImpl interface {
	RRDispatchEvent(name string, args []*Element)
	RRCallbackCall(name string, args []*Element) (*Element, error)
}

// StubBase is the runtime part of a client object reference. Generated
// stubs embed *StubBase and use its methods to access the remote object.
type StubBase struct {
	node       *Node
	inner      swig.WrappedServiceStub
	objectType string
	impl       StubImpl
	director   swig.WrappedServiceStubDirector
}

// RRStubBase implements StubHolder.
func (b *StubBase) RRStubBase() *StubBase { return b }

// RRNode returns the node of the client connection.
func (b *StubBase) RRNode() *Node { return b.node }

// RRObjectType returns the fully qualified object type of the reference.
func (b *StubBase) RRObjectType() string { return b.objectType }

type stubDirector struct{ base *StubBase }

func (d *stubDirector) DispatchEvent(name string, args swig.Vectorptr_messageelement) {
	defer recoverDirector()
	impl := d.base.impl
	if impl == nil {
		return
	}
	elems, err := elementsFromNative(args)
	if err != nil {
		return
	}
	impl.RRDispatchEvent(name, elems)
}

func (d *stubDirector) CallbackCall(name string, args swig.Vectorptr_messageelement) swig.MessageElement {
	defer recoverDirector()
	impl := d.base.impl
	if impl == nil {
		setDirectorError(NewMemberNotFound("callback " + name + " not found"))
		return nil
	}
	elems, err := elementsFromNative(args)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	ret, err := impl.RRCallbackCall(name, elems)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	if ret == nil {
		ret = NewElement("return", nil)
	}
	m, err := toNative(ret)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	// ownership of m transfers to C++ (directorout)
	return m
}

// RRInit attaches the generated stub implementation (obj, which must
// implement StubImpl) to the native stub. Called by generated CreateStub.
func (b *StubBase) RRInit(obj StubImpl) error {
	b.impl = obj
	sd := &stubDirector{base: b}
	b.director = swig.NewDirectorWrappedServiceStubDirector(sd)
	d := b.director
	id := heapAdd(obj, func() { swig.DeleteDirectorWrappedServiceStubDirector(d) })
	return callVoid(func() { b.inner.SetRRDirector(b.director, id) })
}

// PropertyGet reads a property.
func (b *StubBase) PropertyGet(name string) (*Element, error) {
	var out *Element
	var cerr error
	err := callVoid(func() {
		m := b.inner.PropertyGet(name)
		if m == nil || m.Swigcptr() == 0 {
			return
		}
		defer swig.DeleteMessageElement(m)
		out, cerr = fromNative(m)
	})
	if err != nil {
		return nil, err
	}
	return out, cerr
}

// PropertySet writes a property.
func (b *StubBase) PropertySet(name string, value *Element) error {
	var cerr error
	err := callVoid(func() {
		m, err := toNative(value)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteMessageElement(m)
		b.inner.PropertySet(name, m)
	})
	if err != nil {
		return err
	}
	return cerr
}

// FunctionCall calls a function. The returned element is nil for void functions.
func (b *StubBase) FunctionCall(name string, args []*Element) (*Element, error) {
	var out *Element
	var cerr error
	err := callVoid(func() {
		vec, err := elementsToNative(args)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteVectorptr_messageelement(vec)
		m := b.inner.FunctionCall(name, vec)
		if m == nil || m.Swigcptr() == 0 {
			return
		}
		defer swig.DeleteMessageElement(m)
		out, cerr = fromNative(m)
	})
	if err != nil {
		return nil, err
	}
	return out, cerr
}

// AsyncPropertyGet reads a property asynchronously.
func (b *StubBase) AsyncPropertyGet(name string, timeout time.Duration, handler func(*Element, error)) {
	d, id := newAsyncRequest(handler)
	if err := callVoid(func() { b.inner.Async_PropertyGet(name, timeoutMs(timeout), d, id) }); err != nil {
		heapRemove(id)
		go handler(nil, err)
	}
}

// AsyncPropertySet writes a property asynchronously.
func (b *StubBase) AsyncPropertySet(name string, value *Element, timeout time.Duration, handler func(error)) {
	d, id := newAsyncRequest(func(_ *Element, err error) { handler(err) })
	var cerr error
	err := callVoid(func() {
		m, err := toNative(value)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteMessageElement(m)
		b.inner.Async_PropertySet(name, m, timeoutMs(timeout), d, id)
	})
	if cerr != nil {
		err = cerr
	}
	if err != nil {
		heapRemove(id)
		go handler(err)
	}
}

// AsyncFunctionCall calls a function asynchronously.
func (b *StubBase) AsyncFunctionCall(name string, args []*Element, timeout time.Duration, handler func(*Element, error)) {
	d, id := newAsyncRequest(handler)
	var cerr error
	err := callVoid(func() {
		vec, err := elementsToNative(args)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteVectorptr_messageelement(vec)
		b.inner.Async_FunctionCall(name, vec, timeoutMs(timeout), d, id)
	})
	if cerr != nil {
		err = cerr
	}
	if err != nil {
		heapRemove(id)
		go handler(nil, err)
	}
}

// FindObjRef returns the object reference for an objref member. index is
// empty for non-indexed objrefs; objectType optionally requests a type.
func (b *StubBase) FindObjRef(name, index, objectType string) (any, error) {
	ws, err := call(func() swig.WrappedServiceStub {
		switch {
		case index == "" && objectType == "":
			return swig.SwigcptrWrappedServiceStub(b.inner.Swigcptr()).FindObjRef__SWIG_0(name)
		case objectType == "":
			return swig.SwigcptrWrappedServiceStub(b.inner.Swigcptr()).FindObjRef__SWIG_1(name, index)
		case index == "":
			return swig.SwigcptrWrappedServiceStub(b.inner.Swigcptr()).FindObjRefTyped__SWIG_0(name, objectType)
		default:
			return swig.SwigcptrWrappedServiceStub(b.inner.Swigcptr()).FindObjRefTyped__SWIG_1(name, index, objectType)
		}
	})
	if err != nil {
		return nil, err
	}
	return b.node.stubFromNative(ws)
}

// AsyncFindObjRef returns an object reference asynchronously.
func (b *StubBase) AsyncFindObjRef(name, index, objectType string, timeout time.Duration, handler func(any, error)) {
	d, id := newAsyncStub(b.node, handler)
	t := timeoutMs(timeout)
	err := callVoid(func() {
		switch {
		case index == "" && objectType == "":
			swig.SwigcptrWrappedServiceStub(b.inner.Swigcptr()).Async_FindObjRef__SWIG_0(name, t, d, id)
		case objectType == "":
			swig.SwigcptrWrappedServiceStub(b.inner.Swigcptr()).Async_FindObjRef__SWIG_1(name, index, t, d, id)
		case index == "":
			swig.SwigcptrWrappedServiceStub(b.inner.Swigcptr()).Async_FindObjRefTyped__SWIG_0(name, objectType, t, d, id)
		default:
			swig.SwigcptrWrappedServiceStub(b.inner.Swigcptr()).Async_FindObjRefTyped__SWIG_1(name, index, objectType, t, d, id)
		}
	})
	if err != nil {
		heapRemove(id)
		go handler(nil, err)
	}
}

// FindObjRefTyped is a typed helper for generated objref accessors.
func FindObjRefTyped[T any](b *StubBase, name, index, objectType string) (T, error) {
	var zero T
	o, err := b.FindObjRef(name, index, objectType)
	if err != nil {
		return zero, err
	}
	t, ok := o.(T)
	if !ok {
		return zero, NewDataTypeMismatch("object reference type mismatch")
	}
	return t, nil
}

// ---- client service listener ----

// ClientServiceListenerEventType is the type of a client connection event.
type ClientServiceListenerEventType int32

// Client connection events.
const (
	ClientClosed                     ClientServiceListenerEventType = 1
	ClientConnectionTimeout          ClientServiceListenerEventType = 2
	TransportConnectionConnected     ClientServiceListenerEventType = 3
	TransportConnectionClosed        ClientServiceListenerEventType = 4
	ServicePathReleased              ClientServiceListenerEventType = 5
)

// ClientServiceListenerEvent is passed to a connection listener.
type ClientServiceListenerEvent struct {
	Type ClientServiceListenerEventType
	// ServicePath is set for ServicePathReleased.
	ServicePath string
}

// WithListener sets a function called on client connection events.
func WithListener(f func(ClientServiceListenerEvent)) ConnectOption {
	return func(o *connectOptions) { o.listener = f }
}

type clientListener struct {
	f func(ClientServiceListenerEvent)
}

func (l *clientListener) Callback(code int32) {
	defer recoverDirector()
	l.f(ClientServiceListenerEvent{Type: ClientServiceListenerEventType(code)})
}

func (l *clientListener) Callback2(code int32, p string) {
	defer recoverDirector()
	l.f(ClientServiceListenerEvent{Type: ClientServiceListenerEventType(code), ServicePath: p})
}

func newClientServiceListener(n *Node, f func(ClientServiceListenerEvent)) swig.ClientServiceListenerDirector {
	impl := &clientListener{f: f}
	d := swig.NewDirectorClientServiceListenerDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorClientServiceListenerDirector(d) })
	d.SetObjectheapid(id)
	return d
}
