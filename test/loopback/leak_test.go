package loopback

import (
	"fmt"
	"runtime"
	"testing"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts1 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice1"
)

// settle runs the GC until the native handle and object heap counts stop
// changing (cleanups and director releases run asynchronously).
func settle() (handles, heap int) {
	handles, heap = rr.NativeHandleCount()
	for i := 0; i < 50; i++ {
		runtime.GC()
		time.Sleep(20 * time.Millisecond)
		h, o := rr.NativeHandleCount()
		if h == handles && o == heap && i > 3 {
			break
		}
		handles, heap = h, o
	}
	return
}

// TestNoLeaks checks that client objects are released after Close and
// DisconnectService: stubs, sub object stubs, pipe endpoints and wire
// connections (including ones closed only by the disconnect), so repeated
// connections do not accumulate native handles or object heap entries.
func TestNoLeaks(t *testing.T) {
	ctx := t.Context()
	cycle := func(closeExplicitly bool) {
		o, err := node.ConnectService(ctx, connectURL())
		if err != nil {
			t.Fatal(err)
		}
		r := o.(ts1.Testroot)
		if _, err := r.Func3(ctx, 1, 2); err != nil {
			t.Fatal(err)
		}
		if _, err := r.Get_o1(ctx); err != nil {
			t.Fatal(err)
		}
		ep, err := r.Get_p1().Connect(ctx, rr.AnyIndex)
		if err != nil {
			t.Fatal(err)
		}
		c, err := r.Get_w1().Connect(ctx)
		if err != nil {
			t.Fatal(err)
		}
		if closeExplicitly {
			_ = ep.Close()
			_ = c.Close()
		}
		if err := node.DisconnectService(o); err != nil {
			t.Fatal(err)
		}
	}
	cycle(true)
	cycle(false)
	h0, o0 := settle()
	const n = 50
	for i := 0; i < n; i++ {
		cycle(i%2 == 0)
	}
	h1, o1 := settle()
	t.Logf("after %d connections: native handles %d -> %d, object heap %d -> %d", n, h0, h1, o0, o1)
	// a few may still be in flight; growth per connection would be ~10
	if h1-h0 > 5 || o1-o0 > 5 {
		t.Errorf("leak: native handles %d -> %d, object heap %d -> %d", h0, h1, o0, o1)
	}
}

func connectURL() string { return fmt.Sprintf("rr+tcp://localhost:%d?service=go_test", port) }
