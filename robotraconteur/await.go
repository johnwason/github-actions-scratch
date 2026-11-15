package robotraconteur

import (
	"context"
	"math"
	"sync"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// Every network operation in this package is a native asynchronous
// operation awaited by the calling goroutine: the goroutine parks on a
// channel (no OS thread is held while the request is in flight) and the
// completion, run on a Robot Raconteur thread pool thread, only sends the
// result. There is no separate asynchronous API; use goroutines for
// concurrency and ctx for deadlines and cancellation.

type awaitResult[T any] struct {
	v   T
	err error
}

// await starts a native asynchronous operation and waits for its completion
// or for ctx to be done. start receives the completion function, which must
// be called at most once (it never blocks). If start returns an error the
// completion must not be called. When ctx ends first the operation keeps
// running in the core (requests cannot be cancelled); a late successful
// result is passed to discard so resources (stubs, endpoints) are released.
func await[T any](ctx context.Context, start func(done func(T, error)) error, discard func(T)) (T, error) {
	var zero T
	if ctx == nil {
		ctx = context.Background()
	}
	if err := ctx.Err(); err != nil {
		return zero, err
	}
	ch := make(chan awaitResult[T], 1)
	var once sync.Once
	done := func(v T, err error) {
		once.Do(func() { ch <- awaitResult[T]{v, err} })
	}
	if err := start(done); err != nil {
		return zero, err
	}
	select {
	case r := <-ch:
		if r.err != nil {
			if cerr := ctx.Err(); cerr != nil {
				return zero, cerr
			}
		}
		return r.v, r.err
	case <-ctx.Done():
		go func() {
			r := <-ch
			if r.err == nil && discard != nil {
				discard(r.v)
			}
		}()
		return zero, ctx.Err()
	}
}

// timeoutMs converts the time left before the ctx deadline, bounded by def
// (a timeout; negative means infinite), to a native timeout in milliseconds.
func ctxTimeoutMs(ctx context.Context, def time.Duration) int32 {
	t := def
	if ctx != nil {
		if dl, ok := ctx.Deadline(); ok {
			r := time.Until(dl)
			if t < 0 || r < t {
				t = r
			}
		}
	}
	if t < 0 {
		return -1
	}
	ms := int64(math.Ceil(float64(t) / float64(time.Millisecond)))
	if ms < 1 {
		ms = 1
	}
	if ms > math.MaxInt32 {
		ms = math.MaxInt32
	}
	return int32(ms)
}

// requestTimeoutMs is the native timeout for a request: the node request
// timeout, shortened by the ctx deadline.
func (n *Node) requestTimeoutMs(ctx context.Context) int32 {
	return ctxTimeoutMs(ctx, n.RequestTimeout())
}

// startNative runs f, which prepares arguments and calls a native function
// that starts an asynchronous operation, on a locked OS thread. It returns
// f's error or the native error reported synchronously, if any.
func startNative(f func() error) error {
	var ferr error
	err := callVoid(func() { ferr = f() })
	if ferr != nil {
		return ferr
	}
	return err
}

// signal lets goroutines wait for state changed by native callbacks without
// holding an OS thread. Waiters take the channel from wait before checking
// their condition, then select on it; notify wakes all current waiters.
type signal struct {
	mu sync.Mutex
	ch chan struct{}
}

func (s *signal) wait() <-chan struct{} {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.ch == nil {
		s.ch = make(chan struct{})
	}
	return s.ch
}

func (s *signal) notify() {
	s.mu.Lock()
	if s.ch != nil {
		close(s.ch)
		s.ch = nil
	}
	s.mu.Unlock()
}

// waitFor waits until cond reports done (returning its result) or ctx ends.
func waitFor[T any](ctx context.Context, s *signal, cond func() (T, bool, error)) (T, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	for {
		c := s.wait()
		v, ok, err := cond()
		if err != nil || ok {
			return v, err
		}
		select {
		case <-c:
		case <-ctx.Done():
			var zero T
			return zero, ctx.Err()
		}
	}
}

// awaitVoid awaits an operation completing through an AsyncVoidReturnDirector,
// bounded by the default node's request timeout and ctx.
func awaitVoid(ctx context.Context, start func(t int32, d swig.AsyncVoidReturnDirector, id int32)) error {
	_, err := await(ctx, func(done func(struct{}, error)) error {
		d, id := newAsyncVoid(func(err error) { done(struct{}{}, err) })
		t := DefaultNode().requestTimeoutMs(ctx)
		err := startNative(func() error { start(t, d, id); return nil })
		if err != nil {
			heapRemove(id)
		}
		return err
	}, nil)
	return err
}
