package loopback

import (
	"testing"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
)

func TestTimer(t *testing.T) {
	n := rr.DefaultNode()
	if _, err := n.CreateTimer(0, false, func(rr.TimerEvent, error) {}); err == nil {
		t.Error("expected error for zero period")
	}
	ch := make(chan rr.TimerEvent, 100)
	tm, err := n.CreateTimer(20*time.Millisecond, false, func(ev rr.TimerEvent, err error) {
		if err != nil {
			t.Error(err)
		}
		select {
		case ch <- ev:
		default:
		}
	})
	if err != nil {
		t.Fatal(err)
	}
	defer tm.Close()
	if tm.Period() != 20*time.Millisecond || tm.IsRunning() {
		t.Errorf("period %v running %v", tm.Period(), tm.IsRunning())
	}
	if err := tm.Start(); err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 3; i++ {
		select {
		case ev := <-ch:
			if ev.Stopped || ev.CurrentReal.IsZero() {
				t.Errorf("event %+v", ev)
			}
		case <-time.After(5 * time.Second):
			t.Fatal("timer did not fire")
		}
	}
	if err := tm.Stop(); err != nil {
		t.Fatal(err)
	}
	if err := tm.Stop(); err == nil {
		t.Error("expected error stopping a stopped timer")
	}

	one := make(chan struct{}, 4)
	ot, err := n.CreateTimer(10*time.Millisecond, true, func(ev rr.TimerEvent, err error) {
		if !ev.Stopped {
			one <- struct{}{}
		}
	})
	if err != nil {
		t.Fatal(err)
	}
	defer ot.Close()
	_ = ot.Start()
	select {
	case <-one:
	case <-time.After(5 * time.Second):
		t.Fatal("one-shot timer did not fire")
	}

	r, err := n.CreateRate(50)
	if err != nil {
		t.Fatal(err)
	}
	defer r.Close()
	start := time.Now()
	for i := 0; i < 5; i++ {
		if err := r.Sleep(); err != nil {
			t.Fatal(err)
		}
	}
	if d := time.Since(start); d < 60*time.Millisecond || d > 2*time.Second {
		t.Errorf("5 rate sleeps at 50 Hz took %v", d)
	}
}
