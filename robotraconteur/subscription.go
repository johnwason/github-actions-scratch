package robotraconteur

import (
	"context"
	"sync"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// ServiceSubscriptionClientID identifies a client connection of a subscription.
type ServiceSubscriptionClientID struct {
	NodeID      NodeID
	ServiceName string
}

func clientIDFromNative(c swig.WrappedServiceSubscriptionClientID) ServiceSubscriptionClientID {
	return ServiceSubscriptionClientID{NodeID: nodeIDFromNative(c.GetNodeID()), ServiceName: c.GetServiceName()}
}

// ServiceSubscriptionFilterNode selects nodes for a subscription by type.
type ServiceSubscriptionFilterNode struct {
	NodeID      NodeID
	NodeName    string
	Username    string
	Credentials map[string]any
}

// ServiceSubscriptionFilter restricts the services a subscription by type
// connects to.
type ServiceSubscriptionFilter struct {
	Nodes            []ServiceSubscriptionFilterNode
	ServiceNames     []string
	TransportSchemes []string
	// Predicate, if set, must return true for a service to be used.
	Predicate      func(ServiceInfo2) bool
	MaxConnections uint32
}

type filterPredicate struct{ f func(ServiceInfo2) bool }

func (p *filterPredicate) Predicate(info swig.ServiceInfo2Wrapped) bool {
	defer recoverDirector()
	return p.f(serviceInfo2FromNative(info))
}

func (n *Node) filterToNative(f *ServiceSubscriptionFilter) (swig.WrappedServiceSubscriptionFilter, error) {
	if f == nil {
		return nil, nil
	}
	nf := swig.NewWrappedServiceSubscriptionFilter()
	sn := stringsToNative(f.ServiceNames)
	defer swig.DeleteVectorstring(sn)
	nf.SetServiceNames(sn)
	ts := stringsToNative(f.TransportSchemes)
	defer swig.DeleteVectorstring(ts)
	nf.SetTransportSchemes(ts)
	nf.SetMaxConnections(f.MaxConnections)
	if len(f.Nodes) > 0 {
		v := swig.NewVectorptr_wrappedservicesubscriptionnode()
		defer swig.DeleteVectorptr_wrappedservicesubscriptionnode(v)
		for _, fn := range f.Nodes {
			nn := swig.NewWrappedServiceSubscriptionFilterNode()
			id := fn.NodeID.toNative()
			nn.SetNodeID(id)
			swig.DeleteNodeID(id)
			nn.SetNodeName(fn.NodeName)
			nn.SetUsername(fn.Username)
			if fn.Credentials != nil {
				cred, err := n.packCredentials(fn.Credentials)
				if err != nil {
					swig.DeleteWrappedServiceSubscriptionFilterNode(nn)
					return nil, err
				}
				nn.SetCredentials(cred)
				swig.DeleteMessageElementData(cred)
			}
			v.Add(nn)
			swig.DeleteWrappedServiceSubscriptionFilterNode(nn)
		}
		nf.SetNodes(v)
	}
	if f.Predicate != nil {
		impl := &filterPredicate{f: f.Predicate}
		d := swig.NewDirectorWrappedServiceSubscriptionFilterPredicateDirector(impl)
		id := heapAdd(impl, func() { swig.DeleteDirectorWrappedServiceSubscriptionFilterPredicateDirector(d) })
		nf.SetRRPredicateDirector(d, id)
	}
	return nf, nil
}

// ---------------------------------------------------------------- service subscription

// ServiceSubscription keeps connections to services found by URL or by type
// and reconnects automatically.
type ServiceSubscription struct {
	node   *Node
	native swig.WrappedServiceSubscription

	connected    Event[func(s *ServiceSubscription, id ServiceSubscriptionClientID, client any)]
	disconnected Event[func(s *ServiceSubscription, id ServiceSubscriptionClientID, client any)]
	failed       Event[func(s *ServiceSubscription, id ServiceSubscriptionClientID, urls []string, err error)]

	childMu  sync.Mutex
	children []interface{ Close() error } // wire/pipe subscriptions, closed with s
}

func (s *ServiceSubscription) addChild(c interface{ Close() error }) {
	s.childMu.Lock()
	s.children = append(s.children, c)
	s.childMu.Unlock()
}

type serviceSubscriptionDirector struct{ s *ServiceSubscription }

func (d *serviceSubscriptionDirector) client(stub swig.WrappedServiceStub) any {
	if stub == nil || stub.Swigcptr() == 0 {
		return nil
	}
	o, err := d.s.node.stubFromNative(stub)
	if err != nil {
		return nil
	}
	return o
}

func (d *serviceSubscriptionDirector) ClientConnected(sub swig.WrappedServiceSubscription, id swig.WrappedServiceSubscriptionClientID, stub swig.WrappedServiceStub) {
	defer recoverDirector()
	if sub != nil && sub.Swigcptr() != 0 {
		swig.DeleteWrappedServiceSubscription(sub)
	}
	cid := clientIDFromNative(id)
	c := d.client(stub)
	for _, h := range d.s.connected.Handlers() {
		h(d.s, cid, c)
	}
}

func (d *serviceSubscriptionDirector) ClientDisconnected(sub swig.WrappedServiceSubscription, id swig.WrappedServiceSubscriptionClientID, stub swig.WrappedServiceStub) {
	defer recoverDirector()
	if sub != nil && sub.Swigcptr() != 0 {
		swig.DeleteWrappedServiceSubscription(sub)
	}
	cid := clientIDFromNative(id)
	c := d.client(stub)
	for _, h := range d.s.disconnected.Handlers() {
		h(d.s, cid, c)
	}
}

func (d *serviceSubscriptionDirector) ClientConnectFailed(sub swig.WrappedServiceSubscription, id swig.WrappedServiceSubscriptionClientID, urls swig.Vectorstring, info swig.HandlerErrorInfo) {
	defer recoverDirector()
	if sub != nil && sub.Swigcptr() != 0 {
		swig.DeleteWrappedServiceSubscription(sub)
	}
	cid := clientIDFromNative(id)
	u := stringsFromNative(urls)
	err := errorFromInfo(info)
	for _, h := range d.s.failed.Handlers() {
		h(d.s, cid, u, err)
	}
}

var _ swig.WrappedServiceSubscriptionDirectorMethods = (*serviceSubscriptionDirector)(nil)
var _ swig.WrappedServiceSubscriptionFilterPredicateDirectorMethods = (*filterPredicate)(nil)

func (n *Node) newServiceSubscription(native swig.WrappedServiceSubscription) (*ServiceSubscription, error) {
	s := &ServiceSubscription{node: n, native: native}
	addCleanup(s, native, swig.DeleteWrappedServiceSubscription)
	impl := &serviceSubscriptionDirector{s: s}
	d := swig.NewDirectorWrappedServiceSubscriptionDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedServiceSubscriptionDirector(d) })
	if err := callVoid(func() { native.SetRRDirector(d, id) }); err != nil {
		heapRemove(id)
		return nil, err
	}
	return s, nil
}

// SubscribeService subscribes to a service by URL (or a list of candidate URLs).
func (n *Node) SubscribeService(urls []string, opts ...ConnectOption) (*ServiceSubscription, error) {
	var o connectOptions
	for _, f := range opts {
		f(&o)
	}
	if err := n.RegisterServiceTypes(); err != nil {
		return nil, err
	}
	var cerr error
	s, err := call(func() swig.WrappedServiceSubscription {
		cred, err := n.packCredentials(o.credentials)
		if err != nil {
			cerr = err
			return nil
		}
		if cred != nil {
			defer swig.DeleteMessageElementData(cred)
		}
		uv := stringsToNative(urls)
		defer swig.DeleteVectorstring(uv)
		return swig.WrappedSubscribeService__SWIG_0(n.native, uv, o.username, cred, o.objectType)
	})
	if cerr != nil {
		return nil, cerr
	}
	if err != nil {
		return nil, err
	}
	return n.newServiceSubscription(s)
}

// SubscribeServiceByType subscribes to all services implementing one of
// serviceTypes found by discovery (optionally filtered).
func (n *Node) SubscribeServiceByType(serviceTypes []string, filter *ServiceSubscriptionFilter) (*ServiceSubscription, error) {
	if err := n.RegisterServiceTypes(); err != nil {
		return nil, err
	}
	var cerr error
	s, err := call(func() swig.WrappedServiceSubscription {
		nf, err := n.filterToNative(filter)
		if err != nil {
			cerr = err
			return nil
		}
		if nf != nil {
			defer swig.DeleteWrappedServiceSubscriptionFilter(nf)
		}
		tv := stringsToNative(serviceTypes)
		defer swig.DeleteVectorstring(tv)
		return swig.WrappedSubscribeServiceByType__SWIG_0(n.native, tv, nf)
	})
	if cerr != nil {
		return nil, cerr
	}
	if err != nil {
		return nil, err
	}
	return n.newServiceSubscription(s)
}

func (s *ServiceSubscription) nat() swig.SwigcptrWrappedServiceSubscription {
	return swig.SwigcptrWrappedServiceSubscription(s.native.Swigcptr())
}

// ClientConnected is fired when a client connects.
func (s *ServiceSubscription) ClientConnected() *Event[func(s *ServiceSubscription, id ServiceSubscriptionClientID, client any)] {
	return &s.connected
}

// ClientDisconnected is fired when a client disconnects.
func (s *ServiceSubscription) ClientDisconnected() *Event[func(s *ServiceSubscription, id ServiceSubscriptionClientID, client any)] {
	return &s.disconnected
}

// ClientConnectFailed is fired when a connection attempt fails.
func (s *ServiceSubscription) ClientConnectFailed() *Event[func(s *ServiceSubscription, id ServiceSubscriptionClientID, urls []string, err error)] {
	return &s.failed
}

// ConnectedClients returns the connected clients by id.
func (s *ServiceSubscription) ConnectedClients() (map[ServiceSubscriptionClientID]any, error) {
	var stubs []swig.WrappedServiceStub
	var ids []ServiceSubscriptionClientID
	err := callVoid(func() {
		m := s.native.GetConnectedClients()
		defer swig.DeleteMap_subscriptionclients(m)
		v := swig.WrappedServiceSubscriptionClientsToVector(m)
		defer swig.DeleteVectorptr_subscriptionclientid(v)
		for i := 0; i < int(v.Size()); i++ {
			id := v.Get(i)
			ids = append(ids, clientIDFromNative(id))
			stubs = append(stubs, m.Get(id))
		}
	})
	if err != nil {
		return nil, err
	}
	o := make(map[ServiceSubscriptionClientID]any, len(ids))
	for i, id := range ids {
		c, err := s.node.stubFromNative(stubs[i])
		if err == nil {
			o[id] = c
		}
	}
	return o, nil
}

// DefaultClient returns the first connected client (ServiceNotFound error if none).
func (s *ServiceSubscription) DefaultClient() (any, error) {
	st, err := call(func() swig.WrappedServiceStub { return s.native.GetDefaultClient() })
	if err != nil {
		return nil, err
	}
	return s.node.stubFromNative(st)
}

// DefaultClientWait waits for a connected client.
func (s *ServiceSubscription) DefaultClientWait(ctx context.Context) (any, error) {
	return await(ctx, func(done func(any, error)) error {
		d, id := newAsyncStub(s.node, done)
		t := ctxTimeoutMs(ctx, -1)
		err := startNative(func() error { s.native.AsyncGetDefaultClient(t, d, id); return nil })
		if err != nil {
			heapRemove(id)
		}
		return err
	}, nil)
}

// ConnectRetryDelay returns the delay between connection attempts.
func (s *ServiceSubscription) ConnectRetryDelay() time.Duration {
	return time.Duration(s.native.GetConnectRetryDelay()) * time.Millisecond
}

// SetConnectRetryDelay sets the delay between connection attempts.
func (s *ServiceSubscription) SetConnectRetryDelay(d time.Duration) {
	s.native.SetConnectRetryDelay(uint32(d.Milliseconds()))
}

// ServiceURL returns the URLs of a subscription by URL.
func (s *ServiceSubscription) ServiceURL() ([]string, error) {
	return call(func() []string {
		v := s.native.GetServiceURL()
		defer swig.DeleteVectorstring(v)
		return stringsFromNative(v)
	})
}

// UpdateServiceURL changes the URLs of a subscription by URL.
func (s *ServiceSubscription) UpdateServiceURL(urls []string, closeConnected bool, opts ...ConnectOption) error {
	var o connectOptions
	for _, f := range opts {
		f(&o)
	}
	var cerr error
	err := callVoid(func() {
		cred, err := s.node.packCredentials(o.credentials)
		if err != nil {
			cerr = err
			return
		}
		if cred != nil {
			defer swig.DeleteMessageElementData(cred)
		}
		uv := stringsToNative(urls)
		defer swig.DeleteVectorstring(uv)
		s.nat().UpdateServiceURL__SWIG_0(uv, o.username, cred, o.objectType, closeConnected)
	})
	if cerr != nil {
		return cerr
	}
	return err
}

// UpdateServiceByType changes the types and filter of a subscription by type.
func (s *ServiceSubscription) UpdateServiceByType(serviceTypes []string, filter *ServiceSubscriptionFilter) error {
	var cerr error
	err := callVoid(func() {
		nf, err := s.node.filterToNative(filter)
		if err != nil {
			cerr = err
			return
		}
		if nf != nil {
			defer swig.DeleteWrappedServiceSubscriptionFilter(nf)
		}
		tv := stringsToNative(serviceTypes)
		defer swig.DeleteVectorstring(tv)
		s.nat().UpdateServiceByType__SWIG_0(tv, nf)
	})
	if cerr != nil {
		return cerr
	}
	return err
}

// ClaimClient keeps a client connected even if it no longer matches.
func (s *ServiceSubscription) ClaimClient(client any) error {
	b, err := stubBaseOf(client)
	if err != nil {
		return err
	}
	return callVoid(func() { s.native.ClaimClient(b.inner) })
}

// ReleaseClient releases a claimed client.
func (s *ServiceSubscription) ReleaseClient(client any) error {
	b, err := stubBaseOf(client)
	if err != nil {
		return err
	}
	return callVoid(func() { s.native.ReleaseClient(b.inner) })
}

// Close closes the subscription and its connections.
// Close closes the subscription and the wire and pipe subscriptions
// created from it.
func (s *ServiceSubscription) Close() error {
	s.childMu.Lock()
	children := s.children
	s.children = nil
	s.childMu.Unlock()
	for _, c := range children {
		_ = c.Close()
	}
	return callVoid(func() { s.native.Close() })
}

// ---------------------------------------------------------------- wire subscription

// WireSubscription receives the latest value of a wire member from all
// clients of a service subscription.
type WireSubscription[T any] struct {
	native   swig.WrappedWireSubscription
	codec    Codec[T]
	changed  Event[func(s *WireSubscription[T], value T, ts TimeSpec)]
	received signal
}

type wireSubscriptionDirector[T any] struct{ s *WireSubscription[T] }

func (d *wireSubscriptionDirector[T]) WireValueChanged(sub swig.WrappedWireSubscription, value swig.WrappedService_typed_packet, ts swig.TimeSpec) {
	defer recoverDirector()
	if sub != nil && sub.Swigcptr() != 0 {
		swig.DeleteWrappedWireSubscription(sub)
	}
	defer d.s.received.notify()
	hs := d.s.changed.Handlers()
	if len(hs) == 0 {
		return
	}
	v, err := unpackTypedPacket(value, d.s.codec)
	if err != nil {
		return
	}
	t := timeSpecFromNative(ts)
	for _, h := range hs {
		h(d.s, v, t)
	}
}

func unpackTypedPacket[T any](p swig.WrappedService_typed_packet, c Codec[T]) (T, error) {
	var zero T
	m := p.GetPacket()
	if m == nil || m.Swigcptr() == 0 {
		return zero, NewValueNotSet("value not set")
	}
	defer swig.DeleteMessageElement(m)
	e, err := fromNative(m)
	if err != nil {
		return zero, err
	}
	return c.Unpack(e)
}

// SubscribeWire subscribes to a wire member of the service objects of a
// subscription. servicePath is relative to the root ("" for the root
// object). The codec for T is found with CodecFor.
func SubscribeWire[T any](s *ServiceSubscription, member, servicePath string) (*WireSubscription[T], error) {
	c, err := CodecFor[T]()
	if err != nil {
		return nil, err
	}
	return SubscribeWireCodec(s, member, servicePath, c)
}

// SubscribeWireCodec is SubscribeWire with an explicit codec.
func SubscribeWireCodec[T any](s *ServiceSubscription, member, servicePath string, codec Codec[T]) (*WireSubscription[T], error) {
	n, err := call(func() swig.WrappedWireSubscription { return s.native.SubscribeWire(member, servicePath) })
	if err != nil {
		return nil, err
	}
	ws := &WireSubscription[T]{native: n, codec: codec}
	addCleanup(ws, n, swig.DeleteWrappedWireSubscription)
	impl := &wireSubscriptionDirector[T]{s: ws}
	d := swig.NewDirectorWrappedWireSubscriptionDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedWireSubscriptionDirector(d) })
	if err := callVoid(func() { n.SetRRDirector(d, id) }); err != nil {
		heapRemove(id)
		return nil, err
	}
	s.addChild(ws)
	return ws, nil
}

// WireValueChanged is fired when a new value is received.
func (s *WireSubscription[T]) WireValueChanged() *Event[func(s *WireSubscription[T], value T, ts TimeSpec)] {
	return &s.changed
}

// InValue returns the current value (ValueNotSet error if none).
func (s *WireSubscription[T]) InValue() (T, TimeSpec, error) {
	var out T
	var ts TimeSpec
	var cerr error
	err := callVoid(func() {
		t := swig.NewTimeSpec__SWIG_0()
		defer swig.DeleteTimeSpec(t)
		p := swig.SwigcptrWrappedWireSubscription(s.native.Swigcptr()).GetInValue__SWIG_0(t)
		if swig.Rrgo_error_pending() {
			return
		}
		defer swig.DeleteWrappedService_typed_packet(p)
		ts = timeSpecFromNative(t)
		out, cerr = unpackTypedPacket(p, s.codec)
	})
	if err == nil {
		err = cerr
	}
	return out, ts, err
}

// WaitInValueValid waits until a value has been received.
func (s *WireSubscription[T]) WaitInValueValid(ctx context.Context) error {
	_, err := waitFor(ctx, &s.received, func() (struct{}, bool, error) {
		_, _, err := s.InValue()
		return struct{}{}, err == nil, nil
	})
	return err
}

// ActiveWireConnectionCount returns the number of connected wires.
func (s *WireSubscription[T]) ActiveWireConnectionCount() int {
	return int(s.native.GetActiveWireConnectionCount())
}

// InValueLifespan returns the lifespan of received values.
func (s *WireSubscription[T]) InValueLifespan() time.Duration {
	return lifespan(s.native.GetInValueLifespan())
}

// SetInValueLifespan sets the lifespan of received values.
func (s *WireSubscription[T]) SetInValueLifespan(t time.Duration) {
	s.native.SetInValueLifespan(timeoutMs(t))
}

// Close closes the wire subscription.
func (s *WireSubscription[T]) Close() error { return callVoid(func() { s.native.Close() }) }

// ---------------------------------------------------------------- pipe subscription

// PipeSubscription receives packets of a pipe member from all clients of a
// service subscription.
type PipeSubscription[T any] struct {
	native   swig.WrappedPipeSubscription
	codec    Codec[T]
	received Event[func(s *PipeSubscription[T])]
	arrived  signal
}

type pipeSubscriptionDirector[T any] struct{ s *PipeSubscription[T] }

func (d *pipeSubscriptionDirector[T]) PipePacketReceived(sub swig.WrappedPipeSubscription) {
	defer recoverDirector()
	if sub != nil && sub.Swigcptr() != 0 {
		swig.DeleteWrappedPipeSubscription(sub)
	}
	d.s.arrived.notify()
	for _, h := range d.s.received.Handlers() {
		h(d.s)
	}
}

// SubscribePipe subscribes to a pipe member. maxRecvPackets limits queued
// packets (-1 for no limit). The codec for T is found with CodecFor.
func SubscribePipe[T any](s *ServiceSubscription, member, servicePath string, maxRecvPackets int32) (*PipeSubscription[T], error) {
	c, err := CodecFor[T]()
	if err != nil {
		return nil, err
	}
	return SubscribePipeCodec(s, member, servicePath, maxRecvPackets, c)
}

// SubscribePipeCodec is SubscribePipe with an explicit codec.
func SubscribePipeCodec[T any](s *ServiceSubscription, member, servicePath string, maxRecvPackets int32, codec Codec[T]) (*PipeSubscription[T], error) {
	n, err := call(func() swig.WrappedPipeSubscription {
		return s.nat().SubscribePipe__SWIG_0(member, servicePath, maxRecvPackets)
	})
	if err != nil {
		return nil, err
	}
	ps := &PipeSubscription[T]{native: n, codec: codec}
	addCleanup(ps, n, swig.DeleteWrappedPipeSubscription)
	impl := &pipeSubscriptionDirector[T]{s: ps}
	d := swig.NewDirectorWrappedPipeSubscriptionDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedPipeSubscriptionDirector(d) })
	if err := callVoid(func() { n.SetRRDirector(d, id) }); err != nil {
		heapRemove(id)
		return nil, err
	}
	s.addChild(ps)
	return ps, nil
}

// PipePacketReceived is fired when packets are available.
func (s *PipeSubscription[T]) PipePacketReceived() *Event[func(s *PipeSubscription[T])] {
	return &s.received
}

// Available returns the number of queued packets.
func (s *PipeSubscription[T]) Available() int { return int(s.native.Available()) }

// ActivePipeEndpointCount returns the number of connected pipe endpoints.
func (s *PipeSubscription[T]) ActivePipeEndpointCount() int {
	return int(s.native.GetActivePipeEndpointCount())
}

// ReceivePacket returns the next queued packet.
func (s *PipeSubscription[T]) ReceivePacket() (T, error) {
	var out T
	var cerr error
	err := callVoid(func() {
		p := s.native.ReceivePacket()
		if swig.Rrgo_error_pending() {
			return
		}
		defer swig.DeleteWrappedService_typed_packet(p)
		out, cerr = unpackTypedPacket(p, s.codec)
	})
	if err == nil {
		err = cerr
	}
	return out, err
}

// ReceivePacketWait waits for and returns the next packet.
func (s *PipeSubscription[T]) ReceivePacketWait(ctx context.Context) (T, error) {
	return waitFor(ctx, &s.arrived, func() (T, bool, error) {
		if s.Available() > 0 {
			v, err := s.ReceivePacket()
			return v, true, err
		}
		var zero T
		return zero, false, nil
	})
}

// Close closes the pipe subscription.
func (s *PipeSubscription[T]) Close() error { return callVoid(func() { s.native.Close() }) }

// ---------------------------------------------------------------- service info subscription

// ServiceInfo2Subscription tracks services found by discovery without connecting.
type ServiceInfo2Subscription struct {
	native   swig.WrappedServiceInfo2Subscription
	mu       sync.Mutex
	detected Event[func(s *ServiceInfo2Subscription, id ServiceSubscriptionClientID, info ServiceInfo2)]
	lost     Event[func(s *ServiceInfo2Subscription, id ServiceSubscriptionClientID, info ServiceInfo2)]
}

type serviceInfo2SubscriptionDirector struct{ s *ServiceInfo2Subscription }

func (d *serviceInfo2SubscriptionDirector) ServiceDetected(sub swig.WrappedServiceInfo2Subscription, id swig.WrappedServiceSubscriptionClientID, info swig.ServiceInfo2Wrapped) {
	defer recoverDirector()
	if sub != nil && sub.Swigcptr() != 0 {
		swig.DeleteWrappedServiceInfo2Subscription(sub)
	}
	cid, si := clientIDFromNative(id), serviceInfo2FromNative(info)
	for _, h := range d.s.detected.Handlers() {
		h(d.s, cid, si)
	}
}

func (d *serviceInfo2SubscriptionDirector) ServiceLost(sub swig.WrappedServiceInfo2Subscription, id swig.WrappedServiceSubscriptionClientID, info swig.ServiceInfo2Wrapped) {
	defer recoverDirector()
	if sub != nil && sub.Swigcptr() != 0 {
		swig.DeleteWrappedServiceInfo2Subscription(sub)
	}
	cid, si := clientIDFromNative(id), serviceInfo2FromNative(info)
	for _, h := range d.s.lost.Handlers() {
		h(d.s, cid, si)
	}
}

// SubscribeServiceInfo2 tracks services implementing serviceTypes.
func (n *Node) SubscribeServiceInfo2(serviceTypes []string, filter *ServiceSubscriptionFilter) (*ServiceInfo2Subscription, error) {
	var cerr error
	native, err := call(func() swig.WrappedServiceInfo2Subscription {
		nf, err := n.filterToNative(filter)
		if err != nil {
			cerr = err
			return nil
		}
		if nf != nil {
			defer swig.DeleteWrappedServiceSubscriptionFilter(nf)
		}
		tv := stringsToNative(serviceTypes)
		defer swig.DeleteVectorstring(tv)
		return swig.WrappedSubscribeServiceInfo2__SWIG_0(n.native, tv, nf)
	})
	if cerr != nil {
		return nil, cerr
	}
	if err != nil {
		return nil, err
	}
	s := &ServiceInfo2Subscription{native: native}
	addCleanup(s, native, swig.DeleteWrappedServiceInfo2Subscription)
	impl := &serviceInfo2SubscriptionDirector{s: s}
	d := swig.NewDirectorWrappedServiceInfo2SubscriptionDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedServiceInfo2SubscriptionDirector(d) })
	if err := callVoid(func() { native.SetRRDirector(d, id) }); err != nil {
		heapRemove(id)
		return nil, err
	}
	return s, nil
}

// ServiceDetected is fired when a service is detected.
func (s *ServiceInfo2Subscription) ServiceDetected() *Event[func(s *ServiceInfo2Subscription, id ServiceSubscriptionClientID, info ServiceInfo2)] {
	return &s.detected
}

// ServiceLost is fired when a service is lost.
func (s *ServiceInfo2Subscription) ServiceLost() *Event[func(s *ServiceInfo2Subscription, id ServiceSubscriptionClientID, info ServiceInfo2)] {
	return &s.lost
}

// DetectedServiceInfo2 returns the currently detected services.
func (s *ServiceInfo2Subscription) DetectedServiceInfo2() (map[ServiceSubscriptionClientID]ServiceInfo2, error) {
	return call(func() map[ServiceSubscriptionClientID]ServiceInfo2 {
		m := s.native.GetDetectedServiceInfo2()
		defer swig.DeleteMap_subscriptionserviceinfo2(m)
		v := swig.WrappedServiceInfo2SubscriptionServicesToVector(m)
		defer swig.DeleteVectorptr_subscriptionclientid(v)
		o := map[ServiceSubscriptionClientID]ServiceInfo2{}
		for i := 0; i < int(v.Size()); i++ {
			id := v.Get(i)
			o[clientIDFromNative(id)] = serviceInfo2FromNative(m.Get(id))
		}
		return o
	})
}

// Close closes the subscription.
func (s *ServiceInfo2Subscription) Close() error { return callVoid(func() { s.native.Close() }) }

var (
	_ swig.WrappedWireSubscriptionDirectorMethods         = (*wireSubscriptionDirector[int32])(nil)
	_ swig.WrappedPipeSubscriptionDirectorMethods         = (*pipeSubscriptionDirector[int32])(nil)
	_ swig.WrappedServiceInfo2SubscriptionDirectorMethods = (*serviceInfo2SubscriptionDirector)(nil)
)
