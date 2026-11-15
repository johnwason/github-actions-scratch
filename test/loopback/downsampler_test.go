package loopback

import (
	"testing"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
)

func TestBroadcastDownsampler(t *testing.T) {
	if _, err := rr.NewBroadcastDownsampler(nil, 1); err == nil {
		t.Error("expected error for nil context")
	}
	d, err := rr.NewBroadcastDownsampler(testCtx, 1)
	if err != nil {
		t.Fatal(err)
	}
	b, err := rr.NewWireBroadcaster(testRoot.bwire)
	if err != nil {
		t.Fatal(err)
	}
	if err := rr.AddWireBroadcasterToDownsampler(d, b); err != nil {
		t.Fatal(err)
	}
	d.SetClientDownsample(123456, 3)
	if v := d.ClientDownsample(123456); v != 3 {
		t.Errorf("client downsample %d", v)
	}

	r := connect(t)
	c, err := r.Broadcastwire().Connect()
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		d.BeginStep()
		err := b.SetOutValue(42)
		d.EndStep()
		if err != nil {
			t.Fatal(err)
		}
		if ok, _ := c.WaitInValueValid(100 * time.Millisecond); ok {
			v, err := c.InValue()
			if err != nil || v != 42 {
				t.Fatalf("in value %v %v", v, err)
			}
			return
		}
	}
	t.Fatal("broadcast value not received")
}
