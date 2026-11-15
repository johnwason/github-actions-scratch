package robotraconteur

import (
	"errors"
	"sync"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// Generators (function returning or taking a {generator}). The numbering
// follows the C# and Java bindings:
//
//	Generator1[R, P]  R{generator} f(P{generator} p)  Next(P) (R, error)
//	Generator2[R]     R{generator} f()                Next() (R, error)
//	Generator3[P]     void f(P{generator} p)          Next(P) error
//
// Next returns an error matching ErrStopIteration when the generator is
// exhausted. Service objects return generators from generator functions;
// NewSliceGenerator2 and GeneratorFunc* helpers create simple ones.

// Generator1 is a generator with a return value and a parameter.
type Generator1[R, P any] interface {
	Next(param P) (R, error)
	Abort() error
	Close() error
}

// Generator2 is a generator with a return value.
type Generator2[R any] interface {
	Next() (R, error)
	Abort() error
	Close() error
}

// Generator3 is a generator with a parameter.
type Generator3[P any] interface {
	Next(param P) error
	Abort() error
	Close() error
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

func (g *GeneratorClient[R, P]) next(param P, hasParam bool) (R, error) {
	var zero R
	var pe *Element
	if hasParam {
		e, err := g.param.Pack("parameter", param)
		if err != nil {
			return zero, err
		}
		pe = e
	}
	var out R
	var cerr error
	err := callVoid(func() {
		var pm swig.MessageElement
		if pe != nil {
			m, err := toNative(pe)
			if err != nil {
				cerr = err
				return
			}
			defer swig.DeleteMessageElement(m)
			pm = m
		}
		r := g.native.Next(pm)
		if swig.Rrgo_error_pending() {
			return
		}
		if r != nil && r.Swigcptr() != 0 {
			defer swig.DeleteMessageElement(r)
		}
		if g.ret == nil {
			return
		}
		e, err := fromNative(r)
		if err != nil {
			cerr = err
			return
		}
		out, cerr = UnpackReturn(e, *g.ret)
	})
	if err != nil {
		return zero, err
	}
	return out, cerr
}

// Abort aborts the generator (the service sees an OperationAborted error).
func (g *GeneratorClient[R, P]) Abort() error { return callVoid(func() { g.native.Abort() }) }

// Close closes the generator.
func (g *GeneratorClient[R, P]) Close() error { return callVoid(func() { g.native.Close() }) }

// AsyncAbort aborts the generator asynchronously.
func (g *GeneratorClient[R, P]) AsyncAbort(timeout time.Duration, handler func(error)) {
	d, id := newAsyncVoid(handler)
	if err := callVoid(func() { g.native.AsyncAbort(timeoutMs(timeout), d, id) }); err != nil {
		heapRemove(id)
		go handler(err)
	}
}

// AsyncClose closes the generator asynchronously.
func (g *GeneratorClient[R, P]) AsyncClose(timeout time.Duration, handler func(error)) {
	d, id := newAsyncVoid(handler)
	if err := callVoid(func() { g.native.AsyncClose(timeoutMs(timeout), d, id) }); err != nil {
		heapRemove(id)
		go handler(err)
	}
}

// Generator1Client is the client side of a Generator1.
type Generator1Client[R, P any] struct{ GeneratorClient[R, P] }

// Next sends param and returns the next value.
func (g *Generator1Client[R, P]) Next(param P) (R, error) { return g.next(param, true) }

// Generator2Client is the client side of a Generator2.
type Generator2Client[R any] struct{ GeneratorClient[R, struct{}] }

// Next returns the next value.
func (g *Generator2Client[R]) Next() (R, error) { return g.next(struct{}{}, false) }

// NextAll reads all remaining values.
func (g *Generator2Client[R]) NextAll() ([]R, error) {
	var o []R
	for {
		v, err := g.Next()
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
func (g *Generator3Client[P]) Next(param P) error {
	_, err := g.next(param, true)
	return err
}

func generatorCall(b *StubBase, name string, args *ElementBuilder) (swig.WrappedGeneratorClient, error) {
	a, err := args.Result()
	if err != nil {
		return nil, err
	}
	var cerr error
	g, err := call(func() swig.WrappedGeneratorClient {
		vec, err := elementsToNative(a)
		if err != nil {
			cerr = err
			return nil
		}
		defer swig.DeleteVectorptr_messageelement(vec)
		return b.inner.GeneratorFunctionCall(name, vec)
	})
	if cerr != nil {
		return nil, cerr
	}
	return g, err
}

// CallGenerator1Function calls a generator function returning a Generator1.
func CallGenerator1Function[R, P any](b *StubBase, name string, args *ElementBuilder, ret Codec[R], param Codec[P]) (Generator1[R, P], error) {
	g, err := generatorCall(b, name, args)
	if err != nil {
		return nil, err
	}
	return &Generator1Client[R, P]{GeneratorClient[R, P]{native: g, ret: &ret, param: &param}}, nil
}

// CallGenerator2Function calls a generator function returning a Generator2.
func CallGenerator2Function[R any](b *StubBase, name string, args *ElementBuilder, ret Codec[R]) (Generator2[R], error) {
	g, err := generatorCall(b, name, args)
	if err != nil {
		return nil, err
	}
	return &Generator2Client[R]{GeneratorClient[R, struct{}]{native: g, ret: &ret}}, nil
}

// CallGenerator3Function calls a generator function returning a Generator3.
func CallGenerator3Function[P any](b *StubBase, name string, args *ElementBuilder, param Codec[P]) (Generator3[P], error) {
	g, err := generatorCall(b, name, args)
	if err != nil {
		return nil, err
	}
	return &Generator3Client[P]{GeneratorClient[struct{}, P]{native: g, param: &param}}, nil
}

// ---------------------------------------------------------------- service

type generatorServerDirector struct {
	next  func(param *Element) (*Element, error)
	abort func() error
	close func() error
}

func (d *generatorServerDirector) Next(m swig.MessageElement, async swig.WrappedServiceSkelAsyncAdapter) swig.MessageElement {
	defer recoverDirector()
	if async != nil && async.Swigcptr() != 0 {
		defer swig.DeleteWrappedServiceSkelAsyncAdapter(async)
	}
	var pe *Element
	if m != nil && m.Swigcptr() != 0 {
		defer swig.DeleteMessageElement(m)
		e, err := fromNative(m)
		if err != nil {
			setDirectorError(err)
			return nil
		}
		pe = e
	}
	r, err := d.next(pe)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	if r == nil {
		r = NewElement("return", []int32{0})
	}
	o, err := toNative(r)
	if err != nil {
		setDirectorError(err)
		return nil
	}
	return o
}

func (d *generatorServerDirector) Abort(async swig.WrappedServiceSkelAsyncAdapter) {
	defer recoverDirector()
	if async != nil && async.Swigcptr() != 0 {
		defer swig.DeleteWrappedServiceSkelAsyncAdapter(async)
	}
	if err := d.abort(); err != nil {
		setDirectorError(err)
	}
}

func (d *generatorServerDirector) Close(async swig.WrappedServiceSkelAsyncAdapter) {
	defer recoverDirector()
	if async != nil && async.Swigcptr() != 0 {
		defer swig.DeleteWrappedServiceSkelAsyncAdapter(async)
	}
	if err := d.close(); err != nil {
		setDirectorError(err)
	}
}

var _ swig.WrappedGeneratorServerDirectorMethods = (*generatorServerDirector)(nil)

func (b *SkelBase) registerGenerator(name string, impl *generatorServerDirector) (*Element, error) {
	d := swig.NewDirectorWrappedGeneratorServerDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorWrappedGeneratorServerDirector(d) })
	d.SetObjectheapid(id)
	idx, err := call(func() int32 { return b.inner.RegisterGeneratorServer(name, d) })
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
func RegisterGenerator1[R, P any](b *SkelBase, name string, g Generator1[R, P], ret Codec[R], param Codec[P]) (*Element, error) {
	if g == nil {
		return nil, NewNullValue("null generator")
	}
	return b.registerGenerator(name, &generatorServerDirector{
		next: func(pe *Element) (*Element, error) {
			p, err := unpackParam(pe, param)
			if err != nil {
				return nil, err
			}
			r, err := g.Next(p)
			if err != nil {
				return nil, err
			}
			return ret.Pack("return", r)
		},
		abort: g.Abort, close: g.Close,
	})
}

// RegisterGenerator2 registers a Generator2 returned by a service object.
func RegisterGenerator2[R any](b *SkelBase, name string, g Generator2[R], ret Codec[R]) (*Element, error) {
	if g == nil {
		return nil, NewNullValue("null generator")
	}
	return b.registerGenerator(name, &generatorServerDirector{
		next: func(*Element) (*Element, error) {
			r, err := g.Next()
			if err != nil {
				return nil, err
			}
			return ret.Pack("return", r)
		},
		abort: g.Abort, close: g.Close,
	})
}

// RegisterGenerator3 registers a Generator3 returned by a service object.
func RegisterGenerator3[P any](b *SkelBase, name string, g Generator3[P], param Codec[P]) (*Element, error) {
	if g == nil {
		return nil, NewNullValue("null generator")
	}
	return b.registerGenerator(name, &generatorServerDirector{
		next: func(pe *Element) (*Element, error) {
			p, err := unpackParam(pe, param)
			if err != nil {
				return nil, err
			}
			return nil, g.Next(p)
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
func NewSliceGenerator2[R any](values []R) *SliceGenerator[R] { return &SliceGenerator[R]{values: values} }

// Next returns the next value or ErrStopIteration.
func (g *SliceGenerator[R]) Next() (R, error) {
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
func (g *SliceGenerator[R]) Abort() error {
	g.mu.Lock()
	g.aborted = true
	g.mu.Unlock()
	return nil
}

// Close closes the generator.
func (g *SliceGenerator[R]) Close() error {
	g.mu.Lock()
	g.closed = true
	g.mu.Unlock()
	return nil
}

// Generator1Func adapts functions to a Generator1.
type Generator1Func[R, P any] struct {
	NextFunc  func(P) (R, error)
	AbortFunc func() error
	CloseFunc func() error
}

func (g *Generator1Func[R, P]) Next(p P) (R, error) { return g.NextFunc(p) }
func (g *Generator1Func[R, P]) Abort() error {
	if g.AbortFunc != nil {
		return g.AbortFunc()
	}
	return nil
}
func (g *Generator1Func[R, P]) Close() error {
	if g.CloseFunc != nil {
		return g.CloseFunc()
	}
	return nil
}

// Generator3Func adapts functions to a Generator3.
type Generator3Func[P any] struct {
	NextFunc  func(P) error
	AbortFunc func() error
	CloseFunc func() error
}

func (g *Generator3Func[P]) Next(p P) error { return g.NextFunc(p) }
func (g *Generator3Func[P]) Abort() error {
	if g.AbortFunc != nil {
		return g.AbortFunc()
	}
	return nil
}
func (g *Generator3Func[P]) Close() error {
	if g.CloseFunc != nil {
		return g.CloseFunc()
	}
	return nil
}
