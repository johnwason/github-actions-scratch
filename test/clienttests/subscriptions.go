package clienttests

import (
	"testing"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts1 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice1"
)

// RunSubscriptions tests service, wire and pipe subscriptions by URL and
// subscription/discovery by type (local transport) for the node nodeName.
func RunSubscriptions(t *testing.T, n *rr.Node, baseURL, nodeName string) {
	url := baseURL + "?service=RobotRaconteurTestService"
	t.Run("ByURL", func(t *testing.T) {
		sub, err := n.SubscribeService([]string{url})
		must(t, err)
		defer sub.Close()
		c, err := sub.DefaultClientWait(10 * time.Second)
		must(t, err)
		r, ok := c.(ts1.Testroot)
		if !ok {
			t.Fatalf("unexpected client type %T", c)
		}
		eq(t, "func3", get(t, "func3", func() (float64, error) { return r.Func3(1, 2) }), 3.0)
		clients, err := sub.ConnectedClients()
		must(t, err)
		if len(clients) != 1 {
			t.Errorf("expected 1 connected client, got %d", len(clients))
		}

		ws, err := rr.SubscribeWire[float64](sub, "broadcastwire", "")
		must(t, err)
		defer ws.Close()
		got := make(chan float64, 100)
		ws.WireValueChanged().Connect(func(_ *rr.WireSubscription[float64], v float64, _ rr.TimeSpec) {
			select {
			case got <- v:
			default:
			}
		})
		ok2, err := ws.WaitInValueValid(10 * time.Second)
		if err != nil || !ok2 {
			t.Fatalf("wire subscription value not valid: %v", err)
		}
		if _, _, err := ws.InValue(); err != nil {
			t.Errorf("wire subscription InValue: %v", err)
		}
		select {
		case <-got:
		case <-time.After(5 * time.Second):
			t.Error("no wire subscription value change")
		}

		ps, err := rr.SubscribePipe[float64](sub, "broadcastpipe", "", -1)
		must(t, err)
		defer ps.Close()
		v, ok3, err := ps.TryReceivePacketWait(10*time.Second, false)
		if err != nil || !ok3 {
			t.Fatalf("pipe subscription: no packet (%v)", err)
		}
		if v < 0 || v >= 100 {
			t.Errorf("unexpected broadcast packet %v", v)
		}
	})
	t.Run("ByType", func(t *testing.T) {
		filter := &rr.ServiceSubscriptionFilter{
			Predicate:        func(i rr.ServiceInfo2) bool { return i.NodeName == nodeName && i.Name == "RobotRaconteurTestService" },
			TransportSchemes: []string{"rr+local"},
		}
		info, err := n.SubscribeServiceInfo2([]string{"com.robotraconteur.testing.TestService1.testroot"}, filter)
		must(t, err)
		defer info.Close()
		sub, err := n.SubscribeServiceByType([]string{"com.robotraconteur.testing.TestService1.testroot"}, filter)
		must(t, err)
		defer sub.Close()
		c, err := sub.DefaultClientWait(20 * time.Second)
		must(t, err)
		if _, err := c.(ts1.Testroot).GetD1(); err != nil {
			t.Errorf("by type client: %v", err)
		}
		deadline := time.Now().Add(10 * time.Second)
		for {
			d, err := info.DetectedServiceInfo2()
			must(t, err)
			if len(d) > 0 {
				for _, s := range d {
					eq(t, "service info node", s.NodeName, nodeName)
				}
				break
			}
			if time.Now().After(deadline) {
				t.Error("ServiceInfo2 subscription did not detect the service")
				break
			}
			time.Sleep(100 * time.Millisecond)
		}
		found, err := n.FindServiceByType("com.robotraconteur.testing.TestService1.testroot", "rr+local")
		must(t, err)
		ok := false
		for _, f := range found {
			if f.NodeName == nodeName && f.Name == "RobotRaconteurTestService" {
				ok = true
				eq(t, "attributes", f.Attributes["test"], any("This is a test attribute"))
			}
		}
		if !ok {
			t.Errorf("FindServiceByType did not find %s", nodeName)
		}
	})
}
