package robotraconteur

import (
	"context"
	"errors"
	"sync"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// Generators (function returning or taking a {generator}). The numbering
// follows the C# and Java bindings:
//
//	Generator1[R, P]  R{generator} f(P{generator} p)  Next(ctx, P) (R, error)
//	Generator2[R]     R{generator} f()                Next(ctx) (R, error)
//	Generator3[P]     void f(P{generator} p)          Next(ctx, P) error
//
// Next returns an error matching ErrStopIteration when the generator is
// exhausted. Service objects return generators from generator functions;
// NewSliceGenerator2 and Generator*Func helpers create simple ones. On a
// service the ctx passed to the generator describes the request.

// Generator1 is a generator with a return value and a parameter.
type Generator1[R, P any] interface {
	Next(ctx context.Context, param P) (R, error)
	Abort(ctx context.Context) error
	Close(ctx context.Context) error
}

// Generator2 is a generator with a return value.
type Generator2[R any] interface {
	Next(ctx context.Context) (R, error)
	Abort(ctx context.Context) error
	Close(ctx context.Context) error
}

// Generator3 is a generator with a parameter.
type Generator3[P any] interface {
	Next(ctx context.Context, param P) error
	Abort(ctx context.Context) error
	Close(ctx context.Context) error
}

// IsStopIteration reports whether err signals the end of a generator.
func IsStopIteration(err error) bool { return errors.Is(err, ErrStopIteration) }

// ---------------------------------------------------------------- client

// GeneratorClient is the client side of a generator returned by a stub. It
// implements Generator1, Generator2 or Generator3 through the typed wrappers.
type GeneratorClient[R, P any] struct {
	native swig.WrappedGeneratorClient
	ret    *Codec[R]
	param  *Codec[P]
}

func (g *GeneratorClient[R, P]) next(ctx context.Context, param P, hasParam bool) (R, error) {
	var pe *Element
	if hasParam {
		e, err := g.param.Pack("parameter", param)
		if err != nil {
			var zero R
			return zero, err
		}
		pe = e
	}
	e, err := await(ctx, func(done func(*Element, error)) error {
		d, id := newAsyncRequest(done)
		t := DefaultNode().requestTimeoutMs(ctx)
		err := startNative(func() error {
			pm := swig.MessageElement(swig.SwigcptrMessageElement(0))
			if pe != nil {
				m, err := toNative(pe)
				if err != nil {
					return err
				}
				defer swig.DeleteMessageElement(m)
				pm = m
			}
			g.native.AsyncNext(pm, t, d, id)
			return nil
		})
		if err != nil {
			heapRemove(id)
		}
		return err
	}, nil)
	if err != nil || g.ret == nil {
		var zero R
		return zero, err
	}
	return UnpackReturn(e, *g.ret)
}

// Abort aborts the generator (the service sees an OperationAborted error).
func (g *GeneratorClient[R, P]) Abort(ctx context.Context) error {
	return awaitVoid(ctx, func(t int32, d swig.AsyncVoidReturnDirector, id int32) { g.native.AsyncAbort(t, d, id) })
}

// Close closes the generator.
func (g *GeneratorClient[R, P]) Close(ctx context.Context) error {
	return awaitVoid(ctx, func(t int32, d swig.AsyncVoidReturnDirector, id int32) { g.native.AsyncClose(t, d, id) })
}

// Generator1Client is the client side of a Generator1.
type Generator1Client[R, P any] struct{ GeneratorClient[R, P] }

// Next sends param and returns the next value.
func (g *Generator1Client[R, P]) Next(ctx context.Context, param P) (R, error) {
	return g.next(ctx, param, true)
}

// Generator2Client is the client side of a Generator2.
type Generator2Client[R any] struct{ GeneratorClient[R, struct{}] }

// Next returns the next value.
func (g *Generator2Client[R]) Next(ctx context.Context) (R, error) { return g.next(ctx, struct{}{}, false) }

// NextAll reads all remaining values.
func NextAll[R any](ctx context.Context, g Generator2[R]) ([]R, error) {
	var o []R
	for {
		v, err := g.Next(ctx)
		if IsStopIteration(err) {
			return o, nil
		}
		if err != nil {
			return o, err
		}
		o = append(o, v)
	}
}

// Generator3Client is the client side of a Generator3.
type Generator3Client[P any] struct{ GeneratorClient[struct{}, P] }

// Next sends param.
func (g *Generator3Client[P]) Next(ctx context.Context, param P) error {
	_, err := g.next(ctx, param, true)
	return err
}

type asyncGenerator struct {
	handler func(swig.WrappedGeneratorClient, error)
}

func (d *asyncGenerator) Handler(g swig.WrappedGeneratorClient, info swig.HandlerErrorInfo) {
	defer recoverDirector()
	err := errorFromInfo(info)
	if err == nil && (g == nil || g.Swigcptr() == 0) {
		err = NewNullValue("null generator")
	}
	if err != nil {
		if g != nil && g.Swigcptr() != 0 {
			swig.DeleteWrappedGeneratorClient(g)
		}
		g = nil
	}
	d.handler(g, err)
}

var _ swig.AsyncGeneratorClientReturnDirectorMethods = (*asyncGenerator)(nil)

func generatorCall(ctx context.Context, b *StubBase, name string, args *ElementBuilder) (swig.WrappedGeneratorClient, error) {
	a, err := args.Result()
	if err != nil {
		return nil, err
	}
	return await(ctx, func(done func(swig.WrappedGeneratorClient, error)) error {
		impl := &asyncGenerator{handler: done}
		d := swig.NewDirectorAsyncGeneratorClientReturnDirector(impl)
		id := heapAdd(impl, func() { swig.DeleteDirectorAsyncGeneratorClientReturnDirector(d) })
		t := b.node.requestTimeoutMs(ctx)
		err := startNative(func() error {
			vec, err := elementsToNative(a)
			if err != nil {
				return err
			}
			defer swig.DeleteVectorptr_messageelement(vec)
			b.inner.Async_GeneratorFunctionCall(name, vec, t, d, id)
			return nil
		})
		if err != nil {
			heapRemove(id)
		}
		return err
	}, func(g swig.WrappedGeneratorClient) { swig.DeleteWrappedGeneratorClient(g) })
}

// CallGenerator1Function calls a generator function returning a Generator1.
func CallGenerator1Function[R, P any](ctx context.Context, b *StubBase, name string, args *ElementBuilder, ret Codec[R], param Codec[P]) (Generator1[R, P], error) {
	g, err := generatorCall(ctx, b, name, args)
	if err != nil {
		return nil, err
	}
	return &Generator1Client[R, P]{GeneratorClient[R, P]{native: g, ret: &ret, param: &param}}, nil
}

// CallGenerator2Function calls a generator function returning a Generator2.
func CallGenerator2Function[R any](ctx context.Context, b *StubBase, name string, args *ElementBuilder, ret Codec[R]) (Generator2[R], error) {
	g, err := generatorCall(ctx, b, name, args)
	if err != nil {
		return nil, err
	}
	return &Generator2Client[R]{GeneratorClient[R, struct{}]{native: g, ret: &ret}}, nil
}

// CallGenerator3Function calls a generator function returning a Generator3.
func CallGenerator3Function[P any](ctx context.Context, b *StubBase, name string, args *ElementBuilder, param Codec[P]) (Generator3[P], error) {
	g, err := generatorCall(ctx, b, name, args)
	if err != nil {
		return nil, err
	}
	return &Generator3Client[P]{GeneratorClient[struct{}, P]{native: g, param: &param}}, nil
}

// ---------------------------------------------------------------- service

type generatorServerDirector struct {
	node  *Node
	next  func(ctx context.Context, param *Element) (*Element, error)
	abort func(ctx context.Context) error
	close func(ctx context.Context) error
}

// serve runs a generator operation like skelDirector.serve: on a new
// goroutine completed through the async adapter when one is provided.
func (d *generatorServerDirector) serve(async swig.WrappedServiceSkelAsyncAdapter, f func(ctx context.Context) (*Element, error)) swig.MessageElement {
	a := newAsyncAdapter(async)
	r := captureRequest(d.node, a)
	if a == nil {
		defer r.release()
		e, err := runRequest(r.context(), f)
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
		return m
	}
	a.makeAsync()
	go func() {
		defer r.release()
		a.end(runRequest(r.context(), f))
	}()
	return nil
}

func (d *generatorServerDirector) Next(m swig.MessageElement, async swig.WrappedServiceSkelAsyncAdapter) swig.MessageElement {
	defer recoverDirector()
	var pe *Element
	if m != nil && m.Swigcptr() != 0 {
		defer swig.DeleteMessageElement(m)
		e, err := fromNative(m)
		if err != nil {
			if async != nil && async.Swigcptr() != 0 {
				swig.DeleteWrappedServiceSkelAsyncAdapter(async)
			}
			setDirectorError(err)
			return nil
		}
		pe = e
	}
	return d.serve(async, func(ctx context.Context) (*Element, error) { return d.next(ctx, pe) })
}

func (d *generatorServerDirector) Abort(async swig.WrappedServiceSkelAsyncAdapter) {
	defer recoverDirector()
	d.serve(async, func(ctx context.Context) (*Element, error) { return nil, d.abort(ctx) })
}

func (d *generatorServerDirector) Close(async swig.WrappedServiceSkelAsyncAdapter) {
	defer recoverDirector()
	d.serve(async, func(ctx context.Context) (*Element, error) { return nil, d.close(ctx) })
}

var _ swig.WrappedGeneratorServerDirectorMethods = (*generatorServerDirector)(nil)

// registerGenerator registers a generator for the client of the request ctx
// belongs to (from any goroutine, using the endpoint captured with it).
func (b *SkelBase) registerGenerator(ctx context.Context, name string, impl *generatorServerDirector) (*Element, error) {
	impl.node = b.node
	r, _ := requestFrom(ctx)
	d := swig.NewDirectorWrappedGeneratorServerDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedGeneratorServerDirector(d) })
	d.SetObjectheapid(id)
	idx, err := call(func() int32 {
		s := swig.SwigcptrWrappedServiceSkel(b.inner.Swigcptr())
		if r != nil && r.adapter != nil && r.adapter.native != nil {
			return s.RegisterGeneratorServer__SWIG_1(name, d, r.adapter.native)
		}
		return s.RegisterGeneratorServer__SWIG_0(name, d)
	})
	if err != nil {
		return nil, err
	}
	return PackScalar("index", idx), nil
}

func unpackParam[P any](e *Element, c Codec[P]) (P, error) {
	var zero P
	if e == nil {
		return zero, NewInvalidArgument("missing generator parameter")
	}
	return c.Unpack(e)
}

// RegisterGenerator1 registers a Generator1 returned by a service object.
// Used by generated skeletons.
func RegisterGenerator1[R, P any](ctx context.Context, b *SkelBase, name string, g Generator1[R, P], ret Codec[R], param Codec[P]) (*Element, error) {
	if g == nil {
		return nil, NewNullValue("null generator")
	}
	return b.registerGenerator(ctx, name, &generatorServerDirector{
		next: func(ctx context.Context, pe *Element) (*Element, error) {
			p, err := unpackParam(pe, param)
			if err != nil {
				return nil, err
			}
			r, err := g.Next(ctx, p)
			if err != nil {
				return nil, err
			}
			return ret.Pack("return", r)
		},
		abort: g.Abort, close: g.Close,
	})
}

// RegisterGenerator2 registers a Generator2 returned by a service object.
func RegisterGenerator2[R any](ctx context.Context, b *SkelBase, name string, g Generator2[R], ret Codec[R]) (*Element, error) {
	if g == nil {
		return nil, NewNullValue("null generator")
	}
	return b.registerGenerator(ctx, name, &generatorServerDirector{
		next: func(ctx context.Context, _ *Element) (*Element, error) {
			r, err := g.Next(ctx)
			if err != nil {
				return nil, err
			}
			return ret.Pack("return", r)
		},
		abort: g.Abort, close: g.Close,
	})
}

// RegisterGenerator3 registers a Generator3 returned by a service object.
func RegisterGenerator3[P any](ctx context.Context, b *SkelBase, name string, g Generator3[P], param Codec[P]) (*Element, error) {
	if g == nil {
		return nil, NewNullValue("null generator")
	}
	return b.registerGenerator(ctx, name, &generatorServerDirector{
		next: func(ctx context.Context, pe *Element) (*Element, error) {
			p, err := unpackParam(pe, param)
			if err != nil {
				return nil, err
			}
			return nil, g.Next(ctx, p)
		},
		abort: g.Abort, close: g.Close,
	})
}

// ---------------------------------------------------------------- helpers

// SliceGenerator is a Generator2 returning the values of a slice.
type SliceGenerator[R any] struct {
	mu      sync.Mutex
	values  []R
	pos     int
	aborted bool
	closed  bool
}

// NewSliceGenerator2 returns a Generator2 producing values in order.
func NewSliceGenerator2[R any](values []R) *SliceGenerator[R] {
	return &SliceGenerator[R]{values: values}
}

// Next returns the next value or ErrStopIteration.
func (g *SliceGenerator[R]) Next(context.Context) (R, error) {
	g.mu.Lock()
	defer g.mu.Unlock()
	var zero R
	if g.aborted {
		return zero, NewOperationAborted("generator aborted")
	}
	if g.closed || g.pos >= len(g.values) {
		return zero, NewStopIteration("")
	}
	v := g.values[g.pos]
	g.pos++
	return v, nil
}

// Abort aborts the generator.
func (g *SliceGenerator[R]) Abort(context.Context) error {
	g.mu.Lock()
	g.aborted = true
	g.mu.Unlock()
	return nil
}

// Close closes the generator.
func (g *SliceGenerator[R]) Close(context.Context) error {
	g.mu.Lock()
	g.closed = true
	g.mu.Unlock()
	return nil
}

// Generator1Func adapts functions to a Generator1.
type Generator1Func[R, P any] struct {
	NextFunc  func(ctx context.Context, p P) (R, error)
	AbortFunc func(ctx context.Context) error
	CloseFunc func(ctx context.Context) error
}

func (g *Generator1Func[R, P]) Next(ctx context.Context, p P) (R, error) { return g.NextFunc(ctx, p) }
func (g *Generator1Func[R, P]) Abort(ctx context.Context) error {
	if g.AbortFunc != nil {
		return g.AbortFunc(ctx)
	}
	return nil
}
func (g *Generator1Func[R, P]) Close(ctx context.Context) error {
	if g.CloseFunc != nil {
		return g.CloseFunc(ctx)
	}
	return nil
}

// Generator3Func adapts functions to a Generator3.
type Generator3Func[P any] struct {
	NextFunc  func(ctx context.Context, p P) error
	AbortFunc func(ctx context.Context) error
	CloseFunc func(ctx context.Context) error
}

func (g *Generator3Func[P]) Next(ctx context.Context, p P) error { return g.NextFunc(ctx, p) }
func (g *Generator3Func[P]) Abort(ctx context.Context) error {
	if g.AbortFunc != nil {
		return g.AbortFunc(ctx)
	}
	return nil
}
func (g *Generator3Func[P]) Close(ctx context.Context) error {
	if g.CloseFunc != nil {
		return g.CloseFunc(ctx)
	}
	return nil
}
