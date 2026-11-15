package robotraconteur

import (
	"sync"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// MemberDirection is the direction of a property, pipe, wire or memory.
type MemberDirection int32

// Member directions.
const (
	DirectionBoth      MemberDirection = 0
	DirectionReadOnly  MemberDirection = 1
	DirectionWriteOnly MemberDirection = 2
)

// AnyIndex requests any available pipe endpoint index.
const AnyIndex int32 = -1

// Pipe is a Robot Raconteur pipe member carrying packets of type T.
//
// On a client, use Connect to open an endpoint. On a service, the skeleton
// creates the Pipe and passes it to the object (SetX method); set
// PipeConnectCallback to accept connections, or use a PipeBroadcaster.
type Pipe[T any] struct {
	name   string
	codec  Codec[T]
	client swig.WrappedPipeClient
	server swig.WrappedPipeServer

	mu              sync.Mutex
	connectCallback func(*PipeEndpoint[T])
}

// NewPipeClient creates the client side of a pipe member. Used by generated stubs.
func NewPipeClient[T any](b *StubBase, name string, codec Codec[T]) (*Pipe[T], error) {
	c, err := call(func() swig.WrappedPipeClient { return b.inner.GetPipe(name) })
	if err != nil {
		return nil, err
	}
	return &Pipe[T]{name: name, codec: codec, client: c}, nil
}

// MemberName returns the member name of the pipe.
func (p *Pipe[T]) MemberName() string { return p.name }

// Direction returns the direction of the pipe.
func (p *Pipe[T]) Direction() MemberDirection {
	if p.client != nil {
		return MemberDirection(p.client.Direction())
	}
	if p.server != nil {
		return MemberDirection(p.server.Direction())
	}
	return DirectionBoth
}

// Connect connects a pipe endpoint (client). Use AnyIndex for any index.
func (p *Pipe[T]) Connect(index int32) (*PipeEndpoint[T], error) {
	if p.client == nil {
		return nil, NewInvalidOperation("pipe is not a client pipe")
	}
	ep, err := call(func() swig.WrappedPipeEndpoint { return p.client.Connect(index) })
	if err != nil {
		return nil, err
	}
	return newPipeEndpoint(ep, p.codec)
}

// AsyncConnect connects a pipe endpoint asynchronously (client).
func (p *Pipe[T]) AsyncConnect(index int32, timeout time.Duration, handler func(*PipeEndpoint[T], error)) {
	if p.client == nil {
		go handler(nil, NewInvalidOperation("pipe is not a client pipe"))
		return
	}
	impl := &asyncPipeEndpoint[T]{codec: p.codec, handler: handler}
	d := swig.NewDirectorAsyncPipeEndpointReturnDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorAsyncPipeEndpointReturnDirector(d) })
	if err := callVoid(func() { p.client.AsyncConnect(index, timeoutMs(timeout), d, id) }); err != nil {
		heapRemove(id)
		go handler(nil, err)
	}
}

type asyncPipeEndpoint[T any] struct {
	codec   Codec[T]
	handler func(*PipeEndpoint[T], error)
}

func (a *asyncPipeEndpoint[T]) Handler(ep swig.WrappedPipeEndpoint, info swig.HandlerErrorInfo) {
	err := errorFromInfo(info)
	if err != nil {
		if ep != nil && ep.Swigcptr() != 0 {
			swig.DeleteWrappedPipeEndpoint(ep)
		}
		go a.handler(nil, err)
		return
	}
	e, err := newPipeEndpoint(ep, a.codec)
	go a.handler(e, err)
}

// PipeConnectCallback sets the function called when a client connects a
// pipe endpoint (service).
func (p *Pipe[T]) SetPipeConnectCallback(f func(*PipeEndpoint[T])) {
	p.mu.Lock()
	p.connectCallback = f
	p.mu.Unlock()
}

// PipeEndpoint is a connected pipe endpoint.
type PipeEndpoint[T any] struct {
	native swig.WrappedPipeEndpoint
	codec  Codec[T]

	packetReceived Event[func(*PipeEndpoint[T])]
	ackReceived    Event[func(*PipeEndpoint[T], uint32)]
	mu             sync.Mutex
	closed         func(*PipeEndpoint[T])
}

type pipeEndpointDirector[T any] struct{ ep *PipeEndpoint[T] }

func (d *pipeEndpointDirector[T]) PipeEndpointClosedCallback() {
	defer recoverDirector()
	d.ep.mu.Lock()
	f := d.ep.closed
	d.ep.mu.Unlock()
	if f != nil {
		f(d.ep)
	}
}

func (d *pipeEndpointDirector[T]) PacketReceivedEvent() {
	defer recoverDirector()
	for _, h := range d.ep.packetReceived.Handlers() {
		h(d.ep)
	}
}

func (d *pipeEndpointDirector[T]) PacketAckReceivedEvent(n uint32) {
	defer recoverDirector()
	for _, h := range d.ep.ackReceived.Handlers() {
		h(d.ep, n)
	}
}

func newPipeEndpoint[T any](native swig.WrappedPipeEndpoint, codec Codec[T]) (*PipeEndpoint[T], error) {
	if native == nil || native.Swigcptr() == 0 {
		return nil, NewNullValue("null pipe endpoint")
	}
	ep := &PipeEndpoint[T]{native: native, codec: codec}
	impl := &pipeEndpointDirector[T]{ep: ep}
	d := swig.NewDirectorWrappedPipeEndpointDirector(impl)
	id := heapAdd(ep, func() { swig.DeleteDirectorWrappedPipeEndpointDirector(d) })
	if err := callVoid(func() { native.SetRRDirector(d, id) }); err != nil {
		heapRemove(id)
		return nil, err
	}
	return ep, nil
}

// Index returns the endpoint index.
func (e *PipeEndpoint[T]) Index() int32 { return e.native.GetIndex() }

// Endpoint returns the client endpoint id.
func (e *PipeEndpoint[T]) Endpoint() uint32 { return e.native.GetEndpoint() }

// Available returns the number of received packets waiting.
func (e *PipeEndpoint[T]) Available() int { return int(e.native.Available()) }

// IsUnreliable reports whether the pipe is unreliable.
func (e *PipeEndpoint[T]) IsUnreliable() bool { return e.native.IsUnreliable() }

// RequestPacketAck reports whether packet acks are requested.
func (e *PipeEndpoint[T]) RequestPacketAck() bool { return e.native.GetRequestPacketAck() }

// SetRequestPacketAck sets whether packet acks are requested.
func (e *PipeEndpoint[T]) SetRequestPacketAck(v bool) { e.native.SetRequestPacketAck(v) }

// IgnoreReceived reports whether received packets are dropped.
func (e *PipeEndpoint[T]) IgnoreReceived() bool { return e.native.GetIgnoreReceived() }

// SetIgnoreReceived sets whether received packets are dropped.
func (e *PipeEndpoint[T]) SetIgnoreReceived(v bool) { e.native.SetIgnoreReceived(v) }

// PacketReceivedEvent is fired when a packet is received.
func (e *PipeEndpoint[T]) PacketReceivedEvent() *Event[func(*PipeEndpoint[T])] { return &e.packetReceived }

// PacketAckReceivedEvent is fired when a packet ack is received.
func (e *PipeEndpoint[T]) PacketAckReceivedEvent() *Event[func(*PipeEndpoint[T], uint32)] {
	return &e.ackReceived
}

// SetPipeEndpointClosedCallback sets a function called when the endpoint closes.
func (e *PipeEndpoint[T]) SetPipeEndpointClosedCallback(f func(*PipeEndpoint[T])) {
	e.mu.Lock()
	e.closed = f
	e.mu.Unlock()
}

// SendPacket sends a packet and returns its packet number.
func (e *PipeEndpoint[T]) SendPacket(v T) (uint32, error) {
	pe, err := e.codec.Pack("value", v)
	if err != nil {
		return 0, err
	}
	var cerr error
	n, err := call(func() uint32 {
		m, err := toNative(pe)
		if err != nil {
			cerr = err
			return 0
		}
		defer swig.DeleteMessageElement(m)
		return e.native.SendPacket(m)
	})
	if cerr != nil {
		return 0, cerr
	}
	return n, err
}

// AsyncSendPacket sends a packet asynchronously.
func (e *PipeEndpoint[T]) AsyncSendPacket(v T, handler func(uint32, error)) {
	pe, err := e.codec.Pack("value", v)
	if err != nil {
		go handler(0, err)
		return
	}
	impl := &asyncUInt32{handler: handler}
	d := swig.NewDirectorAsyncUInt32ReturnDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorAsyncUInt32ReturnDirector(d) })
	var cerr error
	err = callVoid(func() {
		m, err := toNative(pe)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteMessageElement(m)
		e.native.AsyncSendPacket(m, d, id)
	})
	if cerr != nil {
		err = cerr
	}
	if err != nil {
		heapRemove(id)
		go handler(0, err)
	}
}

func (e *PipeEndpoint[T]) unpackPacket(m swig.MessageElement) (T, error) {
	var zero T
	if m == nil || m.Swigcptr() == 0 {
		return zero, NewNullValue("null packet")
	}
	defer swig.DeleteMessageElement(m)
	el, err := fromNative(m)
	if err != nil {
		return zero, err
	}
	return e.codec.Unpack(el)
}

func (e *PipeEndpoint[T]) recv(f func() swig.MessageElement) (T, error) {
	var out T
	var cerr error
	err := callVoid(func() {
		m := f()
		if swig.Rrgo_error_pending() {
			return
		}
		out, cerr = e.unpackPacket(m)
	})
	if err != nil {
		var zero T
		return zero, err
	}
	return out, cerr
}

// ReceivePacket returns the next received packet (InvalidOperation error if none).
func (e *PipeEndpoint[T]) ReceivePacket() (T, error) {
	return e.recv(func() swig.MessageElement { return e.native.ReceivePacket() })
}

// PeekNextPacket returns the next packet without removing it.
func (e *PipeEndpoint[T]) PeekNextPacket() (T, error) {
	return e.recv(func() swig.MessageElement { return e.native.PeekNextPacket() })
}

// ReceivePacketWait waits for and returns the next packet.
func (e *PipeEndpoint[T]) ReceivePacketWait(timeout time.Duration) (T, error) {
	t := timeoutMs(timeout)
	return e.recv(func() swig.MessageElement {
		return swig.SwigcptrWrappedPipeEndpoint(e.native.Swigcptr()).ReceivePacketWait__SWIG_0(t)
	})
}

// PeekNextPacketWait waits for the next packet without removing it.
func (e *PipeEndpoint[T]) PeekNextPacketWait(timeout time.Duration) (T, error) {
	t := timeoutMs(timeout)
	return e.recv(func() swig.MessageElement {
		return swig.SwigcptrWrappedPipeEndpoint(e.native.Swigcptr()).PeekNextPacketWait__SWIG_0(t)
	})
}

// TryReceivePacketWait waits for a packet; ok is false on timeout.
func (e *PipeEndpoint[T]) TryReceivePacketWait(timeout time.Duration, peek bool) (v T, ok bool, err error) {
	t := timeoutMs(timeout)
	var cerr error
	err = callVoid(func() {
		r := swig.SwigcptrWrappedPipeEndpoint(e.native.Swigcptr()).TryReceivePacketWait__SWIG_0(t, peek)
		if r == nil || r.Swigcptr() == 0 {
			return
		}
		defer swig.DeleteWrappedTryReceivePacketWaitResult(r)
		if !r.GetRes() {
			return
		}
		ok = true
		v, cerr = e.unpackPacket(r.GetPacket())
	})
	if err == nil {
		err = cerr
	}
	return
}

// Close closes the endpoint.
func (e *PipeEndpoint[T]) Close() error {
	return callVoid(func() { e.native.Close() })
}

// AsyncClose closes the endpoint asynchronously.
func (e *PipeEndpoint[T]) AsyncClose(timeout time.Duration, handler func(error)) {
	d, id := newAsyncVoid(handler)
	if err := callVoid(func() { e.native.AsyncClose(timeoutMs(timeout), d, id) }); err != nil {
		heapRemove(id)
		go handler(err)
	}
}

// ---- AsyncUInt32ReturnDirector ----

type asyncUInt32 struct{ handler func(uint32, error) }

func (a *asyncUInt32) Handler(v uint32, info swig.HandlerErrorInfo) {
	err := errorFromInfo(info)
	go a.handler(v, err)
}
