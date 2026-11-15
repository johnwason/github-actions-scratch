// Package interop tests the Go bindings against the upstream C++ test
// server (robotraconteur_test_server) over the loopback interface.
package interop

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"testing"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	_ "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice1"
	_ "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice2"
	_ "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice3"
)

const serverPort = 22222

var serverURL = fmt.Sprintf("rr+tcp://localhost:%d?service=RobotRaconteurTestService", serverPort)
var server2URL = fmt.Sprintf("rr+tcp://localhost:%d?service=RobotRaconteurTestService2", serverPort)

func exeSuffix() string {
	if runtime.GOOS == "windows" {
		return ".exe"
	}
	return ""
}

func serverPath() string {
	if p := os.Getenv("RRGO_CPP_TEST_SERVER"); p != "" {
		return p
	}
	p, _ := filepath.Abs("../../build/rr/test/out_reldebug/bin/robotraconteur_test_server" + exeSuffix())
	return p
}

func TestMain(m *testing.M) {
	path := serverPath()
	if _, err := os.Stat(path); err != nil {
		fmt.Println("skipping interop tests: C++ test server not built (", path, ")")
		os.Exit(0)
	}
	// No discovery announce/listening, and TCP on localhost only: the tests must
	// not reach the network (CI runners are not isolated like the dev container).
	cmd := exec.Command(path, "--robotraconteur-discovery-listening-enable=false",
		"--robotraconteur-discovery-announce-enable=false", "--robotraconteur-tcp-listen-localhost=true")
	stdin, _ := cmd.StdinPipe()
	stdout, _ := cmd.StdoutPipe()
	cmd.Stderr = os.Stderr
	if err := cmd.Start(); err != nil {
		fmt.Println("could not start server:", err)
		os.Exit(1)
	}
	started := make(chan struct{})
	go func() {
		s := bufio.NewScanner(stdout)
		for s.Scan() {
			if s.Text() == "Server started, press enter to quit" {
				close(started)
			}
		}
	}()
	select {
	case <-started:
	case <-time.After(30 * time.Second):
		fmt.Println("server did not start")
		cmd.Process.Kill()
		os.Exit(1)
	}
	// wait for the port to accept connections
	for i := 0; i < 100; i++ {
		c, err := net.Dial("tcp", fmt.Sprintf("127.0.0.1:%d", serverPort))
		if err == nil {
			c.Close()
			break
		}
		time.Sleep(50 * time.Millisecond)
	}

	flags := rr.NodeSetupEnableTcpTransport | rr.NodeSetupEnableIntraTransport | rr.NodeSetupEnableLocalTransport |
		rr.NodeSetupLocalTransportStartClient
	setup, err := rr.ClientNodeSetup(rr.WithFlags(flags))
	if err != nil {
		fmt.Println("node setup failed:", err)
		cmd.Process.Kill()
		os.Exit(1)
	}
	// local transport discovery only (never network discovery)
	if lt := setup.LocalTransport(); lt != nil {
		lt.EnableNodeDiscoveryListening()
	}
	code := m.Run()
	setup.Close()
	io.WriteString(stdin, "\n")
	done := make(chan struct{})
	go func() { cmd.Wait(); close(done) }()
	select {
	case <-done:
	case <-time.After(10 * time.Second):
		cmd.Process.Kill()
	}
	os.Exit(code)
}
