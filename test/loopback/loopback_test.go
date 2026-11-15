// Package loopback tests Go services with Go clients in one process.
package loopback

import (
	"context"
	"errors"
	"fmt"
	"os"
	"reflect"
	"sync"
	"testing"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts1 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice1"
	_ "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice2"
)

const port = 22231

type testroot struct {
	ts1.TestrootDefaultImpl
	o1     *sub1
	cbDone chan error
	pipeRx chan []float64
	wireRx chan []float64
	m1     *rr.SliceArrayMemory[float64]
	once   sync.Once
	bwire  *rr.Wire[float64]
}

func (t *testroot) Set_broadcastwire(w *rr.Wire[float64]) { t.bwire = w }

func (t *testroot) Func3(ctx context.Context, d1, d2 float64) (float64, error) {
	if d1 == slowCall {
		time.Sleep(200 * time.Millisecond)
	}
	return d1 + d2, nil
}

// slowCall makes Func3 block for 200 ms (TestManyConcurrentCalls).
const slowCall = 1000

func (t *testroot) Func_errtest(ctx context.Context) error {
	return rr.NewInvalidOperation("expected error")
}
func (t *testroot) Func_errtest2(ctx context.Context) error { return ts1.NewTestexception1("custom") }
func (t *testroot) Func1(ctx context.Context) error {
	for _, h := range t.Get_ev1().Handlers() {
		h()
	}
	return nil
}
func (t *testroot) Get_o1(ctx context.Context) (ts1.Sub1, error) { return t.o1, nil }
func (t *testroot) Get_m1() rr.ArrayMemory[float64]              { return t.m1 }
func (t *testroot) Test_callbacks(ctx context.Context) error {
	ep, err := rr.CurrentEndpoint(ctx)
	if err != nil {
		return err
	}
	go func() {
		f, err := t.Get_cb3().GetClientFunction(ep)
		if err != nil {
			t.cbDone <- err
			return
		}
		v, err := f(2, 3)
		if err == nil && v != 5 {
			err = fmt.Errorf("cb3 returned %v", v)
		}
		t.cbDone <- err
	}()
	return nil
}

func (t *testroot) Set_p1(p *rr.Pipe[[]float64]) {
	t.TestrootDefaultImpl.Set_p1(p)
	p.SetPipeConnectCallback(func(ep *rr.PipeEndpoint[[]float64]) {
		ep.PacketReceivedEvent().Connect(func(ep *rr.PipeEndpoint[[]float64]) {
			for ep.Available() > 0 {
				v, err := ep.ReceivePacket()
				if err == nil {
					// echo back
					ep.SendPacket(context.Background(), v)
				}
			}
		})
	})
}

func (t *testroot) Set_w1(w *rr.Wire[[]float64]) {
	t.TestrootDefaultImpl.Set_w1(w)
	w.SetWireConnectCallback(func(c *rr.WireConnection[[]float64]) {
		c.WireValueChanged().Connect(func(c *rr.WireConnection[[]float64], v []float64, ts rr.TimeSpec) {
			c.SetOutValue(append([]float64{}, v...))
		})
	})
}

type sub1 struct {
	ts1.Sub1DefaultImpl
}

var node *rr.Node

func TestMain(m *testing.M) {
	setup, err := rr.ServerNodeSetup("go_loopback_test", port,
		rr.WithFlags(rr.NodeSetupEnableTcpTransport|rr.NodeSetupTcpTransportStartServer|rr.NodeSetupEnableIntraTransport|
			rr.NodeSetupIntraTransportStartServer|rr.NodeSetupTcpTransportListenLocalhost))
	if err != nil {
		fmt.Println("server setup:", err)
		os.Exit(1)
	}
	node = setup.Node
	root := &testroot{o1: &sub1{}, cbDone: make(chan error, 1), m1: rr.NewArrayMemory(make([]float64, 100))}
	root.Set_d1(context.Background(), 1.5)
	if testCtx, err = node.RegisterService("go_test", "com.robotraconteur.testing.TestService1.testroot", root); err != nil {
		fmt.Println("register:", err)
		os.Exit(1)
	}
	testRoot = root
	code := m.Run()
	setup.Close()
	os.Exit(code)
}

var (
	testRoot *testroot
	testCtx  *rr.ServerContext
)

func connect(t *testing.T) ts1.Testroot {
	ctx := t.Context()
	t.Helper()
	o, err := node.ConnectService(ctx, fmt.Sprintf("rr+tcp://localhost:%d?service=go_test", port))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { node.DisconnectService(o) })
	return o.(ts1.Testroot)
}

func TestPropertiesAndFunctions(t *testing.T) {
	ctx := t.Context()
	r := connect(t)
	if v, err := r.Get_d1(ctx); err != nil || v != 1.5 {
		t.Fatalf("d1 %v %v", v, err)
	}
	if err := r.Set_d2(ctx, []float64{1, 2, 3}); err != nil {
		t.Fatal(err)
	}
	if v, err := r.Get_d2(ctx); err != nil || !reflect.DeepEqual(v, []float64{1, 2, 3}) {
		t.Fatalf("d2 %v %v", v, err)
	}
	if v, err := r.Func3(ctx, 1, 2); err != nil || v != 3 {
		t.Fatalf("func3 %v %v", v, err)
	}
	if err := r.Func_errtest(ctx); !errors.Is(err, rr.ErrInvalidOperation) {
		t.Fatalf("func_errtest %v", err)
	}
	var e1 *ts1.Testexception1
	if err := r.Func_errtest2(ctx); !errors.As(err, &e1) || e1.E.Message != "custom" {
		t.Fatalf("func_errtest2 %T %v", err, err)
	}
	if _, err := r.Meaning_of_life(ctx); !errors.Is(err, rr.ErrNotImplementedError) {
		t.Fatalf("default impl should return NotImplemented, got %v", err)
	}
}

func TestEventsAndCallbacks(t *testing.T) {
	ctx := t.Context()
	r := connect(t)
	got := make(chan struct{}, 1)
	c := r.Get_ev1().Connect(func() { got <- struct{}{} })
	defer c.Disconnect()
	if err := r.Func1(ctx); err != nil {
		t.Fatal(err)
	}
	select {
	case <-got:
	case <-time.After(3 * time.Second):
		t.Fatal("event not received")
	}
	r.Get_cb3().SetFunction(func(d1, d2 float64) (float64, error) { return d1 + d2, nil })
	if err := r.Test_callbacks(ctx); err != nil {
		t.Fatal(err)
	}
	select {
	case err := <-testRoot.cbDone:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("callback not completed")
	}
}

func TestObjRef(t *testing.T) {
	ctx := t.Context()
	r := connect(t)
	o1, err := r.Get_o1(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if err := o1.Set_d1(ctx, []float64{9}); err != nil {
		t.Fatal(err)
	}
	if v, err := o1.Get_d1(ctx); err != nil || !reflect.DeepEqual(v, []float64{9}) {
		t.Fatalf("o1.d1 %v %v", v, err)
	}
	if _, err := r.Get_o2(ctx, 1); !errors.Is(err, rr.ErrNotImplementedError) {
		t.Fatalf("o2 %v", err)
	}
}

func TestPipe(t *testing.T) {
	ctx := t.Context()
	r := connect(t)
	ep, err := r.Get_p1().Connect(ctx, rr.AnyIndex)
	if err != nil {
		t.Fatal(err)
	}
	defer ep.Close()
	if _, err := ep.SendPacket(ctx, []float64{1, 2}); err != nil {
		t.Fatal(err)
	}
	v, err := ep.ReceivePacketWait(within(t, 3*time.Second))
	if err != nil || !reflect.DeepEqual(v, []float64{1, 2}) {
		t.Fatalf("pipe echo %v %v", v, err)
	}
}

func TestWire(t *testing.T) {
	ctx := t.Context()
	r := connect(t)
	c, err := r.Get_w1().Connect(ctx)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	if err := c.SetOutValue([]float64{4, 5}); err != nil {
		t.Fatal(err)
	}
	if err := c.WaitInValueValid(within(t, 3*time.Second)); err != nil {
		t.Fatalf("wire in value not valid %v", err)
	}
	v, err := c.InValue()
	if err != nil || !reflect.DeepEqual(v, []float64{4, 5}) {
		t.Fatalf("wire echo %v %v", v, err)
	}
}

func TestMemory(t *testing.T) {
	r := connect(t)
	m := r.Get_m1()
	n, err := m.Length()
	if err != nil || n != 100 {
		t.Fatalf("length %v %v", n, err)
	}
	if err := m.Write(10, []float64{1, 2, 3, 4}, 1, 3); err != nil {
		t.Fatal(err)
	}
	if got := testRoot.m1.Data()[10:13]; !reflect.DeepEqual(got, []float64{2, 3, 4}) {
		t.Fatalf("server data %v", got)
	}
	buf := make([]float64, 5)
	if err := m.Read(9, buf, 0, 5); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(buf, []float64{0, 2, 3, 4, 0}) {
		t.Fatalf("read %v", buf)
	}
	if err := m.Read(99, buf, 0, 5); !errors.Is(err, rr.ErrOutOfRange) {
		t.Fatalf("expected out of range, got %v", err)
	}
}

// within returns a context of t ending after d.
func within(t testing.TB, d time.Duration) context.Context {
	ctx, cancel := context.WithTimeout(t.Context(), d)
	t.Cleanup(cancel)
	return ctx
}
