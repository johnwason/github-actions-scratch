package main

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"strings"
	"testing"

	reynard "github.com/robotraconteur/robotraconteur_go/examples/reynard_the_robot/gen/experimental__reynard_the_robot"
	"github.com/robotraconteur/robotraconteur_go/examples/reynard_the_robot/sim"
	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
)

const testPort = 29201

var node *rr.Node

// TestMain runs the simulated service and the client in one process over
// TCP loopback.
func TestMain(m *testing.M) {
	setup, err := rr.ServerNodeSetup("reynard_example_test", testPort,
		rr.WithServiceTypes(reynard.Factory),
		rr.WithFlags(rr.NodeSetupEnableTcpTransport|rr.NodeSetupTcpTransportStartServer|rr.NodeSetupTcpTransportListenLocalhost))
	if err != nil {
		fmt.Println("node setup:", err)
		os.Exit(1)
	}
	robot := sim.New()
	if _, err := setup.Node.RegisterService("reynard", "experimental.reynard_the_robot.Reynard", robot); err != nil {
		fmt.Println("register:", err)
		os.Exit(1)
	}
	node = setup.Node
	code := m.Run()
	robot.Close()
	setup.Close()
	os.Exit(code)
}

func TestClient(t *testing.T) {
	ctx := t.Context()
	var out bytes.Buffer
	url := fmt.Sprintf("rr+tcp://localhost:%d?service=reynard", testPort)
	if err := run(ctx, node, url, &out); err != nil {
		t.Fatal(err)
	}
	s := out.String()
	for _, want := range []string{"[1 0 0]", "expected error:", "Hello, World From Go!"} {
		if !strings.Contains(s, want) {
			t.Errorf("output missing %q:\n%s", want, s)
		}
	}

	obj, err := node.ConnectService(ctx, url)
	if err != nil {
		t.Fatal(err)
	}
	defer node.DisconnectService(obj)
	c := obj.(reynard.Reynard)
	pos, err := c.Get_robot_position(ctx)
	// teleported to (0.1,-0.2), then drove at (0.5,-0.2) for about 1 s
	if err != nil || len(pos) != 2 || pos[0] < 0.4 || pos[0] > 0.8 || pos[1] > -0.3 {
		t.Errorf("robot position %v %v", pos, err)
	}
	q, err := c.Getf_arm_position(ctx)
	// drove the arm at (10,-30,-15) deg/s for 1.5 s from (100,-30,-70) deg
	if err != nil || len(q) != 3 || q[0] < deg(110) || q[0] > deg(120) {
		t.Errorf("arm position %v %v", q, err)
	}
	err = c.Drive_robot(ctx, 1, 0, -1, true)
	if !errors.Is(err, rr.ErrInvalidArgument) {
		t.Errorf("expected InvalidArgument, got %v", err)
	}
}
