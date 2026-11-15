package interop

import (
	"fmt"
	"testing"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	"github.com/robotraconteur/robotraconteur_go/test/clienttests"
)

// TestGoClientTestService1 runs the Go port of ServiceTestClient against the
// C++ test server.
func TestGoClientTestService1(t *testing.T) {
	clienttests.RunTestService1(t, rr.DefaultNode(), fmt.Sprintf("rr+tcp://localhost:%d", serverPort))
}

// TestGoClientTestService3 runs the Go port of ServiceTestClient2 against the
// C++ test server.
func TestGoClientTestService3(t *testing.T) {
	clienttests.RunTestService3(t, rr.DefaultNode(), fmt.Sprintf("rr+tcp://localhost:%d", serverPort))
}

// TestGoClientSubscriptions tests subscriptions against the C++ test server.
func TestGoClientSubscriptions(t *testing.T) {
	clienttests.RunSubscriptions(t, rr.DefaultNode(), fmt.Sprintf("rr+tcp://localhost:%d", serverPort), "testprog")
}
