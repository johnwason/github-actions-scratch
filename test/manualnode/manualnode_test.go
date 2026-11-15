// Package manualnode tests a node configured without NodeSetup: transports
// created with rr.New*Transport and registered with Node.RegisterTransport.
package manualnode

import (
	"fmt"
	"testing"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts1 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice1"
	_ "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice2"
)

const port = 22241

type root struct{ ts1.TestrootDefaultImpl }

func TestManualTransports(t *testing.T) {
	ctx := t.Context()
	n := rr.DefaultNode()
	if err := n.SetNodeName("go_manual_node"); err != nil {
		t.Fatal(err)
	}
	if err := n.RegisterServiceTypes(ts1.Factory); err != nil {
		t.Fatal(err)
	}
	defer n.Shutdown()

	tcp, err := rr.NewTCPTransport(n)
	if err != nil {
		t.Fatal(err)
	}
	if err := tcp.StartServer(port, true); err != nil {
		t.Fatal(err)
	}
	if err := n.RegisterTransport(tcp); err != nil {
		t.Fatal(err)
	}
	if err := n.RegisterTransport(nil); err == nil {
		t.Error("expected error registering a nil transport")
	}

	// The hardware transport is only constructed and configured: it is not
	// registered, so it never accesses devices.
	hw, err := rr.NewHardwareTransport(n)
	if err != nil {
		t.Fatal(err)
	}
	hw.SetDisableStringTable(true)
	if !hw.DisableStringTable() || hw.MaxMessageSize() <= 0 {
		t.Errorf("hardware transport settings: %v %v", hw.DisableStringTable(), hw.MaxMessageSize())
	}
	if err := hw.SetMaxMessageSize(1 << 20); err != nil || hw.MaxMessageSize() != 1<<20 {
		t.Errorf("SetMaxMessageSize: %v %v", err, hw.MaxMessageSize())
	}
	if err := hw.Close(); err != nil {
		t.Error(err)
	}

	r := &root{}
	if err := r.Set_d1(ctx, 2.5); err != nil {
		t.Fatal(err)
	}
	if _, err := n.RegisterService("manual", "com.robotraconteur.testing.TestService1.testroot", r); err != nil {
		t.Fatal(err)
	}
	o, err := n.ConnectService(ctx, fmt.Sprintf("rr+tcp://localhost:%d?service=manual", port))
	if err != nil {
		t.Fatal(err)
	}
	defer n.DisconnectService(o)
	v, err := o.(ts1.Testroot).Get_d1(ctx)
	if err != nil || v != 2.5 {
		t.Fatalf("d1 over manually registered TCP transport: %v %v", v, err)
	}
}
