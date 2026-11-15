// Command service runs a simulated Reynard the Robot service.
//
// Connect with rr+tcp://localhost:29200?service=reynard (for example with
// the client in ../client).
package main

import (
	"flag"
	"fmt"
	"os"
	"os/signal"

	"github.com/robotraconteur/robotraconteur_go/examples/reynard_the_robot/gen/experimental__reynard_the_robot"
	"github.com/robotraconteur/robotraconteur_go/examples/reynard_the_robot/sim"
	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
)

func main() {
	port := flag.Int("port", 29200, "TCP port")
	flag.Parse()

	// Initialize the server node: TCP, local and intra transports
	setup, err := rr.ServerNodeSetup("experimental.reynard_the_robot", uint16(*port),
		rr.WithServiceTypes(experimental__reynard_the_robot.Factory))
	if err != nil {
		fmt.Fprintln(os.Stderr, "node setup:", err)
		os.Exit(1)
	}
	defer setup.Close()

	robot := sim.New()
	defer robot.Close()

	// Register the service object
	if _, err := setup.Node.RegisterService("reynard", "experimental.reynard_the_robot.Reynard", robot); err != nil {
		fmt.Fprintln(os.Stderr, "register service:", err)
		os.Exit(1)
	}

	fmt.Printf("Reynard the Robot service started on rr+tcp://localhost:%d?service=reynard\n", *port)
	fmt.Println("Press Ctrl-C to quit")
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt)
	<-sig
}
