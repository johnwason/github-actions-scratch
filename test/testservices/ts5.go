package testservices

// Go port of asynctestroot_impl (robotraconteur/test/cpp/service/ServiceTest3.cpp):
// a service implemented with asynchronous member functions.

import (
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts5 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice5"
)

// Asynctestroot implements com.robotraconteur.testing.TestService5.asynctestroot
// through the AsyncAsynctestroot interface.
type Asynctestroot struct {
	ts5.AsynctestrootDefaultImpl
}

var _ ts5.AsyncAsynctestroot = (*Asynctestroot)(nil)

func notImpl() error { return rr.NewNotImplementedError("") }

func (a *Asynctestroot) AsyncGetD1(_ time.Duration, h func(float64, error)) { go h(8.5515, nil) }
func (a *Asynctestroot) AsyncSetD1(v float64, _ time.Duration, h func(error)) {
	if v == 3.0819 {
		go h(nil)
		return
	}
	go h(rr.NewInvalidArgument(""))
}
func (a *Asynctestroot) AsyncGetS1(_ time.Duration, h func(string, error))      { h("", notImpl()) }
func (a *Asynctestroot) AsyncSetS1(string, time.Duration, func(error))          {}
func (a *Asynctestroot) AsyncGetD2(_ time.Duration, h func([]float64, error))   { h(nil, notImpl()) }
func (a *Asynctestroot) AsyncSetD2(_ []float64, _ time.Duration, h func(error)) { h(notImpl()) }
func (a *Asynctestroot) AsyncGetS2(_ time.Duration, h func([]string, error))    { h(nil, notImpl()) }
func (a *Asynctestroot) AsyncSetS2(_ []string, _ time.Duration, h func(error))  { h(notImpl()) }
func (a *Asynctestroot) AsyncGetS3(_ time.Duration, h func(*ts5.Teststruct4, error)) {
	h(nil, notImpl())
}
func (a *Asynctestroot) AsyncSetS3(_ *ts5.Teststruct4, _ time.Duration, h func(error)) { h(notImpl()) }
func (a *Asynctestroot) AsyncGetErr(_ time.Duration, h func(float64, error)) {
	go h(0, rr.NewInvalidArgument("Test message 1"))
}
func (a *Asynctestroot) AsyncSetErr(_ float64, _ time.Duration, h func(error)) {
	go h(rr.NewInvalidOperation("Test message 2"))
}
func (a *Asynctestroot) AsyncF1(_ time.Duration, h func(error)) { go h(nil) }
func (a *Asynctestroot) AsyncF2(v int32, _ time.Duration, h func(error)) {
	if v == 247 {
		go h(nil)
		return
	}
	go h(rr.NewInvalidArgument(""))
}
func (a *Asynctestroot) AsyncF3(x, y float64, _ time.Duration, h func(int32, error)) {
	go h(int32(x+y), nil)
}
func (a *Asynctestroot) AsyncErr_func(_ time.Duration, h func(error)) {
	go h(rr.NewInvalidOperation(""))
}
func (a *Asynctestroot) AsyncErr_func2(_ time.Duration, h func(float64, error)) {
	go h(0, ts5.NewAsynctestexp(""))
}

// Async generator functions are not used by the service skeleton (it calls
// the synchronous ones, as in C++); NotImplemented as in ServiceTest3.cpp.
func (a *Asynctestroot) AsyncF4(_ time.Duration, h func(rr.Generator2[float64], error)) {
	go h(nil, rr.NewNotImplementedError(""))
}
func (a *Asynctestroot) AsyncF5(_ float64, _ time.Duration, h func(rr.Generator3[float64], error)) {
	go h(nil, rr.NewNotImplementedError(""))
}
func (a *Asynctestroot) AsyncF6(_ float64, _ time.Duration, h func(rr.Generator1[*ts5.Teststruct4, *ts5.Teststruct5], error)) {
	go h(nil, rr.NewNotImplementedError(""))
}
func (a *Asynctestroot) AsyncErr_func3(_ time.Duration, h func(rr.Generator2[float64], error)) {
	go h(nil, rr.NewNotImplementedError(""))
}
func (a *Asynctestroot) Err_func3() (rr.Generator2[float64], error) {
	return nil, rr.NewInvalidOperation("")
}

// RegisterTestService5 registers RobotRaconteurTestService3 (TestService5).
func RegisterTestService5(n *rr.Node) (*Asynctestroot, error) {
	t := &Asynctestroot{}
	_, err := n.RegisterService("RobotRaconteurTestService3", "com.robotraconteur.testing.TestService5.asynctestroot", t)
	return t, err
}
