package loopback

import (
	"runtime/pprof"
	"sync"
	"testing"
	"time"
)

// TestManyConcurrentCalls checks the threading model: requests are awaited
// on parked goroutines (no OS thread per pending call on the client) and
// service members run on their own goroutines (the core's 20 thread pool
// threads are not held while a member blocks). 400 calls that each block
// for 200 ms in the service must complete together in about 200 ms, without
// a thread per call.
func TestManyConcurrentCalls(t *testing.T) {
	ctx := t.Context()
	r := connect(t)
	if _, err := r.Func3(ctx, 1, 2); err != nil {
		t.Fatal(err)
	}
	threads := pprof.Lookup("threadcreate")
	before := threads.Count()
	const n = 400
	start := time.Now()
	var wg sync.WaitGroup
	errs := make(chan error, n)
	for i := 0; i < n; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			v, err := r.Func3(ctx, slowCall, 1)
			if err == nil && v != slowCall+1 {
				t.Errorf("func3 returned %v", v)
			}
			errs <- err
		}()
	}
	wg.Wait()
	elapsed := time.Since(start)
	close(errs)
	for err := range errs {
		if err != nil {
			t.Fatal(err)
		}
	}
	created := threads.Count() - before
	t.Logf("%d calls blocking 200 ms each: %v, %d OS threads created", n, elapsed, created)
	if elapsed > 3*time.Second {
		t.Errorf("calls were serialized: %v", elapsed)
	}
	if created > n/2 {
		t.Errorf("%d OS threads created for %d concurrent calls", created, n)
	}
}
