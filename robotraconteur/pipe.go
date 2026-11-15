package robotraconteur

import (
	"context"
	"sync"

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
	p := &Pipe[T]{name: name, codec: codec, client: c}
	addCleanup(p, c, swig.DeleteWrappedPipeClient)
	return p, nil
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
func (p *Pipe[T]) Connect(ctx context.Context, index int32) (*PipeEndpoint[T], error) {
	if p.client == nil {
		return nil, NewInvalidOperation("pipe is not a client pipe")
	}
	return await(ctx, func(done func(*PipeEndpoint[T], error)) error {
		impl := &asyncPipeEndpoint[T]{codec: p.codec, handler: done}
		d := swig.NewDirectorAsyncPipeEndpointReturnDirector(impl)
		id := heapAdd(impl, func() { swig.DeleteDirectorAsyncPipeEndpointReturnDirector(d) })
		t := DefaultNode().requestTimeoutMs(ctx)
		err := startNative(func() error { p.client.AsyncConnect(index, t, d, id); return nil })
		if err != nil {
			heapRemove(id)
		}
		return err
	}, func(ep *PipeEndpoint[T]) { _ = ep.Close() })
}

type asyncPipeEndpoint[T any] struct {
	codec   Codec[T]
	handler func(*PipeEndpoint[T], error)
}

func (a *asyncPipeEndpoint[T]) Handler(ep swig.WrappedPipeEndpoint, info swig.HandlerErrorInfo) {
	defer recoverDirector()
	err := errorFromInfo(info)
	if err != nil {
		if ep != nil && ep.Swigcptr() != 0 {
			swig.DeleteWrappedPipeEndpoint(ep)
		}
		a.handler(nil, err)
		return
	}
	a.handler(newPipeEndpoint(ep, a.codec))
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
	isClosed       bool
	changed        signal // packet received or endpoint closed
}

type pipeEndpointDirector[T any] struct{ ep *PipeEndpoint[T] }

func (d *pipeEndpointDirector[T]) PipeEndpointClosedCallback() {
	defer recoverDirector()
	d.ep.mu.Lock()
	f := d.ep.closed
	d.ep.isClosed = true
	d.ep.mu.Unlock()
	d.ep.changed.notify()
	if f != nil {
		f(d.ep)
	}
}

func (d *pipeEndpointDirector[T]) PacketReceivedEvent() {
	defer recoverDirector()
	d.ep.changed.notify()
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
	addCleanup(ep, native, swig.DeleteWrappedPipeEndpoint)
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
func (e *PipeEndpoint[T]) PacketReceivedEvent() *Event[func(*PipeEndpoint[T])] {
	return &e.packetReceived
}

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

// SendPacket sends a packet and returns its packet number once the packet
// has been sent.
func (e *PipeEndpoint[T]) SendPacket(ctx context.Context, v T) (uint32, error) {
	pe, err := e.codec.Pack("value", v)
	if err != nil {
		return 0, err
	}
	return await(ctx, func(done func(uint32, error)) error {
		impl := &asyncUInt32{handler: done}
		d := swig.NewDirectorAsyncUInt32ReturnDirector(impl)
		id := heapAdd(impl, func() { swig.DeleteDirectorAsyncUInt32ReturnDirector(d) })
		err := startNative(func() error {
			m, err := toNative(pe)
			if err != nil {
				return err
			}
			defer swig.DeleteMessageElement(m)
			e.native.AsyncSendPacket(m, d, id)
			return nil
		})
		if err != nil {
			heapRemove(id)
		}
		return err
	}, nil)
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

func (e *PipeEndpoint[T]) waitPacket(ctx context.Context, peek bool) (T, error) {
	return waitFor(ctx, &e.changed, func() (T, bool, error) {
		var zero T
		if e.Available() > 0 {
			var v T
			var err error
			if peek {
				v, err = e.PeekNextPacket()
			} else {
				v, err = e.ReceivePacket()
			}
			return v, true, err
		}
		e.mu.Lock()
		closed := e.isClosed
		e.mu.Unlock()
		if closed {
			return zero, false, NewConnectionError("pipe endpoint closed")
		}
		return zero, false, nil
	})
}

// ReceivePacketWait waits for and returns the next packet.
func (e *PipeEndpoint[T]) ReceivePacketWait(ctx context.Context) (T, error) {
	return e.waitPacket(ctx, false)
}

// PeekNextPacketWait waits for the next packet without removing it.
func (e *PipeEndpoint[T]) PeekNextPacketWait(ctx context.Context) (T, error) {
	return e.waitPacket(ctx, true)
}

// Close closes the endpoint and waits for the close to complete (bounded by
// the request timeout).
func (e *PipeEndpoint[T]) Close() error {
	return awaitVoid(context.Background(), func(t int32, d swig.AsyncVoidReturnDirector, id int32) {
		e.native.AsyncClose(t, d, id)
	})
}

// ---- AsyncUInt32ReturnDirector ----

type asyncUInt32 struct{ handler func(uint32, error) }

func (a *asyncUInt32) Handler(v uint32, info swig.HandlerErrorInfo) {
	a.handler(v, errorFromInfo(info))
}
