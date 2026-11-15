// Command rrgo_test_server runs the Go port of the upstream test services
// (same service names and port as robotraconteur_test_server), so the
// upstream C++ client tests can run against a Go server:
//
//	ROBOTRACONTEUR_TEST_SERVER_URL=rr+tcp://localhost:22222 robotraconteur_test_service
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	"github.com/robotraconteur/robotraconteur_go/test/testservices"
)

func main() {
	port := flag.Int("port", 22222, "TCP port")
	flag.Parse()
	setup, err := rr.ServerNodeSetup("testprog", uint16(*port),
		rr.WithFlags(rr.NodeSetupEnableTcpTransport|rr.NodeSetupTcpTransportStartServer|rr.NodeSetupEnableIntraTransport|
			rr.NodeSetupIntraTransportStartServer|rr.NodeSetupEnableLocalTransport|rr.NodeSetupLocalTransportStartServer))
	if err != nil {
		fmt.Fprintln(os.Stderr, "node setup:", err)
		os.Exit(1)
	}
	defer setup.Close()
	roots, err := testservices.RegisterTestService1(setup.Node)
	if err != nil {
		fmt.Fprintln(os.Stderr, "register:", err)
		os.Exit(1)
	}
	ts3root, err := testservices.RegisterTestService3(setup.Node)
	if err != nil {
		fmt.Fprintln(os.Stderr, "register ts3:", err)
		os.Exit(1)
	}
	defer ts3root.Shutdown()
	if _, err := testservices.RegisterTestService5(setup.Node); err != nil {
		fmt.Fprintln(os.Stderr, "register ts5:", err)
		os.Exit(1)
	}
	defer func() {
		for _, r := range roots {
			r.Shutdown()
		}
	}()
	fmt.Println("Server started, press enter to quit")
	bufio.NewReader(os.Stdin).ReadString('\n')
}
