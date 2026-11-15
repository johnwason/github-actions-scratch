package clienttests

import (
	"testing"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
)

// RunTestService1 runs the TestService1 client tests. baseURL is
// "rr+tcp://host:port" of a node hosting RobotRaconteurTestService and
// RobotRaconteurTestService_auth.
func RunTestService1(t *testing.T, n *rr.Node, baseURL string) {
	url := baseURL + "?service=RobotRaconteurTestService"
	authURL := baseURL + "?service=RobotRaconteurTestService_auth"
	run := func(name string, f func(t *testing.T)) { t.Run(name, f) }
	withRoot := func(f func(t *testing.T, r interface{})) {}
	_ = withRoot
	run("Properties", func(t *testing.T) { r := Connect(t, n, url); defer n.DisconnectService(r); TestProperties(t, r) })
	run("Functions", func(t *testing.T) { r := Connect(t, n, url); defer n.DisconnectService(r); TestFunctions(t, r) })
	run("Events", func(t *testing.T) { r := Connect(t, n, url); defer n.DisconnectService(r); TestEvents(t, r) })
	run("ObjRefs", func(t *testing.T) { r := Connect(t, n, url); defer n.DisconnectService(r); TestObjRefs(t, n, r) })
	run("Pipes", func(t *testing.T) { r := Connect(t, n, url); defer n.DisconnectService(r); TestPipes(t, r) })
	run("Callbacks", func(t *testing.T) { r := Connect(t, n, url); defer n.DisconnectService(r); TestCallbacks(t, r) })
	run("Wires", func(t *testing.T) { r := Connect(t, n, url); defer n.DisconnectService(r); TestWires(t, r) })
	run("Memories", func(t *testing.T) { r := Connect(t, n, url); defer n.DisconnectService(r); TestMemories(t, r) })
	run("Authentication", func(t *testing.T) { TestAuthentication(t, n, authURL) })
	run("ObjectLock", func(t *testing.T) { TestObjectLock(t, n, authURL) })
	run("MonitorLock", func(t *testing.T) { TestMonitorLock(t, n, url) })
	run("Async", func(t *testing.T) { r := Connect(t, n, url); defer n.DisconnectService(r); TestAsync(t, r) })
}
