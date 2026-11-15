// Command client is the Go version of the Reynard the Robot client example.
// Start the service first (../service or the Reynard GUI).
package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"math"
	"os"
	"os/signal"
	"time"

	reynard "github.com/robotraconteur/robotraconteur_go/examples/reynard_the_robot/gen/experimental__reynard_the_robot"
	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
)

func deg(d float64) float64 { return d * math.Pi / 180 }

func main() {
	url := flag.String("url", "rr+tcp://localhost:29200?service=reynard", "service URL")
	flag.Parse()

	// Initialize the client node
	setup, err := rr.ClientNodeSetup(rr.WithServiceTypes(reynard.Factory))
	if err != nil {
		fmt.Fprintln(os.Stderr, "node setup:", err)
		os.Exit(1)
	}
	defer setup.Close()

	// Requests are made with a context: Ctrl-C cancels a pending request.
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	if err := run(ctx, setup.Node, *url, os.Stdout); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(ctx context.Context, node *rr.Node, url string, out io.Writer) error {
	// Connect to the Reynard service using a URL
	obj, err := node.ConnectService(ctx, url)
	if err != nil {
		return err
	}
	defer node.DisconnectService(obj)
	c := obj.(reynard.Reynard)

	// Connect a callback function to listen for new messages
	c.Get_new_message().Connect(func(msg string) { fmt.Fprintln(out, msg) })

	// Read the current state using a wire "peek". Can also "connect" to
	// receive streaming updates.
	state, _, err := c.Get_state().PeekInValue(ctx)
	if err != nil {
		return err
	}
	fmt.Fprintln(out, state.Robot_position)
	fmt.Fprintln(out, state.Arm_position)

	// Teleport the robot
	if err := c.Teleport(ctx, 0.1, -0.2); err != nil {
		return err
	}

	// Drive the robot with no timeout
	if err := c.Drive_robot(ctx, 0.5, -0.2, -1, false); err != nil {
		return err
	}

	// Wait for one second
	time.Sleep(time.Second)

	// Stop the robot
	if err := c.Drive_robot(ctx, 0, 0, -1, false); err != nil {
		return err
	}

	// Set the arm position
	if err := c.Setf_arm_position(ctx, deg(100), deg(-30), deg(-70)); err != nil {
		return err
	}

	// Drive the arm using timeout and wait
	if err := c.Drive_arm(ctx, deg(10), deg(-30), deg(-15), 1.5, true); err != nil {
		return err
	}

	// Set the color to red
	if err := c.Set_color(ctx, []float64{1.0, 0.0, 0.0}); err != nil {
		return err
	}

	// Read the color
	color, err := c.Get_color(ctx)
	if err != nil {
		return err
	}
	fmt.Fprintln(out, color)

	time.Sleep(time.Second)

	// Reset the color
	if err := c.Set_color(ctx, []float64{0.929, 0.49, 0.192}); err != nil {
		return err
	}

	// Errors from the service are returned as Go errors
	if err := c.Set_color(ctx, []float64{2, 0, 0}); err != nil {
		fmt.Fprintln(out, "expected error:", err)
	}

	// Say hello
	if err := c.Say(ctx, "Hello, World From Go!"); err != nil {
		return err
	}
	// Events are asynchronous; give the message a moment to arrive
	time.Sleep(100 * time.Millisecond)
	return nil
}
