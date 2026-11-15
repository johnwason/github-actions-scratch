package robotraconteur

import (
	"fmt"
	"sync"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// Node is a Robot Raconteur node. Most programs use the process default node
// (DefaultNode), normally initialized with ClientNodeSetup or ServerNodeSetup.
type Node struct {
	native swig.RobotRaconteurNode

	mu         sync.Mutex
	registered map[string]ServiceFactory
}

var defaultNode struct {
	once sync.Once
	node *Node
}

// DefaultNode returns the process default node (RobotRaconteurNode::s()).
func DefaultNode() *Node {
	defaultNode.once.Do(func() {
		n, err := call(func() swig.RobotRaconteurNode { return swig.RobotRaconteurNodeX_get_s() })
		if err != nil || n == nil || n.Swigcptr() == 0 {
			panic(fmt.Sprintf("robotraconteur: could not get default node: %v", err))
		}
		defaultNode.node = &Node{native: n, registered: map[string]ServiceFactory{}}
	})
	return defaultNode.node
}

// RegisterServiceType registers a generated service factory with the node.
// Registering the same service twice is a no-op.
func (n *Node) RegisterServiceType(f ServiceFactory) error {
	n.mu.Lock()
	defer n.mu.Unlock()
	if _, ok := n.registered[f.ServiceName()]; ok {
		return nil
	}
	err := callVoid(func() {
		def := swig.NewServiceDefinition()
		defer swig.DeleteServiceDefinition(def)
		swig.SwigcptrServiceDefinition(def.Swigcptr()).FromString__SWIG_1(f.DefString())
		if swig.Rrgo_error_pending() {
			return
		}
		if !n.native.IsServiceTypeRegistered(def.GetName()) {
			n.native.RegisterServiceType(def)
		}
	})
	if err != nil {
		return err
	}
	n.registered[f.ServiceName()] = f
	return nil
}

// RegisterServiceTypes registers factories with the node, importing
// services in dependency order. With no arguments, all factories added with
// AddServiceFactory are registered.
func (n *Node) RegisterServiceTypes(factories ...ServiceFactory) error {
	if len(factories) == 0 {
		factories = ServiceFactories()
	}
	pending := append([]ServiceFactory(nil), factories...)
	for len(pending) > 0 {
		var next []ServiceFactory
		var lastErr error
		for _, f := range pending {
			if err := n.RegisterServiceType(f); err != nil {
				// probably an import not yet registered; retry after the others
				next = append(next, f)
				lastErr = err
			}
		}
		if len(next) == len(pending) {
			return lastErr
		}
		pending = next
	}
	return nil
}

// ServiceType returns the registered factory for a service definition name.
func (n *Node) ServiceType(name string) (ServiceFactory, error) {
	n.mu.Lock()
	f, ok := n.registered[name]
	n.mu.Unlock()
	if ok {
		return f, nil
	}
	if f := lookupFactory(name); f != nil {
		if err := n.RegisterServiceType(f); err != nil {
			return nil, err
		}
		return f, nil
	}
	return nil, NewServiceNotFound(fmt.Sprintf("service type %s not registered", name))
}

// ConnectOption configures ConnectService.
type ConnectOption func(*connectOptions)

type connectOptions struct {
	username    string
	credentials map[string]any
	objectType  string
	listener    func(ClientServiceListenerEvent)
	timeout     time.Duration
}

// WithCredentials sets the username and credentials used for authentication.
func WithCredentials(username string, credentials map[string]any) ConnectOption {
	return func(o *connectOptions) { o.username, o.credentials = username, credentials }
}

// WithObjectType sets the requested root object type (recommended).
func WithObjectType(objectType string) ConnectOption {
	return func(o *connectOptions) { o.objectType = objectType }
}

// WithConnectTimeout sets the timeout for AsyncConnectService.
func WithConnectTimeout(t time.Duration) ConnectOption {
	return func(o *connectOptions) { o.timeout = t }
}

func (n *Node) packCredentials(cred map[string]any) (swig.MessageElementData, error) {
	if cred == nil {
		return nil, nil
	}
	e, err := PackMapString("credentials", cred, PackVarValue)
	if err != nil {
		return nil, err
	}
	m, err := toNative(e)
	if err != nil {
		return nil, err
	}
	defer swig.DeleteMessageElement(m)
	return m.GetData(), nil
}

// ConnectService connects to a service and returns the root object
// reference (a generated stub, e.g. experimental_reynard_the_robot.Reynard).
func (n *Node) ConnectService(url string, opts ...ConnectOption) (any, error) {
	return n.connectService([]string{url}, opts...)
}

// ConnectServiceURLs connects using a list of candidate URLs for the same service.
func (n *Node) ConnectServiceURLs(urls []string, opts ...ConnectOption) (any, error) {
	return n.connectService(urls, opts...)
}

func (n *Node) connectService(urls []string, opts ...ConnectOption) (any, error) {
	var o connectOptions
	for _, f := range opts {
		f(&o)
	}
	if err := n.RegisterServiceTypes(); err != nil {
		return nil, err
	}
	var cerr error
	stub, err := call(func() swig.WrappedServiceStub {
		cred, err := n.packCredentials(o.credentials)
		if err != nil {
			cerr = err
			return nil
		}
		if cred != nil {
			defer swig.DeleteMessageElementData(cred)
		}
		urlv := swig.NewVectorstring()
		defer swig.DeleteVectorstring(urlv)
		for _, u := range urls {
			urlv.Add(u)
		}
		var listener swig.ClientServiceListenerDirector
		if o.listener != nil {
			listener = newClientServiceListener(n, o.listener)
		}
		return swig.SwigcptrRobotRaconteurNode(n.native.Swigcptr()).ConnectService__SWIG_5(urlv, o.username, cred, listener, o.objectType)
	})
	if cerr != nil {
		return nil, cerr
	}
	if err != nil {
		return nil, err
	}
	return n.stubFromNative(stub)
}

// AsyncConnectService connects asynchronously; handler receives the root object.
func (n *Node) AsyncConnectService(url string, handler func(any, error), opts ...ConnectOption) {
	o := connectOptions{timeout: TimeoutInfinite}
	for _, f := range opts {
		f(&o)
	}
	if err := n.RegisterServiceTypes(); err != nil {
		go handler(nil, err)
		return
	}
	d, id := newAsyncStub(n, handler)
	var cerr error
	err := callVoid(func() {
		cred, err := n.packCredentials(o.credentials)
		if err != nil {
			cerr = err
			return
		}
		if cred != nil {
			defer swig.DeleteMessageElementData(cred)
		}
		var listener swig.ClientServiceListenerDirector
		if o.listener != nil {
			listener = newClientServiceListener(n, o.listener)
		}
		swig.SwigcptrRobotRaconteurNode(n.native.Swigcptr()).AsyncConnectService__SWIG_0(url, o.username, cred, listener, o.objectType, timeoutMs(o.timeout), d, id)
	})
	if cerr != nil {
		err = cerr
	}
	if err != nil {
		heapRemove(id)
		go handler(nil, err)
	}
}

// stubFromNative converts a native stub into its Go client object, reusing
// an existing Go stub if one was already created for it. Takes ownership of ws.
func (n *Node) stubFromNative(ws swig.WrappedServiceStub) (any, error) {
	if ws == nil || ws.Swigcptr() == 0 {
		return nil, NewNullValue("null object reference")
	}
	if id := ws.GetObjectHeapID(); id != 0 {
		if obj, ok := heapGet(int32(id)); ok {
			swig.DeleteWrappedServiceStub(ws)
			return obj, nil
		}
	}
	var objType, serviceName string
	err := callVoid(func() {
		t := ws.GetRR_objecttype()
		if t == nil || t.Swigcptr() == 0 {
			return
		}
		defer swig.DeleteServiceEntryDefinition(t)
		def := t.GetServiceDefinition()
		defer swig.DeleteServiceDefinition(def)
		serviceName = def.GetName()
		objType = serviceName + "." + t.GetName()
	})
	if err != nil {
		swig.DeleteWrappedServiceStub(ws)
		return nil, err
	}
	f, err := n.ServiceType(serviceName)
	if err != nil {
		swig.DeleteWrappedServiceStub(ws)
		return nil, err
	}
	base := &StubBase{node: n, inner: ws, objectType: objType}
	obj, err := f.CreateStub(objType, base)
	if err != nil {
		swig.DeleteWrappedServiceStub(ws)
		return nil, err
	}
	return obj, nil
}

// StubHolder is implemented by all client object references (generated
// stubs embed *StubBase).
type StubHolder interface {
	RRStubBase() *StubBase
}

func stubBaseOf(obj any) (*StubBase, error) {
	h, ok := obj.(StubHolder)
	if !ok || h.RRStubBase() == nil {
		return nil, NewInvalidArgument(fmt.Sprintf("%T is not a Robot Raconteur object reference", obj))
	}
	return h.RRStubBase(), nil
}

// DisconnectService disconnects the client connection of obj.
func (n *Node) DisconnectService(obj any) error {
	b, err := stubBaseOf(obj)
	if err != nil {
		return err
	}
	return callVoid(func() { n.native.DisconnectService(b.inner) })
}

// RequestTimeout returns the default timeout for requests.
func (n *Node) RequestTimeout() time.Duration {
	return time.Duration(n.native.GetRequestTimeout()) * time.Millisecond
}

// SetRequestTimeout sets the default timeout for requests.
func (n *Node) SetRequestTimeout(t time.Duration) {
	n.native.SetRequestTimeout(uint32(t.Milliseconds()))
}

// TransportInactivityTimeout returns the transport inactivity timeout.
func (n *Node) TransportInactivityTimeout() time.Duration {
	return time.Duration(n.native.GetTransportInactivityTimeout()) * time.Millisecond
}

// SetTransportInactivityTimeout sets the transport inactivity timeout.
func (n *Node) SetTransportInactivityTimeout(t time.Duration) {
	n.native.SetTransportInactivityTimeout(uint32(t.Milliseconds()))
}

// EndpointInactivityTimeout returns the endpoint inactivity timeout.
func (n *Node) EndpointInactivityTimeout() time.Duration {
	return time.Duration(n.native.GetEndpointInactivityTimeout()) * time.Millisecond
}

// SetEndpointInactivityTimeout sets the endpoint inactivity timeout.
func (n *Node) SetEndpointInactivityTimeout(t time.Duration) {
	n.native.SetEndpointInactivityTimeout(uint32(t.Milliseconds()))
}

// MemoryMaxTransferSize returns the maximum memory transfer chunk size in bytes.
func (n *Node) MemoryMaxTransferSize() uint32 { return n.native.GetMemoryMaxTransferSize() }

// SetMemoryMaxTransferSize sets the maximum memory transfer chunk size in bytes.
func (n *Node) SetMemoryMaxTransferSize(s uint32) { n.native.SetMemoryMaxTransferSize(s) }

// NodeName returns the node name.
func (n *Node) NodeName() string { return n.native.NodeName() }

// SetNodeName sets the node name. Must be called before the node is used.
func (n *Node) SetNodeName(name string) error {
	return callVoid(func() { n.native.SetNodeName(name) })
}

// NodeID returns the node id.
func (n *Node) NodeID() (NodeID, error) {
	return call(func() NodeID {
		id := n.native.NodeID()
		defer swig.DeleteNodeID(id)
		return nodeIDFromNative(id)
	})
}

// ThreadPoolCount returns the size of the node's thread pool.
func (n *Node) ThreadPoolCount() int { return int(n.native.GetThreadPoolCount()) }

// SetThreadPoolCount sets the size of the node's thread pool.
func (n *Node) SetThreadPoolCount(c int) error {
	return callVoid(func() { n.native.SetThreadPoolCount(int32(c)) })
}

// NowUTC returns the current time from the node's clock.
func (n *Node) NowUTC() time.Time {
	return timeFromMicros(n.native.NowUTC())
}

// Shutdown shuts down the node. Normally done by closing the NodeSetup.
func (n *Node) Shutdown() error {
	return callVoid(func() { n.native.Shutdown() })
}

// ServicePath returns the service path of an object reference.
func (n *Node) ServicePath(obj any) (string, error) {
	b, err := stubBaseOf(obj)
	if err != nil {
		return "", err
	}
	return call(func() string { return n.native.GetObjectServicePath(b.inner) })
}

// ObjectType returns the fully qualified type of an object reference.
func (n *Node) ObjectType(obj any) (string, error) {
	b, err := stubBaseOf(obj)
	if err != nil {
		return "", err
	}
	return call(func() string { return n.native.GetObjectType(b.inner) })
}

func timeFromMicros(us int64) time.Time {
	return time.UnixMicro(us).UTC()
}
