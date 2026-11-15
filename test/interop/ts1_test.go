package interop

import (
	"errors"
	"reflect"
	"testing"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts1 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice1"
	ts2 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice2"
	tv "github.com/robotraconteur/robotraconteur_go/test/testvectors"
)

func connectTS1(t *testing.T) ts1.Testroot {
	t.Helper()
	o, err := rr.DefaultNode().ConnectService(serverURL, rr.WithObjectType("com.robotraconteur.testing.TestService1.testroot"))
	if err != nil {
		t.Fatalf("ConnectService: %v", err)
	}
	r, ok := o.(ts1.Testroot)
	if !ok {
		t.Fatalf("unexpected root type %T", o)
	}
	t.Cleanup(func() { rr.DefaultNode().DisconnectService(r) })
	return r
}

func must(t *testing.T, err error) {
	t.Helper()
	if err != nil {
		t.Fatal(err)
	}
}

func eq[T any](t *testing.T, name string, got, want T) {
	t.Helper()
	if !reflect.DeepEqual(got, want) {
		t.Errorf("%s: got %v, want %v", name, got, want)
	}
}

func checkArray[T comparable](t *testing.T, name string, get func() ([]T, error), want []T) {
	t.Helper()
	v, err := get()
	if err != nil {
		t.Errorf("%s: %v", name, err)
		return
	}
	eq(t, name, v, want)
}

func TestTS1Properties(t *testing.T) {
	r := connectTS1(t)
	must(t, r.SetD1(3.456))
	d1, err := r.GetD1()
	must(t, err)
	eq(t, "d1", d1, 12.345)

	must(t, r.SetD2(tv.MustGet[float64]("ts1.d2.to_service")))
	checkArray(t, "d2", r.GetD2, tv.MustGet[float64]("ts1.d2.to_client"))
	must(t, r.SetD3(tv.MustGet[float64]("ts1.d3.to_service")))
	checkArray(t, "d3", r.GetD3, tv.MustGet[float64]("ts1.d3.to_client"))
	must(t, r.SetD4(tv.MustGet[float64]("ts1.d4.to_service")))
	checkArray(t, "d4", r.GetD4, tv.MustGet[float64]("ts1.d4.to_client"))

	must(t, r.SetD5(&rr.MultiDimArray[float64]{Dims: tv.MustGet[uint32]("ts1.d5.to_service.dims"), Array: tv.MustGet[float64]("ts1.d5.to_service")}))
	d5, err := r.GetD5()
	must(t, err)
	eq(t, "d5", d5, &rr.MultiDimArray[float64]{Dims: tv.MustGet[uint32]("ts1.d5.to_client.dims"), Array: tv.MustGet[float64]("ts1.d5.to_client")})

	must(t, r.SetS1(3847.9283))
	s1, err := r.GetS1()
	must(t, err)
	eq(t, "s1", s1, float32(7.8573))
	must(t, r.SetS2(tv.MustGet[float32]("ts1.s2.to_service")))
	checkArray(t, "s2", r.GetS2, tv.MustGet[float32]("ts1.s2.to_client"))

	must(t, r.SetI8_1(45))
	i8, err := r.GetI8_1()
	must(t, err)
	eq(t, "i8_1", i8, int8(-66))
	must(t, r.SetI8_2(tv.MustGet[int8]("ts1.i8_2.to_service")))
	checkArray(t, "i8_2", r.GetI8_2, tv.MustGet[int8]("ts1.i8_2.to_client"))

	must(t, r.SetU8_1(232))
	u8, err := r.GetU8_1()
	must(t, err)
	eq(t, "u8_1", u8, uint8(222))
	must(t, r.SetU8_2(tv.MustGet[uint8]("ts1.u8_2.to_service")))
	checkArray(t, "u8_2", r.GetU8_2, tv.MustGet[uint8]("ts1.u8_2.to_client"))
	must(t, r.SetU8_3(&rr.MultiDimArray[uint8]{Dims: tv.MustGet[uint32]("ts1.u8_3.to_service.dims"), Array: tv.MustGet[uint8]("ts1.u8_3.to_service")}))
	u83, err := r.GetU8_3()
	must(t, err)
	eq(t, "u8_3", u83, &rr.MultiDimArray[uint8]{Dims: tv.MustGet[uint32]("ts1.u8_3.to_client.dims"), Array: tv.MustGet[uint8]("ts1.u8_3.to_client")})

	must(t, r.SetI16_1(2387))
	i16, err := r.GetI16_1()
	must(t, err)
	eq(t, "i16_1", i16, int16(-13428))
	must(t, r.SetI16_2(tv.MustGet[int16]("ts1.i16_2.to_service")))
	checkArray(t, "i16_2", r.GetI16_2, tv.MustGet[int16]("ts1.i16_2.to_client"))
}

func TestTS1Functions(t *testing.T) {
	r := connectTS1(t)
	must(t, r.Func1())
	must(t, r.Func2(10, 20.34))
	v, err := r.Func3(2, 3.45)
	must(t, err)
	eq(t, "func3", v, 5.45)
	m, err := r.Meaning_of_life()
	must(t, err)
	eq(t, "meaning_of_life", m, int32(42))

	if err := r.Func_errtest(); !errors.Is(err, rr.ErrRemoteError) {
		t.Errorf("func_errtest: expected RemoteError, got %v", err)
	}
	if err := r.Func_errtest1(); !errors.Is(err, rr.ErrDataTypeError) {
		t.Errorf("func_errtest1: expected DataTypeError, got %v", err)
	}
	var e1 *ts1.Testexception1
	if err := r.Func_errtest2(); !errors.As(err, &e1) {
		t.Errorf("func_errtest2: expected testexception1, got %T %v", err, err)
	}
	var e3 *ts2.Testexception3
	if err := r.Func_errtest3(); !errors.As(err, &e3) {
		t.Errorf("func_errtest3: expected testexception3, got %T %v", err, err)
	}
}

func TestTS1Events(t *testing.T) {
	r := connectTS1(t)
	ev1 := make(chan struct{}, 1)
	ev2 := make(chan struct{}, 1)
	c1 := r.Ev1().Connect(func() { ev1 <- struct{}{} })
	c2 := r.Ev2().Connect(func(d float64, s *ts1.Teststruct2) {
		if d == 27.3 && len(s.Mydat) > 0 && s.Mydat[0] == 98.23 {
			ev2 <- struct{}{}
		}
	})
	defer c1.Disconnect()
	defer c2.Disconnect()
	must(t, r.Func1())
	must(t, r.Func2(27.3, 98.23))
	for name, ch := range map[string]chan struct{}{"ev1": ev1, "ev2": ev2} {
		select {
		case <-ch:
		case <-time.After(2 * time.Second):
			t.Errorf("%s not received", name)
		}
	}
}

func TestTS1ObjRefs(t *testing.T) {
	r := connectTS1(t)
	o1, err := r.GetO1()
	must(t, err)
	must(t, o1.SetD1([]float64{1, 2, 3}))
	o1b, err := r.GetO1()
	must(t, err)
	if o1 != o1b {
		t.Errorf("expected the same object reference for o1")
	}
	o2, err := r.GetO2(32)
	must(t, err)
	must(t, o2.SetD1([]float64{4, 5}))
	o3, err := r.GetO3(12)
	must(t, err)
	must(t, o3.SetD1([]float64{7, 8}))
	d1, err := o3.GetD1()
	must(t, err)
	eq(t, "o3.d1", d1, []float64{7, 8})
	o4, err := r.GetO4("key")
	must(t, err)
	must(t, o4.SetD1([]float64{6}))
	o5, err := r.GetO5()
	must(t, err)
	if _, err := o5.Add_val(2); err != nil {
		t.Errorf("o5.add_val: %v", err)
	}
}

func TestTS1Async(t *testing.T) {
	r := connectTS1(t).(ts1.AsyncTestroot)
	done := make(chan error, 1)
	r.AsyncFunc3(2, 3.45, rr.TimeoutInfinite, func(v float64, err error) {
		if err == nil && v != 5.45 {
			err = errors.New("wrong value")
		}
		done <- err
	})
	select {
	case err := <-done:
		must(t, err)
	case <-time.After(5 * time.Second):
		t.Fatal("async call did not complete")
	}
	r.AsyncGetD1(rr.TimeoutInfinite, func(v float64, err error) { done <- err })
	must(t, <-done)
}
