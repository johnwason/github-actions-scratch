package robotraconteur

import (
	"context"

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

// awaitRequest starts a request that completes with a message element.
func (b *StubBase) awaitRequest(ctx context.Context, start func(timeout int32, d swig.AsyncRequestDirector, id int32) error) (*Element, error) {
	return await(ctx, func(done func(*Element, error)) error {
		d, id := newAsyncRequest(done)
		if err := start(b.node.requestTimeoutMs(ctx), d, id); err != nil {
			heapRemove(id)
			return err
		}
		return nil
	}, nil)
}

// RRPropertyGet reads a property.
func (b *StubBase) RRPropertyGet(ctx context.Context, name string) (*Element, error) {
	return b.awaitRequest(ctx, func(t int32, d swig.AsyncRequestDirector, id int32) error {
		return startNative(func() error { b.inner.Async_PropertyGet(name, t, d, id); return nil })
	})
}

// RRPropertySet writes a property.
func (b *StubBase) RRPropertySet(ctx context.Context, name string, value *Element) error {
	_, err := b.awaitRequest(ctx, func(t int32, d swig.AsyncRequestDirector, id int32) error {
		return startNative(func() error {
			m, err := toNative(value)
			if err != nil {
				return err
			}
			defer swig.DeleteMessageElement(m)
			b.inner.Async_PropertySet(name, m, t, d, id)
			return nil
		})
	})
	return err
}

// RRFunctionCall calls a function. The returned element is nil for void functions.
func (b *StubBase) RRFunctionCall(ctx context.Context, name string, args []*Element) (*Element, error) {
	return b.awaitRequest(ctx, func(t int32, d swig.AsyncRequestDirector, id int32) error {
		return startNative(func() error {
			vec, err := elementsToNative(args)
			if err != nil {
				return err
			}
			defer swig.DeleteVectorptr_messageelement(vec)
			b.inner.Async_FunctionCall(name, vec, t, d, id)
			return nil
		})
	})
}

// RRFindObjRef returns the object reference for an objref member. index is
// empty for non-indexed objrefs; objectType optionally requests a type.
func (b *StubBase) RRFindObjRef(ctx context.Context, name, index, objectType string) (any, error) {
	return await(ctx, func(done func(any, error)) error {
		d, id := newAsyncStub(b.node, done)
		t := b.node.requestTimeoutMs(ctx)
		s := swig.SwigcptrWrappedServiceStub(b.inner.Swigcptr())
		err := startNative(func() error {
			switch {
			case index == "" && objectType == "":
				s.Async_FindObjRef__SWIG_0(name, t, d, id)
			case objectType == "":
				s.Async_FindObjRef__SWIG_1(name, index, t, d, id)
			case index == "":
				s.Async_FindObjRefTyped__SWIG_0(name, objectType, t, d, id)
			default:
				s.Async_FindObjRefTyped__SWIG_1(name, index, objectType, t, d, id)
			}
			return nil
		})
		if err != nil {
			heapRemove(id)
		}
		return err
	}, nil)
}

// FindObjRefTyped is a typed helper for generated objref accessors.
func FindObjRefTyped[T any](ctx context.Context, b *StubBase, name, index, objectType string) (T, error) {
	var zero T
	o, err := b.RRFindObjRef(ctx, name, index, objectType)
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
	ClientClosed                 ClientServiceListenerEventType = 1
	ClientConnectionTimeout      ClientServiceListenerEventType = 2
	TransportConnectionConnected ClientServiceListenerEventType = 3
	TransportConnectionClosed    ClientServiceListenerEventType = 4
	ServicePathReleased          ClientServiceListenerEventType = 5
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
