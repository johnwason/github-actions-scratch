package testservices

// Go port of asynctestroot_impl (robotraconteur/test/cpp/service/ServiceTest3.cpp).
// The C++ service implements its members asynchronously; in Go every service
// member already runs on its own goroutine, so the port is a plain blocking
// implementation (the sleeps stand in for the C++ deferred completions).

import (
	"context"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts5 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice5"
)

// Asynctestroot implements com.robotraconteur.testing.TestService5.asynctestroot.
type Asynctestroot struct {
	ts5.AsynctestrootDefaultImpl
}

var _ ts5.Asynctestroot = (*Asynctestroot)(nil)

func notImpl() error { return rr.NewNotImplementedError("") }

// deferred simulates a completion from another thread.
func deferred() { time.Sleep(time.Millisecond) }

func (a *Asynctestroot) Get_d1(ctx context.Context) (float64, error) {
	deferred()
	return 8.5515, nil
}
func (a *Asynctestroot) Set_d1(ctx context.Context, v float64) error {
	deferred()
	if v == 3.0819 {
		return nil
	}
	return rr.NewInvalidArgument("")
}
func (a *Asynctestroot) Get_s1(ctx context.Context) (string, error)           { return "", notImpl() }
func (a *Asynctestroot) Set_s1(ctx context.Context, _ string) error           { return notImpl() }
func (a *Asynctestroot) Get_d2(ctx context.Context) ([]float64, error)        { return nil, notImpl() }
func (a *Asynctestroot) Set_d2(ctx context.Context, _ []float64) error        { return notImpl() }
func (a *Asynctestroot) Get_s2(ctx context.Context) ([]string, error)         { return nil, notImpl() }
func (a *Asynctestroot) Set_s2(ctx context.Context, _ []string) error         { return notImpl() }
func (a *Asynctestroot) Get_s3(ctx context.Context) (*ts5.Teststruct4, error) { return nil, notImpl() }
func (a *Asynctestroot) Set_s3(ctx context.Context, _ *ts5.Teststruct4) error { return notImpl() }
func (a *Asynctestroot) Get_err(ctx context.Context) (float64, error) {
	deferred()
	return 0, rr.NewInvalidArgument("Test message 1")
}
func (a *Asynctestroot) Set_err(ctx context.Context, _ float64) error {
	deferred()
	return rr.NewInvalidOperation("Test message 2")
}
func (a *Asynctestroot) F1(ctx context.Context) error {
	deferred()
	return nil
}
func (a *Asynctestroot) F2(ctx context.Context, v int32) error {
	deferred()
	if v == 247 {
		return nil
	}
	return rr.NewInvalidArgument("")
}
func (a *Asynctestroot) F3(ctx context.Context, x, y float64) (int32, error) {
	deferred()
	return int32(x + y), nil
}
func (a *Asynctestroot) Err_func(ctx context.Context) error {
	deferred()
	return rr.NewInvalidOperation("")
}
func (a *Asynctestroot) Err_func2(ctx context.Context) (float64, error) {
	deferred()
	return 0, ts5.NewAsynctestexp("")
}
func (a *Asynctestroot) Err_func3(ctx context.Context) (rr.Generator2[float64], error) {
	return nil, rr.NewInvalidOperation("")
}

// RegisterTestService5 registers RobotRaconteurTestService3 (TestService5).
func RegisterTestService5(n *rr.Node) (*Asynctestroot, error) {
	t := &Asynctestroot{}
	_, err := n.RegisterService("RobotRaconteurTestService3", "com.robotraconteur.testing.TestService5.asynctestroot", t)
	return t, err
}
