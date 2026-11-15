package robotraconteur

import (
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// ---------------------------------------------------------------- pipes (service side)

type pipeServerConnect[T any] struct{ p *Pipe[T] }

func (d *pipeServerConnect[T]) PipeConnectCallback(ep swig.WrappedPipeEndpoint) {
	defer recoverDirector()
	e, err := newPipeEndpoint(ep, d.p.codec)
	if err != nil {
		setDirectorError(err)
		return
	}
	d.p.mu.Lock()
	f := d.p.connectCallback
	d.p.mu.Unlock()
	if f != nil {
		f(e)
	}
}

// NewPipeServer creates the service side of a pipe member. Used by generated skeletons.
func NewPipeServer[T any](b *SkelBase, name string, codec Codec[T]) (*Pipe[T], error) {
	s, err := call(func() swig.WrappedPipeServer { return b.inner.GetPipe(name) })
	if err != nil {
		return nil, err
	}
	p := &Pipe[T]{name: name, codec: codec, server: s}
	impl := &pipeServerConnect[T]{p: p}
	d := swig.NewDirectorWrappedPipeServerConnectDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedPipeServerConnectDirector(d) })
	if err := callVoid(func() { s.SetWrappedPipeConnectCallback(d, id) }); err != nil {
		heapRemove(id)
		return nil, err
	}
	return p, nil
}

// PipeBroadcaster sends packets to all connected endpoints of a service pipe.
type PipeBroadcaster[T any] struct {
	native swig.WrappedPipeBroadcaster
	codec  Codec[T]
}

// NewPipeBroadcaster creates a broadcaster for a service pipe. maximumBacklog
// limits unacknowledged packets per endpoint (-1 for no limit).
func NewPipeBroadcaster[T any](p *Pipe[T], maximumBacklog int32) (*PipeBroadcaster[T], error) {
	if p == nil || p.server == nil {
		return nil, NewInvalidArgument("pipe is not a service pipe")
	}
	n, err := call(func() swig.WrappedPipeBroadcaster {
		n := swig.NewWrappedPipeBroadcaster()
		swig.SwigcptrWrappedPipeBroadcaster(n.Swigcptr()).Init__SWIG_0(p.server, maximumBacklog)
		return n
	})
	if err != nil {
		return nil, err
	}
	return &PipeBroadcaster[T]{native: n, codec: p.codec}, nil
}

// SendPacket sends a packet to all connected endpoints.
func (b *PipeBroadcaster[T]) SendPacket(v T) error {
	e, err := b.codec.Pack("value", v)
	if err != nil {
		return err
	}
	var cerr error
	err = callVoid(func() {
		m, err := toNative(e)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteMessageElement(m)
		b.native.SendPacket(m)
	})
	if err != nil {
		return err
	}
	return cerr
}

// ActivePipeEndpointCount returns the number of connected endpoints.
func (b *PipeBroadcaster[T]) ActivePipeEndpointCount() int {
	return int(b.native.GetActivePipeEndpointCount())
}

// MaxBacklog returns the maximum backlog.
func (b *PipeBroadcaster[T]) MaxBacklog() int32 { return b.native.GetMaxBacklog() }

// SetMaxBacklog sets the maximum backlog.
func (b *PipeBroadcaster[T]) SetMaxBacklog(n int32) { b.native.SetMaxBacklog(n) }

type pipePredicate struct {
	f func(endpoint uint32, index int32) bool
}

func (p *pipePredicate) Predicate(ep uint32, index int32) bool {
	defer recoverDirector()
	return p.f(ep, index)
}

// SetPredicate sets a function selecting the endpoints that receive packets.
func (b *PipeBroadcaster[T]) SetPredicate(f func(endpoint uint32, index int32) bool) error {
	impl := &pipePredicate{f: f}
	d := swig.NewDirectorWrappedPipeBroadcasterPredicateDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedPipeBroadcasterPredicateDirector(d) })
	return callVoid(func() { b.native.SetPredicateDirector(d, id) })
}

// ---------------------------------------------------------------- wires (service side)

type wireServerConnect[T any] struct{ w *Wire[T] }

func (d *wireServerConnect[T]) WireConnectCallback(c swig.WrappedWireConnection) {
	defer recoverDirector()
	wc, err := newWireConnection(c, d.w.codec)
	if err != nil {
		setDirectorError(err)
		return
	}
	d.w.mu.Lock()
	f := d.w.connectCallback
	d.w.mu.Unlock()
	if f != nil {
		f(wc)
	}
}

type wirePeek[T any] struct {
	w  *Wire[T]
	in bool
}

func (d *wirePeek[T]) PeekValue(ep uint32) swig.MessageElement {
	defer recoverDirector()
	d.w.mu.Lock()
	f := d.w.peekOut
	if d.in {
		f = d.w.peekIn
	}
	d.w.mu.Unlock()
	if f == nil {
		setDirectorError(NewValueNotSet("value not set"))
		return nil
	}
	v, _, err := f(ep)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	e, err := d.w.codec.Pack("value", v)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	m, err := toNative(e)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	return m
}

type wirePoke[T any] struct {
	w *Wire[T]
	f func(value T, ts TimeSpec, endpoint uint32) error
}

func (d *wirePoke[T]) PokeValue(value swig.MessageElement, ts swig.TimeSpec, ep uint32) {
	defer recoverDirector()
	if value != nil && value.Swigcptr() != 0 {
		defer swig.DeleteMessageElement(value)
	}
	f := d.f
	if f == nil {
		d.w.mu.Lock()
		f = d.w.pokeOut
		d.w.mu.Unlock()
	}
	if f == nil {
		setDirectorError(NewInvalidOperation("wire poke not supported"))
		return
	}
	e, err := fromNative(value)
	if err != nil {
		setDirectorError(err)
		return
	}
	v, err := d.w.codec.Unpack(e)
	if err != nil {
		setDirectorError(err)
		return
	}
	if err := f(v, timeSpecFromNative(ts), ep); err != nil {
		setDirectorError(err)
	}
}

// NewWireServer creates the service side of a wire member. Used by generated skeletons.
func NewWireServer[T any](b *SkelBase, name string, codec Codec[T]) (*Wire[T], error) {
	s, err := call(func() swig.WrappedWireServer { return b.inner.GetWire(name) })
	if err != nil {
		return nil, err
	}
	w := &Wire[T]{name: name, codec: codec, server: s}
	impl := &wireServerConnect[T]{w: w}
	d := swig.NewDirectorWrappedWireServerConnectDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedWireServerConnectDirector(d) })
	err = callVoid(func() {
		s.SetWrappedWireConnectCallback(d, id)
		pi := &wirePeek[T]{w: w, in: true}
		dpi := swig.NewDirectorWrappedWireServerPeekValueDirector(pi)
		s.SetPeekInValueCallback(dpi, heapAdd(pi, func() { swig.DeleteDirectorWrappedWireServerPeekValueDirector(dpi) }))
		po := &wirePeek[T]{w: w}
		dpo := swig.NewDirectorWrappedWireServerPeekValueDirector(po)
		s.SetPeekOutValueCallback(dpo, heapAdd(po, func() { swig.DeleteDirectorWrappedWireServerPeekValueDirector(dpo) }))
		pk := &wirePoke[T]{w: w}
		dpk := swig.NewDirectorWrappedWireServerPokeValueDirector(pk)
		s.SetPokeOutValueCallback(dpk, heapAdd(pk, func() { swig.DeleteDirectorWrappedWireServerPokeValueDirector(dpk) }))
	})
	if err != nil {
		return nil, err
	}
	return w, nil
}

// SetWireConnectCallback sets the function called when a client connects (service).
func (w *Wire[T]) SetWireConnectCallback(f func(*WireConnection[T])) {
	w.mu.Lock()
	w.connectCallback = f
	w.mu.Unlock()
}

// SetPeekInValueCallback sets the function answering client PeekInValue (service).
func (w *Wire[T]) SetPeekInValueCallback(f func(endpoint uint32) (T, TimeSpec, error)) {
	w.mu.Lock()
	w.peekIn = f
	w.mu.Unlock()
}

// SetPeekOutValueCallback sets the function answering client PeekOutValue (service).
func (w *Wire[T]) SetPeekOutValueCallback(f func(endpoint uint32) (T, TimeSpec, error)) {
	w.mu.Lock()
	w.peekOut = f
	w.mu.Unlock()
}

// SetPokeOutValueCallback sets the function handling client PokeOutValue (service).
func (w *Wire[T]) SetPokeOutValueCallback(f func(value T, ts TimeSpec, endpoint uint32) error) {
	w.mu.Lock()
	w.pokeOut = f
	w.mu.Unlock()
}

// WireBroadcaster sends a value to all connected clients of a service wire.
type WireBroadcaster[T any] struct {
	native swig.WrappedWireBroadcaster
	codec  Codec[T]
}

// NewWireBroadcaster creates a broadcaster for a service wire.
func NewWireBroadcaster[T any](w *Wire[T]) (*WireBroadcaster[T], error) {
	if w == nil || w.server == nil {
		return nil, NewInvalidArgument("wire is not a service wire")
	}
	n, err := call(func() swig.WrappedWireBroadcaster {
		n := swig.NewWrappedWireBroadcaster()
		n.Init(w.server)
		return n
	})
	if err != nil {
		return nil, err
	}
	return &WireBroadcaster[T]{native: n, codec: w.codec}, nil
}

// SetOutValue sets the value sent to all connected clients.
func (b *WireBroadcaster[T]) SetOutValue(v T) error {
	e, err := b.codec.Pack("value", v)
	if err != nil {
		return err
	}
	var cerr error
	err = callVoid(func() {
		m, err := toNative(e)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteMessageElement(m)
		b.native.SetOutValue(m)
	})
	if err != nil {
		return err
	}
	return cerr
}

// ActiveWireConnectionCount returns the number of connected clients.
func (b *WireBroadcaster[T]) ActiveWireConnectionCount() int {
	return int(b.native.GetActiveWireConnectionCount())
}

// OutValueLifespan returns the lifespan of the out value.
func (b *WireBroadcaster[T]) OutValueLifespan() time.Duration {
	return lifespan(b.native.GetOutValueLifespan())
}

// SetOutValueLifespan sets the lifespan of the out value.
func (b *WireBroadcaster[T]) SetOutValueLifespan(t time.Duration) {
	b.native.SetOutValueLifespan(timeoutMs(t))
}

type wirePredicate struct{ f func(endpoint uint32) bool }

func (p *wirePredicate) Predicate(ep uint32) bool {
	defer recoverDirector()
	return p.f(ep)
}

// SetPredicate sets a function selecting the clients that receive values.
func (b *WireBroadcaster[T]) SetPredicate(f func(endpoint uint32) bool) error {
	impl := &wirePredicate{f: f}
	d := swig.NewDirectorWrappedWireBroadcasterPredicateDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedWireBroadcasterPredicateDirector(d) })
	return callVoid(func() { b.native.SetPredicateDirector(d, id) })
}

// WireUnicastReceiver receives the value written by the most recent client of a service wire.
type WireUnicastReceiver[T any] struct {
	native  swig.WrappedWireUnicastReceiver
	codec   Codec[T]
	changed Event[func(value T, ts TimeSpec, endpoint uint32)]
}

// NewWireUnicastReceiver creates a unicast receiver for a service wire.
func NewWireUnicastReceiver[T any](w *Wire[T]) (*WireUnicastReceiver[T], error) {
	if w == nil || w.server == nil {
		return nil, NewInvalidArgument("wire is not a service wire")
	}
	r := &WireUnicastReceiver[T]{codec: w.codec}
	n, err := call(func() swig.WrappedWireUnicastReceiver {
		n := swig.NewWrappedWireUnicastReceiver()
		n.Init(w.server)
		impl := &wirePoke[T]{w: w, f: func(v T, ts TimeSpec, ep uint32) error {
			for _, h := range r.changed.Handlers() {
				h(v, ts, ep)
			}
			return nil
		}}
		d := swig.NewDirectorWrappedWireServerPokeValueDirector(impl)
		n.AddInValueChangedListener(d, heapAdd(impl, func() { swig.DeleteDirectorWrappedWireServerPokeValueDirector(d) }))
		return n
	})
	if err != nil {
		return nil, err
	}
	r.native = n
	return r, nil
}

// InValue returns the current in value, its timestamp and the client endpoint.
func (r *WireUnicastReceiver[T]) InValue() (T, TimeSpec, uint32, error) {
	var out T
	var ts TimeSpec
	var ep uint32
	var cerr error
	err := callVoid(func() {
		t := swig.NewTimeSpec__SWIG_0()
		defer swig.DeleteTimeSpec(t)
		e := make([]uint, 1)
		m := r.native.GetInValue(t, e)
		if swig.Rrgo_error_pending() {
			return
		}
		ts, ep = timeSpecFromNative(t), uint32(e[0])
		if m == nil || m.Swigcptr() == 0 {
			cerr = NewValueNotSet("value not set")
			return
		}
		defer swig.DeleteMessageElement(m)
		el, err := fromNative(m)
		if err != nil {
			cerr = err
			return
		}
		out, cerr = r.codec.Unpack(el)
	})
	if err == nil {
		err = cerr
	}
	return out, ts, ep, err
}

// InValueChanged is fired when a client writes a new value.
func (r *WireUnicastReceiver[T]) InValueChanged() *Event[func(value T, ts TimeSpec, endpoint uint32)] {
	return &r.changed
}

// InValueLifespan returns the lifespan of the in value.
func (r *WireUnicastReceiver[T]) InValueLifespan() time.Duration {
	return lifespan(r.native.GetInValueLifespan())
}

// SetInValueLifespan sets the lifespan of the in value.
func (r *WireUnicastReceiver[T]) SetInValueLifespan(t time.Duration) {
	r.native.SetInValueLifespan(timeoutMs(t))
}

var (
	_ swig.WrappedPipeServerConnectDirectorMethods        = (*pipeServerConnect[int32])(nil)
	_ swig.WrappedPipeBroadcasterPredicateDirectorMethods = (*pipePredicate)(nil)
	_ swig.WrappedWireServerConnectDirectorMethods        = (*wireServerConnect[int32])(nil)
	_ swig.WrappedWireServerPeekValueDirectorMethods      = (*wirePeek[int32])(nil)
	_ swig.WrappedWireServerPokeValueDirectorMethods      = (*wirePoke[int32])(nil)
	_ swig.WrappedWireBroadcasterPredicateDirectorMethods = (*wirePredicate)(nil)
)
