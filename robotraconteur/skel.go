package robotraconteur

import (
	"context"
	"fmt"
	"sync"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// ServiceSkel is implemented by generated service skeletons. A skeleton
// adapts a Go object implementing a service object interface to the
// Robot Raconteur service.
type ServiceSkel interface {
	// RRInit is called when the skeleton is attached to the service. It
	// creates pipe/wire servers, connects events and attaches callbacks.
	RRInit(b *SkelBase) error
	// Requests run on their own goroutine with a ctx describing the request
	// (see CurrentEndpoint).
	RRCallGetProperty(ctx context.Context, name string) (*Element, error)
	RRCallSetProperty(ctx context.Context, name string, value *Element) error
	RRCallFunction(ctx context.Context, name string, args []*Element) (*Element, error)
	// RRGetSubObj returns the object for an objref member and its fully
	// qualified object type.
	RRGetSubObj(ctx context.Context, name, index string) (obj any, objectType string, err error)
	RRGetMemory(name string) (*MemoryServer, error)
}

// ServiceObjectTyper may be implemented by service objects to report their
// fully qualified object type (required for varobject objrefs).
type ServiceObjectTyper interface {
	RRObjectType() string
}

// ServiceObjectInitializer may be implemented by service objects to be
// notified when they are attached to a service.
type ServiceObjectInitializer interface {
	RRServiceObjectInit(ctx *ServerContext, servicePath string)
}

// SkelBase is the runtime part of a service skeleton.
type SkelBase struct {
	node       *Node
	obj        any
	objectType string
	impl       ServiceSkel
	inner      swig.WrappedServiceSkel
	director   swig.WrappedServiceSkelDirector

	mu          sync.Mutex
	connections []*EventConnection
	monitor     chan struct{}
}

// RRObject returns the service object.
func (b *SkelBase) RRObject() any { return b.obj }

// RRObjectType returns the fully qualified object type.
func (b *SkelBase) RRObjectType() string { return b.objectType }

// RRAddEventConnection records an event connection to be disconnected when
// the object is released. Used by generated skeletons.
func (b *SkelBase) RRAddEventConnection(c *EventConnection) {
	b.mu.Lock()
	b.connections = append(b.connections, c)
	b.mu.Unlock()
}

// ServicePath returns the service path of the object.
func (b *SkelBase) ServicePath() (string, error) {
	return call(func() string { return b.inner.GetServicePath() })
}

// DispatchEvent sends an event to all clients. Used by generated skeletons.
func (b *SkelBase) DispatchEvent(name string, args *ElementBuilder) error {
	a, err := args.Result()
	if err != nil {
		return err
	}
	var cerr error
	err = callVoid(func() {
		vec, err := elementsToNative(a)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteVectorptr_messageelement(vec)
		b.inner.WrappedDispatchEvent(name, vec)
	})
	if err != nil {
		return err
	}
	return cerr
}

// CallbackCall calls a client callback. Used by generated skeletons.
func (b *SkelBase) CallbackCall(name string, endpoint uint32, args *ElementBuilder) (*Element, error) {
	a, err := args.Result()
	if err != nil {
		return nil, err
	}
	var out *Element
	var cerr error
	err = callVoid(func() {
		vec, err := elementsToNative(a)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteVectorptr_messageelement(vec)
		m := b.inner.WrappedCallbackCall(name, endpoint, vec)
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

// ---- director ----

type skelDirector struct{ b *SkelBase }

func (d *skelDirector) Init(skel swig.WrappedServiceSkel) {
	defer recoverDirector()
	d.b.inner = skel
	addCleanup(d.b, skel, swig.DeleteWrappedServiceSkel)
	if err := d.b.impl.RRInit(d.b); err != nil {
		setDirectorError(err)
		return
	}
	if init, ok := d.b.obj.(ServiceObjectInitializer); ok {
		path, _ := d.b.ServicePath()
		init.RRServiceObjectInit(&ServerContext{node: d.b.node}, path)
	}
}

// serve handles a request. With an async adapter (always provided by the
// core for properties and functions) the request completes from a new
// goroutine, so the core's thread is released at once and the
// implementation may block. Returns the element for inline completion.
func (d *skelDirector) serve(async swig.WrappedServiceSkelAsyncAdapter, f func(ctx context.Context) (*Element, error)) swig.MessageElement {
	a := newAsyncAdapter(async)
	r := captureRequest(d.b.node, a)
	if a == nil {
		defer r.release()
		e, err := runRequest(r.context(), f)
		return d.returnElement(e, err)
	}
	a.makeAsync()
	go func() {
		defer r.release()
		a.end(runRequest(r.context(), f))
	}()
	return nil
}

// runRequest calls a service implementation, converting a panic into an
// UnknownError returned to the client.
func runRequest(ctx context.Context, f func(ctx context.Context) (*Element, error)) (e *Element, err error) {
	defer func() {
		if p := recover(); p != nil {
			e, err = nil, panicError(p)
		}
	}()
	return f(ctx)
}

func (d *skelDirector) X_CallGetProperty(name string, async swig.WrappedServiceSkelAsyncAdapter) swig.MessageElement {
	defer recoverDirector()
	return d.serve(async, func(ctx context.Context) (*Element, error) {
		return d.b.impl.RRCallGetProperty(ctx, name)
	})
}

func (d *skelDirector) X_CallSetProperty(name string, m swig.MessageElement, async swig.WrappedServiceSkelAsyncAdapter) {
	defer recoverDirector()
	if m != nil && m.Swigcptr() != 0 {
		defer swig.DeleteMessageElement(m)
	}
	e, err := fromNative(m)
	if err != nil {
		if async != nil && async.Swigcptr() != 0 {
			swig.DeleteWrappedServiceSkelAsyncAdapter(async)
		}
		setDirectorError(err)
		return
	}
	d.serve(async, func(ctx context.Context) (*Element, error) {
		return nil, d.b.impl.RRCallSetProperty(ctx, name, e)
	})
}

func (d *skelDirector) X_CallFunction(name string, args swig.Vectorptr_messageelement, async swig.WrappedServiceSkelAsyncAdapter) swig.MessageElement {
	defer recoverDirector()
	elems, err := elementsFromNative(args)
	if err != nil {
		if async != nil && async.Swigcptr() != 0 {
			swig.DeleteWrappedServiceSkelAsyncAdapter(async)
		}
		setDirectorError(err)
		return nil
	}
	return d.serve(async, func(ctx context.Context) (*Element, error) {
		return d.b.impl.RRCallFunction(ctx, name, elems)
	})
}

func (d *skelDirector) returnElement(e *Element, err error) swig.MessageElement {
	if err != nil {
		setDirectorError(err)
		return nil
	}
	if e == nil {
		e = NewElement("return", []int32{0})
	}
	m, err := toNative(e)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	return m // transferred to C++
}

// X_GetSubObj runs inline: the core has no asynchronous objref lookup.
func (d *skelDirector) X_GetSubObj(name, index string) swig.WrappedRRObject {
	defer recoverDirector()
	r := captureRequest(d.b.node, nil)
	defer r.release()
	obj, objType, err := d.b.impl.RRGetSubObj(r.context(), name, index)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	if obj == nil {
		setDirectorError(NewObjectNotFound("object " + name + " not found"))
		return nil
	}
	if t, ok := obj.(ServiceObjectTyper); ok {
		objType = t.RRObjectType()
	}
	ro, err := d.b.node.newWrappedRRObject(obj, objType)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	return ro // transferred to C++
}

func (d *skelDirector) memory(name string) *MemoryServer {
	m, err := d.b.impl.RRGetMemory(name)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	if m == nil {
		setDirectorError(NewMemberNotFound("memory " + name + " not found"))
		return &MemoryServer{}
	}
	return m
}

func (d *skelDirector) X_GetArrayMemory(name string) swig.WrappedArrayMemoryDirector {
	defer recoverDirector()
	return d.memory(name).array
}

func (d *skelDirector) X_GetMultiDimArrayMemory(name string) swig.WrappedMultiDimArrayMemoryDirector {
	defer recoverDirector()
	return d.memory(name).multidim
}

func (d *skelDirector) X_GetPodArrayMemory(name string) swig.WrappedPodArrayMemoryDirector {
	defer recoverDirector()
	return d.memory(name).pod
}

func (d *skelDirector) X_GetPodMultiDimArrayMemory(name string) swig.WrappedPodMultiDimArrayMemoryDirector {
	defer recoverDirector()
	return d.memory(name).podmd
}

func (d *skelDirector) X_GetNamedArrayMemory(name string) swig.WrappedNamedArrayMemoryDirector {
	defer recoverDirector()
	return d.memory(name).named
}

func (d *skelDirector) X_GetNamedMultiDimArrayMemory(name string) swig.WrappedNamedMultiDimArrayMemoryDirector {
	defer recoverDirector()
	return d.memory(name).namedmd
}

// MonitorLocker may be implemented by service objects to provide their own
// monitor lock (default: a per-object lock).
type MonitorLocker interface {
	RRMonitorEnter(timeout time.Duration) error
	RRMonitorExit()
}

func (d *skelDirector) MonitorEnter(timeout int32) {
	defer recoverDirector()
	if m, ok := d.b.obj.(MonitorLocker); ok {
		var t time.Duration = TimeoutInfinite
		if timeout >= 0 {
			t = time.Duration(timeout) * time.Millisecond
		}
		if err := m.RRMonitorEnter(t); err != nil {
			setDirectorError(err)
		}
		return
	}
	if timeout < 0 {
		d.b.monitor <- struct{}{}
		return
	}
	select {
	case d.b.monitor <- struct{}{}:
	case <-time.After(time.Duration(timeout) * time.Millisecond):
		setDirectorError(NewRequestTimeout("timeout waiting for monitor lock"))
	}
}

func (d *skelDirector) MonitorExit() {
	defer recoverDirector()
	if m, ok := d.b.obj.(MonitorLocker); ok {
		m.RRMonitorExit()
		return
	}
	select {
	case <-d.b.monitor:
	default:
	}
}

func (d *skelDirector) ReleaseCastObject() {
	defer recoverDirector()
	d.b.mu.Lock()
	conns := d.b.connections
	d.b.connections = nil
	d.b.mu.Unlock()
	for _, c := range conns {
		c.Disconnect()
	}
}

var _ swig.WrappedServiceSkelDirectorMethods = (*skelDirector)(nil)

// newWrappedRRObject creates the native wrapper and skeleton for a service object.
func (n *Node) newWrappedRRObject(obj any, objectType string) (swig.WrappedRRObject, error) {
	serviceName, _ := SplitQualifiedName(objectType)
	f, err := n.ServiceType(serviceName)
	if err != nil {
		return nil, err
	}
	b := &SkelBase{node: n, obj: obj, objectType: objectType, monitor: make(chan struct{}, 1)}
	impl, err := f.CreateSkel(objectType, obj, b)
	if err != nil {
		return nil, err
	}
	b.impl = impl
	sd := &skelDirector{b: b}
	d := swig.NewDirectorWrappedServiceSkelDirector(sd)
	b.director = d
	id := heapAdd(b, func() { swig.DeleteDirectorWrappedServiceSkelDirector(d) })
	ro, err := call(func() swig.WrappedRRObject { return swig.NewWrappedRRObject(objectType, d, id) })
	if err != nil {
		heapRemove(id)
		return nil, err
	}
	return ro, nil
}

// ---- service registration ----

// ServerContext is a registered service.
type ServerContext struct {
	node   *Node
	name   string
	native swig.ServerContext
}

// Name returns the service name.
func (c *ServerContext) Name() string { return c.name }

// RegisterService registers obj as a service. objectType is the fully
// qualified root object type, e.g. "experimental.reynard_the_robot.Reynard".
func (n *Node) RegisterService(name, objectType string, obj any, opts ...ServiceOption) (*ServerContext, error) {
	var o serviceOptions
	for _, f := range opts {
		f(&o)
	}
	if err := n.RegisterServiceTypes(); err != nil {
		return nil, err
	}
	serviceName, _ := SplitQualifiedName(objectType)
	ro, err := n.newWrappedRRObject(obj, objectType)
	if err != nil {
		return nil, err
	}
	defer swig.DeleteWrappedRRObject(ro)
	var cerr error
	ctx, err := call(func() swig.ServerContext {
		rro := ro.SwigGetRRObject()
		defer swig.DeleteRRObject(rro)
		nn := swig.SwigcptrRobotRaconteurNode(n.native.Swigcptr())
		if o.policy != nil {
			p, err := o.policy.toNative()
			if err != nil {
				cerr = err
				return nil
			}
			defer swig.DeleteServiceSecurityPolicy(p)
			return nn.RegisterService__SWIG_0(name, serviceName, rro, p)
		}
		return nn.RegisterService__SWIG_1(name, serviceName, rro)
	})
	if cerr != nil {
		return nil, cerr
	}
	if err != nil {
		return nil, err
	}
	sc := &ServerContext{node: n, name: name, native: ctx}
	addCleanup(sc, ctx, swig.DeleteServerContext)
	n.mu.Lock()
	if n.services == nil {
		n.services = make(map[string]*ServerContext)
	}
	n.services[name] = sc
	n.mu.Unlock()
	return sc, nil
}

// CloseService closes a registered service.
func (n *Node) CloseService(name string) error {
	n.mu.Lock()
	delete(n.services, name)
	n.mu.Unlock()
	return callVoid(func() { n.native.CloseService(name) })
}

// ServiceOption configures RegisterService.
type ServiceOption func(*serviceOptions)

type serviceOptions struct {
	policy *ServiceSecurityPolicy
}

// WithSecurityPolicy sets the security policy of the service.
func WithSecurityPolicy(p *ServiceSecurityPolicy) ServiceOption {
	return func(o *serviceOptions) { o.policy = p }
}

var _ = fmt.Sprintf
