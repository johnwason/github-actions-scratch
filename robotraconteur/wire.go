package robotraconteur

import (
	"context"
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
func (w *Wire[T]) Connect(ctx context.Context) (*WireConnection[T], error) {
	if w.client == nil {
		return nil, NewInvalidOperation("wire is not a client wire")
	}
	return await(ctx, func(done func(*WireConnection[T], error)) error {
		impl := &asyncWireConnection[T]{codec: w.codec, handler: done}
		d := swig.NewDirectorAsyncWireConnectionReturnDirector(impl)
		id := heapAdd(impl, func() { swig.DeleteDirectorAsyncWireConnectionReturnDirector(d) })
		t := DefaultNode().requestTimeoutMs(ctx)
		err := startNative(func() error { w.client.AsyncConnect(t, d, id); return nil })
		if err != nil {
			heapRemove(id)
		}
		return err
	}, func(c *WireConnection[T]) { _ = c.Close() })
}

type asyncWireConnection[T any] struct {
	codec   Codec[T]
	handler func(*WireConnection[T], error)
}

func (a *asyncWireConnection[T]) Handler(c swig.WrappedWireConnection, info swig.HandlerErrorInfo) {
	defer recoverDirector()
	err := errorFromInfo(info)
	if err != nil {
		if c != nil && c.Swigcptr() != 0 {
			swig.DeleteWrappedWireConnection(c)
		}
		a.handler(nil, err)
		return
	}
	a.handler(newWireConnection(c, a.codec))
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

type wirePeekValue[T any] struct {
	v  T
	ts TimeSpec
}

type asyncWirePeek[T any] struct {
	w       *Wire[T]
	handler func(wirePeekValue[T], error)
}

// Handler owns value (a Go handle); ts is owned by C++.
func (a *asyncWirePeek[T]) Handler(value swig.MessageElement, ts swig.TimeSpec, info swig.HandlerErrorInfo) {
	defer recoverDirector()
	if err := errorFromInfo(info); err != nil {
		if value != nil && value.Swigcptr() != 0 {
			swig.DeleteMessageElement(value)
		}
		a.handler(wirePeekValue[T]{}, err)
		return
	}
	v, err := a.w.unpack(value)
	a.handler(wirePeekValue[T]{v: v, ts: timeSpecFromNative(ts)}, err)
}

func (w *Wire[T]) peek(ctx context.Context, out bool) (T, TimeSpec, error) {
	if w.client == nil {
		var zero T
		return zero, TimeSpec{}, NewInvalidOperation("wire is not a client wire")
	}
	r, err := await(ctx, func(done func(wirePeekValue[T], error)) error {
		impl := &asyncWirePeek[T]{w: w, handler: done}
		d := swig.NewDirectorAsyncWirePeekReturnDirector(impl)
		id := heapAdd(impl, func() { swig.DeleteDirectorAsyncWirePeekReturnDirector(d) })
		t := DefaultNode().requestTimeoutMs(ctx)
		err := startNative(func() error {
			if out {
				w.client.AsyncPeekOutValue(t, d, id)
			} else {
				w.client.AsyncPeekInValue(t, d, id)
			}
			return nil
		})
		if err != nil {
			heapRemove(id)
		}
		return err
	}, nil)
	return r.v, r.ts, err
}

// PeekInValue reads the current in value without connecting (client).
func (w *Wire[T]) PeekInValue(ctx context.Context) (T, TimeSpec, error) { return w.peek(ctx, false) }

// PeekOutValue reads the current out value without connecting (client).
func (w *Wire[T]) PeekOutValue(ctx context.Context) (T, TimeSpec, error) { return w.peek(ctx, true) }

// PokeOutValue writes the out value without connecting (client).
func (w *Wire[T]) PokeOutValue(ctx context.Context, v T) error {
	if w.client == nil {
		return NewInvalidOperation("wire is not a client wire")
	}
	e, err := w.codec.Pack("value", v)
	if err != nil {
		return err
	}
	_, err = await(ctx, func(done func(struct{}, error)) error {
		d, id := newAsyncVoid(func(err error) { done(struct{}{}, err) })
		t := DefaultNode().requestTimeoutMs(ctx)
		err := startNative(func() error {
			m, err := toNative(e)
			if err != nil {
				return err
			}
			defer swig.DeleteMessageElement(m)
			w.client.AsyncPokeOutValue(m, t, d, id)
			return nil
		})
		if err != nil {
			heapRemove(id)
		}
		return err
	}, nil)
	return err
}

// WireConnection is a connected wire.
type WireConnection[T any] struct {
	native swig.WrappedWireConnection
	codec  Codec[T]

	valueChanged Event[func(c *WireConnection[T], value T, ts TimeSpec)]
	mu           sync.Mutex
	closed       func(*WireConnection[T])
	isClosed     bool
	changed      signal // value received, out value set or connection closed
}

type wireConnectionDirector[T any] struct{ c *WireConnection[T] }

func (d *wireConnectionDirector[T]) WireValueChanged(value swig.MessageElement, ts swig.TimeSpec) {
	defer recoverDirector()
	defer d.c.changed.notify()
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
	d.c.isClosed = true
	d.c.mu.Unlock()
	d.c.changed.notify()
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
	c.changed.notify()
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

func (c *WireConnection[T]) waitValid(ctx context.Context, valid func() bool) error {
	_, err := waitFor(ctx, &c.changed, func() (struct{}, bool, error) {
		if valid() {
			return struct{}{}, true, nil
		}
		c.mu.Lock()
		closed := c.isClosed
		c.mu.Unlock()
		if closed {
			return struct{}{}, false, NewConnectionError("wire connection closed")
		}
		return struct{}{}, false, nil
	})
	return err
}

// WaitInValueValid waits for the in value to become valid.
func (c *WireConnection[T]) WaitInValueValid(ctx context.Context) error {
	return c.waitValid(ctx, c.InValueValid)
}

// WaitOutValueValid waits for the out value to become valid.
func (c *WireConnection[T]) WaitOutValueValid(ctx context.Context) error {
	return c.waitValid(ctx, c.OutValueValid)
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
func (c *WireConnection[T]) SetInValueLifespan(t time.Duration) {
	c.native.SetInValueLifespan(timeoutMs(t))
}

// OutValueLifespan returns the lifespan of out values.
func (c *WireConnection[T]) OutValueLifespan() time.Duration {
	return lifespan(c.native.GetOutValueLifespan())
}

// SetOutValueLifespan sets the lifespan of out values.
func (c *WireConnection[T]) SetOutValueLifespan(t time.Duration) {
	c.native.SetOutValueLifespan(timeoutMs(t))
}

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

// Close closes the connection and waits for the close to complete (bounded
// by the request timeout).
func (c *WireConnection[T]) Close() error {
	return awaitVoid(context.Background(), func(t int32, d swig.AsyncVoidReturnDirector, id int32) {
		c.native.AsyncClose(t, d, id)
	})
}
