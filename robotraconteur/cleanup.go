package robotraconteur

import (
	"runtime"
	"sync"
	"sync/atomic"
)

// Native handles of long-lived wrapper objects (stubs, pipe endpoints, wire
// connections, subscriptions, ...) are freed by runtime cleanups once the Go
// wrapper becomes unreachable. Close methods only perform the native close;
// they never free the handle, so a wrapper used after Close fails with an
// error instead of touching freed memory.
//
// Wrappers with a director are also referenced by the object heap while the
// core holds the director. The core releases those directors when the
// object is closed or disconnected (stubs on disconnect, pipe endpoints and
// wire connections on any close, subscriptions on Close), which makes the
// wrapper collectable.
//
// The native delete runs on a release goroutine, not the runtime's cleanup
// goroutine: dropping the last reference runs C++ destructors, which may
// take locks or call back into Go.

var (
	releaseOnce  sync.Once
	releaseQueue chan func()
	liveHandles  atomic.Int64
)

func startReleaser() {
	releaseQueue = make(chan func(), 1024)
	go func() {
		for f := range releaseQueue {
			f()
		}
	}()
}

// addCleanup frees h with del once w becomes unreachable. h must not
// reference w (SWIG handles are plain pointer values, so they don't).
func addCleanup[W any, H any](w *W, h H, del func(H)) {
	releaseOnce.Do(startReleaser)
	liveHandles.Add(1)
	runtime.AddCleanup(w, func(h H) {
		f := func() {
			del(h)
			liveHandles.Add(-1)
		}
		select {
		case releaseQueue <- f:
		default:
			go f() // queue full: never block the runtime's cleanup goroutine
		}
	}, h)
}

// NativeHandleCount reports the number of native wrapper handles not yet
// released and the number of Go objects the core currently references
// (director objects in the object heap). It is meant for diagnostics and
// leak tests; call runtime.GC first to run pending cleanups.
func NativeHandleCount() (handles, heapObjects int) {
	return int(liveHandles.Load()), heapCount()
}
