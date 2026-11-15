package robotraconteur

import (
	"sync"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// Wire is a Robot Raconteur wire member carrying values of type T.
//
// On a client, use Connect for a streaming connection, or PeekInValue,
// PeekOutValue and PokeOutValue for single transactions. On a service, the
// skeleton creates the Wire and passes it to the object (SetX method); set a
// WireConnectCallback, or use a WireBroadcaster / WireUnicastReceiver.
type Wire[T any] struct {
	name   string
	codec  Codec[T]
	client swig.WrappedWireClient
	server swig.WrappedWireServer

	mu              sync.Mutex
	connectCallback func(*WireConnection[T])
	peekIn          func(endpoint uint32) (T, TimeSpec, error)
	peekOut         func(endpoint uint32) (T, TimeSpec, error)
	pokeOut         func(value T, ts TimeSpec, endpoint uint32) error
}

// NewWireClient creates the client side of a wire member. Used by generated stubs.
func NewWireClient[T any](b *StubBase, name string, codec Codec[T]) (*Wire[T], error) {
	c, err := call(func() swig.WrappedWireClient { return b.inner.GetWire(name) })
	if err != nil {
		return nil, err
	}
	return &Wire[T]{name: name, codec: codec, client: c}, nil
}

// MemberName returns the member name of the wire.
func (w *Wire[T]) MemberName() string { return w.name }

// Direction returns the direction of the wire.
func (w *Wire[T]) Direction() MemberDirection {
	if w.client != nil {
		return MemberDirection(w.client.Direction())
	}
	if w.server != nil {
		return MemberDirection(w.server.Direction())
	}
	return DirectionBoth
}

// Connect connects the wire (client).
func (w *Wire[T]) Connect() (*WireConnection[T], error) {
	if w.client == nil {
		return nil, NewInvalidOperation("wire is not a client wire")
	}
	c, err := call(func() swig.WrappedWireConnection { return w.client.Connect() })
	if err != nil {
		return nil, err
	}
	return newWireConnection(c, w.codec)
}

// AsyncConnect connects the wire asynchronously (client).
func (w *Wire[T]) AsyncConnect(timeout time.Duration, handler func(*WireConnection[T], error)) {
	if w.client == nil {
		go handler(nil, NewInvalidOperation("wire is not a client wire"))
		return
	}
	impl := &asyncWireConnection[T]{codec: w.codec, handler: handler}
	d := swig.NewDirectorAsyncWireConnectionReturnDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorAsyncWireConnectionReturnDirector(d) })
	if err := callVoid(func() { w.client.AsyncConnect(timeoutMs(timeout), d, id) }); err != nil {
		heapRemove(id)
		go handler(nil, err)
	}
}

type asyncWireConnection[T any] struct {
	codec   Codec[T]
	handler func(*WireConnection[T], error)
}

func (a *asyncWireConnection[T]) Handler(c swig.WrappedWireConnection, info swig.HandlerErrorInfo) {
	err := errorFromInfo(info)
	if err != nil {
		if c != nil && c.Swigcptr() != 0 {
			swig.DeleteWrappedWireConnection(c)
		}
		go a.handler(nil, err)
		return
	}
	wc, err := newWireConnection(c, a.codec)
	go a.handler(wc, err)
}

func (w *Wire[T]) unpack(m swig.MessageElement) (T, error) {
	var zero T
	if m == nil || m.Swigcptr() == 0 {
		return zero, NewValueNotSet("value not set")
	}
	defer swig.DeleteMessageElement(m)
	e, err := fromNative(m)
	if err != nil {
		return zero, err
	}
	return w.codec.Unpack(e)
}

func (w *Wire[T]) peek(f func(swig.TimeSpec) swig.MessageElement) (T, TimeSpec, error) {
	var out T
	var ts TimeSpec
	var cerr error
	err := callVoid(func() {
		t := swig.NewTimeSpec__SWIG_0()
		defer swig.DeleteTimeSpec(t)
		m := f(t)
		if swig.Rrgo_error_pending() {
			return
		}
		ts = timeSpecFromNative(t)
		out, cerr = w.unpack(m)
	})
	if err != nil {
		var zero T
		return zero, TimeSpec{}, err
	}
	return out, ts, cerr
}

// PeekInValue reads the current in value without connecting (client).
func (w *Wire[T]) PeekInValue() (T, TimeSpec, error) {
	if w.client == nil {
		var zero T
		return zero, TimeSpec{}, NewInvalidOperation("wire is not a client wire")
	}
	return w.peek(func(t swig.TimeSpec) swig.MessageElement { return w.client.PeekInValue(t) })
}

// PeekOutValue reads the current out value without connecting (client).
func (w *Wire[T]) PeekOutValue() (T, TimeSpec, error) {
	if w.client == nil {
		var zero T
		return zero, TimeSpec{}, NewInvalidOperation("wire is not a client wire")
	}
	return w.peek(func(t swig.TimeSpec) swig.MessageElement { return w.client.PeekOutValue(t) })
}

// PokeOutValue writes the out value without connecting (client).
func (w *Wire[T]) PokeOutValue(v T) error {
	if w.client == nil {
		return NewInvalidOperation("wire is not a client wire")
	}
	e, err := w.codec.Pack("value", v)
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
		w.client.PokeOutValue(m)
	})
	if err != nil {
		return err
	}
	return cerr
}

// WireConnection is a connected wire.
type WireConnection[T any] struct {
	native swig.WrappedWireConnection
	codec  Codec[T]

	valueChanged Event[func(c *WireConnection[T], value T, ts TimeSpec)]
	mu           sync.Mutex
	closed       func(*WireConnection[T])
}

type wireConnectionDirector[T any] struct{ c *WireConnection[T] }

func (d *wireConnectionDirector[T]) WireValueChanged(value swig.MessageElement, ts swig.TimeSpec) {
	defer recoverDirector()
	if value != nil && value.Swigcptr() != 0 {
		defer swig.DeleteMessageElement(value)
	}
	hs := d.c.valueChanged.Handlers()
	if len(hs) == 0 {
		return
	}
	e, err := fromNative(value)
	if err != nil {
		return
	}
	v, err := d.c.codec.Unpack(e)
	if err != nil {
		return
	}
	t := timeSpecFromNative(ts)
	for _, h := range hs {
		h(d.c, v, t)
	}
}

func (d *wireConnectionDirector[T]) WireConnectionClosedCallback() {
	defer recoverDirector()
	d.c.mu.Lock()
	f := d.c.closed
	d.c.mu.Unlock()
	if f != nil {
		f(d.c)
	}
}

func newWireConnection[T any](native swig.WrappedWireConnection, codec Codec[T]) (*WireConnection[T], error) {
	if native == nil || native.Swigcptr() == 0 {
		return nil, NewNullValue("null wire connection")
	}
	c := &WireConnection[T]{native: native, codec: codec}
	impl := &wireConnectionDirector[T]{c: c}
	d := swig.NewDirectorWrappedWireConnectionDirector(impl)
	id := heapAdd(c, func() { swig.DeleteDirectorWrappedWireConnectionDirector(d) })
	if err := callVoid(func() { native.SetRRDirector(d, id) }); err != nil {
		heapRemove(id)
		return nil, err
	}
	return c, nil
}

func (c *WireConnection[T]) get(f func() swig.MessageElement) (T, error) {
	var out T
	var cerr error
	err := callVoid(func() {
		m := f()
		if swig.Rrgo_error_pending() {
			return
		}
		var zero T
		if m == nil || m.Swigcptr() == 0 {
			out, cerr = zero, NewValueNotSet("value not set")
			return
		}
		defer swig.DeleteMessageElement(m)
		e, err := fromNative(m)
		if err != nil {
			cerr = err
			return
		}
		out, cerr = c.codec.Unpack(e)
	})
	if err != nil {
		var zero T
		return zero, err
	}
	return out, cerr
}

// InValue returns the current in value (ValueNotSet error if not valid).
func (c *WireConnection[T]) InValue() (T, error) {
	return c.get(func() swig.MessageElement { return c.native.GetInValue() })
}

// OutValue returns the current out value.
func (c *WireConnection[T]) OutValue() (T, error) {
	return c.get(func() swig.MessageElement { return c.native.GetOutValue() })
}

// SetOutValue sets the out value, sending it to the other end.
func (c *WireConnection[T]) SetOutValue(v T) error {
	e, err := c.codec.Pack("value", v)
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
		c.native.SetOutValue(m)
	})
	if err != nil {
		return err
	}
	return cerr
}

func (c *WireConnection[T]) tryGet(f func() swig.TryGetValueResult) (v T, ts TimeSpec, ok bool, err error) {
	var cerr error
	err = callVoid(func() {
		r := f()
		if r == nil || r.Swigcptr() == 0 {
			return
		}
		defer swig.DeleteTryGetValueResult(r)
		if !r.GetRes() {
			return
		}
		ok = true
		ts = timeSpecFromNative(r.GetTs())
		m := r.GetValue()
		if m == nil || m.Swigcptr() == 0 {
			return
		}
		defer swig.DeleteMessageElement(m)
		e, err := fromNative(m)
		if err != nil {
			cerr = err
			return
		}
		v, cerr = c.codec.Unpack(e)
	})
	if err == nil {
		err = cerr
	}
	return
}

// TryGetInValue returns the in value if valid.
func (c *WireConnection[T]) TryGetInValue() (T, TimeSpec, bool, error) {
	return c.tryGet(func() swig.TryGetValueResult { return c.native.TryGetInValue() })
}

// TryGetOutValue returns the out value if valid.
func (c *WireConnection[T]) TryGetOutValue() (T, TimeSpec, bool, error) {
	return c.tryGet(func() swig.TryGetValueResult { return c.native.TryGetOutValue() })
}

// InValueValid reports whether the in value is valid.
func (c *WireConnection[T]) InValueValid() bool { return c.native.GetInValueValid() }

// OutValueValid reports whether the out value is valid.
func (c *WireConnection[T]) OutValueValid() bool { return c.native.GetOutValueValid() }

// WaitInValueValid waits for the in value to become valid.
func (c *WireConnection[T]) WaitInValueValid(timeout time.Duration) (bool, error) {
	t := timeoutMs(timeout)
	return call(func() bool { return swig.SwigcptrWrappedWireConnection(c.native.Swigcptr()).WaitInValueValid__SWIG_0(t) })
}

// WaitOutValueValid waits for the out value to become valid.
func (c *WireConnection[T]) WaitOutValueValid(timeout time.Duration) (bool, error) {
	t := timeoutMs(timeout)
	return call(func() bool { return swig.SwigcptrWrappedWireConnection(c.native.Swigcptr()).WaitOutValueValid__SWIG_0(t) })
}

// LastValueReceivedTime returns the timestamp of the last received value.
func (c *WireConnection[T]) LastValueReceivedTime() TimeSpec {
	t := c.native.GetLastValueReceivedTime()
	defer swig.DeleteTimeSpec(t)
	return timeSpecFromNative(t)
}

// LastValueSentTime returns the timestamp of the last sent value.
func (c *WireConnection[T]) LastValueSentTime() TimeSpec {
	t := c.native.GetLastValueSentTime()
	defer swig.DeleteTimeSpec(t)
	return timeSpecFromNative(t)
}

// Endpoint returns the client endpoint id.
func (c *WireConnection[T]) Endpoint() uint32 { return c.native.GetEndpoint() }

// IgnoreInValue reports whether received values are dropped.
func (c *WireConnection[T]) IgnoreInValue() bool { return c.native.GetIgnoreInValue() }

// SetIgnoreInValue sets whether received values are dropped.
func (c *WireConnection[T]) SetIgnoreInValue(v bool) { c.native.SetIgnoreInValue(v) }

// InValueLifespan returns the lifespan of in values (negative = infinite).
func (c *WireConnection[T]) InValueLifespan() time.Duration {
	return lifespan(c.native.GetInValueLifespan())
}

// SetInValueLifespan sets the lifespan of in values.
func (c *WireConnection[T]) SetInValueLifespan(t time.Duration) { c.native.SetInValueLifespan(timeoutMs(t)) }

// OutValueLifespan returns the lifespan of out values.
func (c *WireConnection[T]) OutValueLifespan() time.Duration {
	return lifespan(c.native.GetOutValueLifespan())
}

// SetOutValueLifespan sets the lifespan of out values.
func (c *WireConnection[T]) SetOutValueLifespan(t time.Duration) { c.native.SetOutValueLifespan(timeoutMs(t)) }

func lifespan(ms int32) time.Duration {
	if ms < 0 {
		return TimeoutInfinite
	}
	return time.Duration(ms) * time.Millisecond
}

// WireValueChanged is fired when a new in value is received.
func (c *WireConnection[T]) WireValueChanged() *Event[func(c *WireConnection[T], value T, ts TimeSpec)] {
	return &c.valueChanged
}

// SetWireConnectionClosedCallback sets a function called when the connection closes.
func (c *WireConnection[T]) SetWireConnectionClosedCallback(f func(*WireConnection[T])) {
	c.mu.Lock()
	c.closed = f
	c.mu.Unlock()
}

// Close closes the connection.
func (c *WireConnection[T]) Close() error {
	return callVoid(func() { c.native.Close() })
}

// AsyncClose closes the connection asynchronously.
func (c *WireConnection[T]) AsyncClose(timeout time.Duration, handler func(error)) {
	d, id := newAsyncVoid(handler)
	if err := callVoid(func() { c.native.AsyncClose(timeoutMs(timeout), d, id) }); err != nil {
		heapRemove(id)
		go handler(err)
	}
}
