// Package crosscheck runs the upstream C++ client test suites
// (robotraconteur_test_service*) against the Go port of the test services
// hosted in this process, over the loopback interface.
package crosscheck

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	"github.com/robotraconteur/robotraconteur_go/test/clienttests"
	"github.com/robotraconteur/robotraconteur_go/test/testservices"
)

const port = 22232

func cppTest(name string) string {
	if d := os.Getenv("RRGO_CPP_TEST_BIN_DIR"); d != "" {
		return filepath.Join(d, name)
	}
	p, _ := filepath.Abs(filepath.Join("../../build/rr/test/out_reldebug/bin", name))
	return p
}

func TestMain(m *testing.M) {
	if _, err := os.Stat(cppTest("robotraconteur_test_service")); err != nil {
		fmt.Println("skipping crosscheck tests: C++ tests not built")
		os.Exit(0)
	}
	setup, err := rr.ServerNodeSetup("testprog_go", port,
		rr.WithFlags(rr.NodeSetupEnableTcpTransport|rr.NodeSetupTcpTransportStartServer|rr.NodeSetupEnableIntraTransport|
			rr.NodeSetupIntraTransportStartServer))
	if err != nil {
		fmt.Println("node setup:", err)
		os.Exit(1)
	}
	roots, err := testservices.RegisterTestService1(setup.Node)
	if err != nil {
		fmt.Println("register:", err)
		os.Exit(1)
	}
	ts3root, err := testservices.RegisterTestService3(setup.Node)
	if err != nil {
		fmt.Println("register ts3:", err)
		os.Exit(1)
	}
	defer ts3root.Shutdown()
	if _, err := testservices.RegisterTestService5(setup.Node); err != nil {
		fmt.Println("register ts5:", err)
		os.Exit(1)
	}
	code := m.Run()
	for _, r := range roots {
		r.Shutdown()
	}
	setup.Close()
	os.Exit(code)
}

func runCpp(t *testing.T, name string) {
	t.Helper()
	cmd := exec.Command(cppTest(name))
	cmd.Env = append(os.Environ(), fmt.Sprintf("ROBOTRACONTEUR_TEST_SERVER_URL=rr+tcp://localhost:%d", port))
	out, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("%s failed: %v\n%s", name, err, out)
	}
	t.Logf("%s passed", name)
}

// TestCppClientTestService1 runs the C++ ServiceTestClient (TestService1/2)
// against the Go testroot service.
func TestCppClientTestService1(t *testing.T) { runCpp(t, "robotraconteur_test_service") }

// TestCppClientTestService3 runs the C++ ServiceTestClient2 (TestService3)
// against the Go testroot3 service.
func TestCppClientTestService3(t *testing.T) { runCpp(t, "robotraconteur_test_service2") }

// TestCppClientTestService5 runs the C++ ServiceTestClient3 (TestService5,
// asynchronous service) against the Go asynctestroot service.
func TestCppClientTestService5(t *testing.T) { runCpp(t, "robotraconteur_test_service3") }

// TestGoClientTestService1 runs the Go port of ServiceTestClient against the
// Go test services (Go client and Go server).
func TestGoClientTestService1(t *testing.T) {
	clienttests.RunTestService1(t, rr.DefaultNode(), fmt.Sprintf("rr+tcp://localhost:%d", port))
}
