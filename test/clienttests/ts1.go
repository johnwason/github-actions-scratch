// Package clienttests is a Go port of the upstream C++ ServiceTestClient
// suites. The same tests run against the C++ test server (test/interop) and
// the Go port of the test services (test/crosscheck).
package clienttests

import (
	"errors"
	"fmt"
	"math/rand"
	"reflect"
	"sync"
	"testing"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts1 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice1"
	ts2 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice2"
	tv "github.com/robotraconteur/robotraconteur_go/test/testvectors"
)

func must(t testing.TB, err error) {
	t.Helper()
	if err != nil {
		t.Fatal(err)
	}
}

func expectNoErr(t testing.TB, what string, err error) {
	t.Helper()
	if err != nil {
		t.Errorf("%s: %v", what, err)
	}
}

func eq[T any](t testing.TB, name string, got, want T) {
	t.Helper()
	if !reflect.DeepEqual(got, want) {
		t.Errorf("%s: got %v, want %v", name, abbrev(got), abbrev(want))
	}
}

func abbrev(v any) string {
	s := ""
	if v != nil {
		s = reflect.TypeOf(v).String() + " "
	}
	s += fmtShort(v)
	return s
}

func fmtShort(v any) string {
	s := ""
	func() {
		defer func() { recover() }()
		s = sprint(v)
	}()
	if len(s) > 200 {
		return s[:200] + "..."
	}
	return s
}

func get[T any](t testing.TB, name string, f func() (T, error)) T {
	t.Helper()
	v, err := f()
	if err != nil {
		t.Errorf("%s: %v", name, err)
	}
	return v
}

func waitFor(t testing.TB, name string, ch <-chan struct{}, d time.Duration) {
	t.Helper()
	select {
	case <-ch:
	case <-time.After(d):
		t.Errorf("%s: timeout", name)
	}
}

// eventually retries f for up to d (ShouldNotBeErr_Timeout).
func eventually(t testing.TB, name string, d time.Duration, f func() error) {
	t.Helper()
	end := time.Now().Add(d)
	var err error
	for time.Now().Before(end) {
		if err = f(); err == nil {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Errorf("%s: %v", name, err)
}

// Connect connects to the TestService1 root.
func Connect(t testing.TB, n *rr.Node, url string, opts ...rr.ConnectOption) ts1.Testroot {
	t.Helper()
	o, err := n.ConnectService(url, opts...)
	if err != nil {
		t.Fatalf("ConnectService(%s): %v", url, err)
	}
	r, ok := o.(ts1.Testroot)
	if !ok {
		t.Fatalf("unexpected root type %T", o)
	}
	return r
}

// TestProperties ports ServiceTestClient::TestProperties (numeric, string,
// structure, map, list and varvalue properties).
func TestProperties(t *testing.T, r ts1.Testroot) {
	v := tv.MustGet[float64]
	u32 := tv.MustGet[uint32]
	mda := func(n string) *rr.MultiDimArray[float64] {
		return &rr.MultiDimArray[float64]{Dims: u32(n + ".dims"), Array: v(n)}
	}
	expectNoErr(t, "set d1", r.SetD1(3.456))
	eq(t, "d1", get(t, "d1", r.GetD1), 12.345)
	expectNoErr(t, "set d2", r.SetD2(v("ts1.d2.to_service")))
	eq(t, "d2", get(t, "d2", r.GetD2), v("ts1.d2.to_client"))
	expectNoErr(t, "set d3", r.SetD3(v("ts1.d3.to_service")))
	eq(t, "d3", get(t, "d3", r.GetD3), v("ts1.d3.to_client"))
	expectNoErr(t, "set d4", r.SetD4(v("ts1.d4.to_service")))
	eq(t, "d4", get(t, "d4", r.GetD4), v("ts1.d4.to_client"))
	expectNoErr(t, "set d5", r.SetD5(mda("ts1.d5.to_service")))
	eq(t, "d5", get(t, "d5", r.GetD5), mda("ts1.d5.to_client"))
	expectNoErr(t, "set d6", r.SetD6(mda("ts1.d6.to_service")))
	eq(t, "d6", get(t, "d6", r.GetD6), mda("ts1.d6.to_client"))

	expectNoErr(t, "set s1", r.SetS1(3847.9283))
	eq(t, "s1", get(t, "s1", r.GetS1), float32(7.8573))
	expectNoErr(t, "set s2", r.SetS2(tv.MustGet[float32]("ts1.s2.to_service")))
	eq(t, "s2", get(t, "s2", r.GetS2), tv.MustGet[float32]("ts1.s2.to_client"))
	expectNoErr(t, "set i8_1", r.SetI8_1(45))
	eq(t, "i8_1", get(t, "i8_1", r.GetI8_1), int8(-66))
	expectNoErr(t, "set i8_2", r.SetI8_2(tv.MustGet[int8]("ts1.i8_2.to_service")))
	eq(t, "i8_2", get(t, "i8_2", r.GetI8_2), tv.MustGet[int8]("ts1.i8_2.to_client"))
	expectNoErr(t, "set u8_1", r.SetU8_1(232))
	eq(t, "u8_1", get(t, "u8_1", r.GetU8_1), uint8(222))
	expectNoErr(t, "set u8_2", r.SetU8_2(tv.MustGet[uint8]("ts1.u8_2.to_service")))
	eq(t, "u8_2", get(t, "u8_2", r.GetU8_2), tv.MustGet[uint8]("ts1.u8_2.to_client"))
	expectNoErr(t, "set u8_3", r.SetU8_3(&rr.MultiDimArray[uint8]{Dims: u32("ts1.u8_3.to_service.dims"), Array: tv.MustGet[uint8]("ts1.u8_3.to_service")}))
	eq(t, "u8_3", get(t, "u8_3", r.GetU8_3), &rr.MultiDimArray[uint8]{Dims: u32("ts1.u8_3.to_client.dims"), Array: tv.MustGet[uint8]("ts1.u8_3.to_client")})
	expectNoErr(t, "set i16_1", r.SetI16_1(2387))
	eq(t, "i16_1", get(t, "i16_1", r.GetI16_1), int16(-13428))
	expectNoErr(t, "set i16_2", r.SetI16_2(tv.MustGet[int16]("ts1.i16_2.to_service")))
	eq(t, "i16_2", get(t, "i16_2", r.GetI16_2), tv.MustGet[int16]("ts1.i16_2.to_client"))
	expectNoErr(t, "set u16_1", r.SetU16_1(54732))
	eq(t, "u16_1", get(t, "u16_1", r.GetU16_1), uint16(60981))
	expectNoErr(t, "set u16_2", r.SetU16_2(tv.MustGet[uint16]("ts1.u16_2.to_service")))
	eq(t, "u16_2", get(t, "u16_2", r.GetU16_2), tv.MustGet[uint16]("ts1.u16_2.to_client"))
	expectNoErr(t, "set i32_1", r.SetI32_1(-9837284))
	eq(t, "i32_1", get(t, "i32_1", r.GetI32_1), int32(898734))
	expectNoErr(t, "set i32_2", r.SetI32_2(tv.MustGet[int32]("ts1.i32_2.to_service")))
	eq(t, "i32_2", get(t, "i32_2", r.GetI32_2), tv.MustGet[int32]("ts1.i32_2.to_client"))
	huge := make([]int32, 2621440)
	for i := range huge {
		huge[i] = int32(-i)
	}
	expectNoErr(t, "set i32_huge", r.SetI32_huge(huge))
	h := get(t, "i32_huge", r.GetI32_huge)
	if len(h) != 2621440 || h[1234567] != 1234567 {
		t.Errorf("i32_huge: wrong value")
	}
	expectNoErr(t, "set u32_1", r.SetU32_1(1550099195))
	eq(t, "u32_1", get(t, "u32_1", r.GetU32_1), uint32(547919123))
	expectNoErr(t, "set u32_2", r.SetU32_2(tv.MustGet[uint32]("ts1.u32_2.to_service")))
	eq(t, "u32_2", get(t, "u32_2", r.GetU32_2), tv.MustGet[uint32]("ts1.u32_2.to_client"))
	expectNoErr(t, "set i64_1", r.SetI64_1(8621740821050813024))
	eq(t, "i64_1", get(t, "i64_1", r.GetI64_1), int64(-1357833931563696072))
	expectNoErr(t, "set i64_2", r.SetI64_2(tv.MustGet[int64]("ts1.i64_2.to_service")))
	eq(t, "i64_2", get(t, "i64_2", r.GetI64_2), tv.MustGet[int64]("ts1.i64_2.to_client"))
	expectNoErr(t, "set u64_1", r.SetU64_1(1465640522145789825))
	eq(t, "u64_1", get(t, "u64_1", r.GetU64_1), uint64(13389861970863644378))
	expectNoErr(t, "set u64_2", r.SetU64_2(tv.MustGet[uint64]("ts1.u64_2.to_service")))
	eq(t, "u64_2", get(t, "u64_2", r.GetU64_2), tv.MustGet[uint64]("ts1.u64_2.to_client"))
	expectNoErr(t, "set str1", r.SetStr1("Hello Server!"))
	eq(t, "str1", get(t, "str1", r.GetStr1), "Hello Client!")

	t2 := func(n string) *ts1.Teststruct2 { return &ts1.Teststruct2{Mydat: v(n)} }
	s := get(t, "struct1", r.GetStruct1)
	if s != nil {
		eq(t, "struct1.dat1", s.Dat1, v("ts1.struct1.dat1.to_client"))
		eq(t, "struct1.str2", s.Str2, "Hello world!")
		eq(t, "struct1.vec3", s.Vec3, map[int32]string{1: "Hello Client!", 2: "Hello Client, again", 4372: "This is yet another test string"})
		eq(t, "struct1.list5", s.List5, []string{"Hello Client!", "Hello Client, again", "This is yet another test string"})
		eq(t, "struct1.struct1", s.Struct1, t2("ts1.struct1.struct1_mydat.to_client"))
		eq(t, "struct1.lstruct3", s.Lstruct3, []*ts1.Teststruct2{t2("ts1.struct1.dstruct2_1.to_client"), t2("ts1.struct1.dstruct2_3.to_client")})
		eq(t, "struct1.multidimarray", s.Multidimarray, mda("ts1.struct1.multidimarray.to_client"))
		eq(t, "struct1.var3", s.Var3, any("This is a vartype string"))
	}
	expectNoErr(t, "set struct1", r.SetStruct1(&ts1.Teststruct1{
		Dat1: v("ts1.struct1.dat1.to_service"), Str2: "Hello world 2!",
		Vec3:  map[int32]string{10: "Hello Server!", 11: "Hello Server, again", 46372: "Test string!", 46373: "Test string again"},
		Dict4: map[string]string{"cteststring1": "Hello Server!", "cteststring2": "Hello Server, again"},
		List5: []string{"Hello Server!", "Hello Server, again"}, Struct1: t2("ts1.struct1.struct1_mydat.to_service"),
		Dstruct2: map[string]*ts1.Teststruct2{"ctest1": t2("ts1.struct1.dstruct2_1.to_service"), "anothertest": t2("ts1.struct1.dstruct2_2.to_service")},
		Lstruct3: []*ts1.Teststruct2{t2("ts1.struct1.dstruct2_1.to_service"), t2("ts1.struct1.dstruct2_2.to_service")},
		Multidimarray: mda("ts1.struct1.multidimarray.to_service"), Var3: v("ts1.struct1.var3.to_service"),
	}))
	eq(t, "struct2", get(t, "struct2", r.GetStruct2), t2("ts1.struct2.to_client"))
	expectNoErr(t, "set struct2", r.SetStruct2(t2("ts1.struct2.to_service")))

	eq(t, "is_d1", get(t, "is_d1", r.GetIs_d1), map[int32]float64{9285: 1.643392e-01, 74822: 1.537133e+09, 4: 1.369505e-03})
	expectNoErr(t, "set is_d1", r.SetIs_d1(map[int32]float64{928: 4.074501e-07, 394820: -4.535303e+05, 623: -2.956241e-20}))
	eq(t, "is_d2", get(t, "is_d2", r.GetIs_d2), map[string]float64{"testval1": -1.079664e+16, "testval2": 2.224846e+00})
	expectNoErr(t, "set is_d2", r.SetIs_d2(map[string]float64{"testval3": 5.242474e+10, "testval4": 2.208636e+08}))
	eq(t, "is_d3", get(t, "is_d3", r.GetIs_d3), map[int32][]float64{12: v("ts1.is_d3.to_client.a"), 832: v("ts1.is_d3.to_client.b")})
	expectNoErr(t, "set is_d3", r.SetIs_d3(map[int32][]float64{47: v("ts1.is_d3.to_service.a"), 324: v("ts1.is_d3.to_service.b")}))
	eq(t, "is_d5", get(t, "is_d5", r.GetIs_d5), map[int32]*rr.MultiDimArray[float64]{564: mda("ts1.is_d5.to_client")})
	expectNoErr(t, "set is_d5", r.SetIs_d5(map[int32]*rr.MultiDimArray[float64]{328: mda("ts1.is_d5.to_service")}))
	eq(t, "is_str1", get(t, "is_str1", r.GetIs_str1), map[int32]string{23: "Hello server"})
	expectNoErr(t, "set is_str1", r.SetIs_str1(map[int32]string{24: "Hello client"}))
	eq(t, "is_struct2", get(t, "is_struct2", r.GetIs_struct2), map[string]*ts1.Teststruct2{"testval1": t2("ts1.is_struct2.to_client")})
	expectNoErr(t, "set is_struct2", r.SetIs_struct2(map[string]*ts1.Teststruct2{"testval2": t2("ts1.is_struct2.to_service")}))

	eq(t, "list_d1", get(t, "list_d1", r.GetList_d1), []float64{1.643392e-01, 1.537133e+09, 1.369505e-03})
	expectNoErr(t, "set list_d1", r.SetList_d1([]float64{4.074501e-07, -4.535303e+05, -2.956241e-20}))
	eq(t, "list_d5", get(t, "list_d5", r.GetList_d5), []*rr.MultiDimArray[float64]{mda("ts1.is_d5.to_client")})
	expectNoErr(t, "set list_d5", r.SetList_d5([]*rr.MultiDimArray[float64]{mda("ts1.is_d5.to_service")}))
	eq(t, "list_struct1", get(t, "list_struct1", r.GetList_struct1), []*ts1.Teststruct2{t2("ts1.is_struct1.to_client")})
	expectNoErr(t, "set list_struct1", r.SetList_struct1([]*ts1.Teststruct2{t2("ts1.is_struct1.to_service")}))
	eq(t, "struct3", get(t, "struct3", r.GetStruct3), &ts2.Ostruct2{A1: v("ts1.struct3.to_client")})
	expectNoErr(t, "set struct3", r.SetStruct3(&ts2.Ostruct2{A1: v("ts1.struct3.to_service")}))

	// varvalue
	expectNoErr(t, "set var1", r.SetVar1("hello"))
	eq(t, "var1", get(t, "var1", r.GetVar1), any("hello"))
	expectNoErr(t, "set var2", r.SetVar2(map[int32]any{1: []int32{5}}))
	eq(t, "var2", get(t, "var2", r.GetVar2), map[int32]any{1: []int32{5}})
	eq(t, "var_num", get(t, "var_num", r.GetVar_num), any(tv.MustGet[int32]("ts1.var_num.to_client")))
	expectNoErr(t, "set var_num", r.SetVar_num(tv.MustGet[int32]("ts1.var_num.to_service")))
	eq(t, "var_str", get(t, "var_str", r.GetVar_str), any("Hello Client!"))
	expectNoErr(t, "set var_str", r.SetVar_str("Hello Server!"))
	eq(t, "var_struct", get(t, "var_struct", r.GetVar_struct), any(t2("ts1.var_struct.to_client")))
	expectNoErr(t, "set var_struct", r.SetVar_struct(t2("ts1.var_struct.to_service")))
	eq(t, "var_vector", get(t, "var_vector", r.GetVar_vector), any(map[int32]any{10: "Hello Client!"}))
	expectNoErr(t, "set var_vector", r.SetVar_vector(map[int32]any{11: "Hello Server!"}))
	eq(t, "var_dictionary", get(t, "var_dictionary", r.GetVar_dictionary), any(map[string]any{"test1": "Hello Client!"}))
	expectNoErr(t, "set var_dictionary", r.SetVar_dictionary(map[string]any{"test2": "Hello Server!"}))
	eq(t, "var_list", get(t, "var_list", r.GetVar_list), any([]any{"Hello Client!"}))
	expectNoErr(t, "set var_list", r.SetVar_list([]any{"Hello Server!"}))
	eq(t, "var_multidimarray", get(t, "var_multidimarray", r.GetVar_multidimarray), any(mda("ts1.var_multidimarray.to_client")))
	expectNoErr(t, "set var_multidimarray", r.SetVar_multidimarray(mda("ts1.var_multidimarray.to_service")))

	if _, err := r.GetErrtest(); err == nil {
		t.Error("errtest: expected error")
	}
	if err := r.SetErrtest(1); err == nil {
		t.Error("set errtest: expected error")
	}
	n, err := r.GetNulltest()
	expectNoErr(t, "nulltest", err)
	if n != nil {
		t.Error("nulltest: expected nil")
	}
	expectNoErr(t, "set nulltest", r.SetNulltest(nil))
}

// TestFunctions ports ServiceTestClient::TestFunctions.
func TestFunctions(t *testing.T, r ts1.Testroot) {
	expectNoErr(t, "func1", r.Func1())
	expectNoErr(t, "func2", r.Func2(10, 20.34))
	eq(t, "func3", get(t, "func3", func() (float64, error) { return r.Func3(2, 3.45) }), 5.45)
	eq(t, "meaning_of_life", get(t, "meaning_of_life", r.Meaning_of_life), int32(42))
	if err := r.Func_errtest(); !errors.Is(err, rr.ErrRemoteError) {
		t.Errorf("func_errtest: %v", err)
	}
	if err := r.Func_errtest1(); !errors.Is(err, rr.ErrDataTypeError) {
		t.Errorf("func_errtest1: %v", err)
	}
	var e1 *ts1.Testexception1
	if err := r.Func_errtest2(); !errors.As(err, &e1) {
		t.Errorf("func_errtest2: %T %v", err, err)
	}
	var e3 *ts2.Testexception3
	if err := r.Func_errtest3(); !errors.As(err, &e3) {
		t.Errorf("func_errtest3: %T %v", err, err)
	}
}

// TestEvents ports ServiceTestClient::TestEvents.
func TestEvents(t *testing.T, r ts1.Testroot) {
	ev1 := make(chan struct{}, 1)
	ev2 := make(chan struct{}, 1)
	c1 := r.Ev1().Connect(func() {
		select {
		case ev1 <- struct{}{}:
		default:
		}
	})
	c2 := r.Ev2().Connect(func(d float64, s *ts1.Teststruct2) {
		if d == 27.3 && s != nil && len(s.Mydat) > 0 && s.Mydat[0] == 98.23 {
			ev2 <- struct{}{}
		}
	})
	defer c1.Disconnect()
	defer c2.Disconnect()
	expectNoErr(t, "func1", r.Func1())
	expectNoErr(t, "func2", r.Func2(27.3, 98.23))
	waitFor(t, "ev1", ev1, 2*time.Second)
	waitFor(t, "ev2", ev2, 2*time.Second)
}

// TestObjRefs ports ServiceTestClient::TestObjRefs.
func TestObjRefs(t *testing.T, n *rr.Node, r ts1.Testroot) {
	o1, err := r.GetO1()
	must(t, err)
	expectNoErr(t, "o1.d1", o1.SetD1([]float64{1, 2, 3}))
	o1b, _ := r.GetO1()
	if o1 != o1b {
		t.Error("o1: object references should be reused")
	}
	o2, err := r.GetO2(32)
	must(t, err)
	eq(t, "o2.i_ind", get(t, "o2.i_ind", o2.GetI_ind), int32(32))
	o4, err := r.GetO4("key")
	must(t, err)
	eq(t, "o4.s_ind", get(t, "o4.s_ind", o4.GetS_ind), "key")
	o21, err := o1.GetO2_1()
	must(t, err)
	expectNoErr(t, "o2_1.data", o21.SetData("hello"))
	o3, err := o21.GetO3_1("x")
	must(t, err)
	eq(t, "o3_1.ind", get(t, "o3_1.ind", o3.GetInd), "x")
	eq(t, "o3_1.add", get(t, "o3_1.add", func() (float64, error) { return o3.Add(1) }), 43.0)
	o5, err := r.GetO5()
	must(t, err)
	eq(t, "o5.add_val", get(t, "o5", func() (float64, error) { return o5.Add_val(2) }), 3.0)
	for op, want := range []string{"com.robotraconteur.testing.TestService1.sub1", "com.robotraconteur.testing.TestService1.sub2",
		"com.robotraconteur.testing.TestService2.subobj"} {
		must(t, r.O6_op(int32(op)))
		o6, err := r.GetO6()
		must(t, err)
		ot, err := n.ObjectType(o6)
		must(t, err)
		eq(t, "o6 type", ot, want)
	}
}

// TestPipes ports ServiceTestClient::TestPipes.
func TestPipes(t *testing.T, r ts1.Testroot) {
	e1, err := r.P1().Connect(rr.AnyIndex)
	must(t, err)
	e2, err := r.P1().Connect(3432)
	must(t, err)
	e3, err := r.P2().Connect(rr.AnyIndex)
	must(t, err)
	e1.SetRequestPacketAck(true)
	var mu sync.Mutex
	var packetNum uint32
	ackRecv := false
	e1.PacketAckReceivedEvent().Connect(func(_ *rr.PipeEndpoint[[]float64], n uint32) {
		mu.Lock()
		defer mu.Unlock()
		if n == packetNum {
			ackRecv = true
		}
	})
	ee2 := make(chan struct{}, 8)
	ee3 := make(chan struct{}, 8)
	e2.PacketReceivedEvent().Connect(func(*rr.PipeEndpoint[[]float64]) { ee2 <- struct{}{} })
	e3.PacketReceivedEvent().Connect(func(*rr.PipeEndpoint[*ts1.Teststruct2]) { ee3 <- struct{}{} })
	d3 := tv.MustGet[float64]("ts1.client.d3")
	mu.Lock()
	packetNum, err = e1.SendPacket([]float64{1, 2, 3, 4})
	mu.Unlock()
	expectNoErr(t, "send d1", err)
	_, err = e1.SendPacket([]float64{5, 6, 7, 8})
	expectNoErr(t, "send d2", err)
	_, err = e1.SendPacket(d3)
	expectNoErr(t, "send d3", err)
	for _, x := range []float64{3.21, 4.72, 72.34} {
		_, err = e2.SendPacket([]float64{x})
		expectNoErr(t, "send e2", err)
	}
	_, err = e3.SendPacket(&ts1.Teststruct2{Mydat: []float64{738.29}})
	expectNoErr(t, "send s1", err)
	_, err = e3.SendPacket(&ts1.Teststruct2{Mydat: []float64{89.83}})
	expectNoErr(t, "send s2", err)

	eq(t, "e1 1", get(t, "e1 1", func() ([]float64, error) { return e1.ReceivePacketWait(5 * time.Second) }), []float64{1, 2, 3, 4})
	eq(t, "e1 2", get(t, "e1 2", func() ([]float64, error) { return e1.ReceivePacketWait(5 * time.Second) }), []float64{5, 6, 7, 8})
	p1, ok1, err := e1.TryReceivePacketWait(5*time.Second, true)
	expectNoErr(t, "e1 peek", err)
	p2, ok2, err := e1.TryReceivePacketWait(5*time.Second, true)
	expectNoErr(t, "e1 peek 2", err)
	if !ok1 || !ok2 {
		t.Error("TryReceivePacketWait failed")
	}
	eq(t, "e1 peek", p1, d3)
	eq(t, "e1 peek 2", p2, d3)
	waitFor(t, "ee2", ee2, 5*time.Second)
	waitFor(t, "ee3", ee3, 5*time.Second)
	time.Sleep(200 * time.Millisecond)
	eq(t, "e2 1", get(t, "e2 1", e2.ReceivePacket), []float64{3.21})
	eq(t, "e2 2", get(t, "e2 2", func() ([]float64, error) { return e2.ReceivePacketWait(time.Second) }), []float64{4.72})
	eq(t, "e2 3", get(t, "e2 3", func() ([]float64, error) { return e2.ReceivePacketWait(time.Second) }), []float64{72.34})
	eq(t, "e3 1", get(t, "e3 1", e3.ReceivePacket), &ts1.Teststruct2{Mydat: []float64{738.29}})
	eq(t, "e3 2", get(t, "e3 2", func() (*ts1.Teststruct2, error) { return e3.ReceivePacketWait(time.Second) }),
		&ts1.Teststruct2{Mydat: []float64{89.83}})
	eventually(t, "pipe_check_error", time.Second, r.Pipe_check_error)
	mu.Lock()
	if !ackRecv {
		t.Log("packet ack event not observed on client")
	}
	mu.Unlock()
	expectNoErr(t, "e1 close", e1.Close())
	expectNoErr(t, "e3 close", e3.Close())
	_ = e2
}

// TestCallbacks ports ServiceTestClient::TestCallbacks.
func TestCallbacks(t *testing.T, r ts1.Testroot) {
	var mu sync.Mutex
	called := map[string]bool{}
	mark := func(n string) { mu.Lock(); called[n] = true; mu.Unlock() }
	r.Cb1().SetFunction(func() error { mark("cb1"); return nil })
	r.Cb2().SetFunction(func(d1, d2 float64) error {
		if d1 != 739.2 || d2 != 0.392 {
			return errors.New("bad cb2 args")
		}
		mark("cb2")
		return nil
	})
	r.Cb3().SetFunction(func(d1, d2 float64) (float64, error) { mark("cb3"); return d1 + d2 + 3.14, nil })
	r.Cb_meaning_of_life().SetFunction(func() (int32, error) { mark("cb4"); return 42, nil })
	r.Cb_errtest().SetFunction(func() error { mark("cb5"); return rr.NewInvalidOperation("test error") })
	expectNoErr(t, "test_callbacks", r.Test_callbacks())
	for _, n := range []string{"cb1", "cb2", "cb3", "cb4", "cb5"} {
		if !called[n] {
			t.Errorf("%s not called", n)
		}
	}
}

// TestWires ports ServiceTestClient::TestWires.
func TestWires(t *testing.T, r ts1.Testroot) {
	w1, err := r.W1().Connect()
	must(t, err)
	w2, err := r.W2().Connect()
	must(t, err)
	w3, err := r.W3().Connect()
	must(t, err)
	we2 := make(chan struct{}, 16)
	we3 := make(chan struct{}, 16)
	w2.WireValueChanged().Connect(func(*rr.WireConnection[*ts1.Teststruct2], *ts1.Teststruct2, rr.TimeSpec) {
		select {
		case we2 <- struct{}{}:
		default:
		}
	})
	w3.WireValueChanged().Connect(func(*rr.WireConnection[*rr.MultiDimArray[int32]], *rr.MultiDimArray[int32], rr.TimeSpec) {
		select {
		case we3 <- struct{}{}:
		default:
		}
	})
	w1.SetInValueLifespan(10 * time.Second)
	expectNoErr(t, "w1 set 0", w1.SetOutValue([]float64{0}))
	expectNoErr(t, "w2 set 0", w2.SetOutValue(&ts1.Teststruct2{Mydat: []float64{0}}))
	expectNoErr(t, "w3 set 0", w3.SetOutValue(&rr.MultiDimArray[int32]{Dims: []uint32{1, 1}, Array: []int32{0}}))
	d1 := tv.MustGet[float64]("ts1.client.d1")
	d2 := tv.MustGet[float64]("ts1.client.d2")
	a2 := &rr.MultiDimArray[int32]{Dims: tv.MustGet[uint32]("ts1.client.i3.dims"), Array: tv.MustGet[int32]("ts1.client.i3")}
	expectNoErr(t, "w1 set", w1.SetOutValue(d1))
	expectNoErr(t, "w2 set", w2.SetOutValue(&ts1.Teststruct2{Mydat: d2}))
	expectNoErr(t, "w3 set", w3.SetOutValue(a2))
	if ok, err := w1.WaitInValueValid(5 * time.Second); err != nil || !ok {
		t.Errorf("w1 in value not valid: %v", err)
	}
	waitFor(t, "we2", we2, 5*time.Second)
	waitFor(t, "we3", we3, 5*time.Second)
	eventually(t, "w1 echo", time.Second, func() error {
		w1.SetOutValue(d1)
		time.Sleep(5 * time.Millisecond)
		v, _, ok, err := w1.TryGetInValue()
		if err != nil {
			return err
		}
		if !ok || !reflect.DeepEqual(v, d1) {
			return errors.New("w1 in value mismatch")
		}
		return nil
	})
	eventually(t, "w2 echo", time.Second, func() error {
		w2.SetOutValue(&ts1.Teststruct2{Mydat: d2})
		time.Sleep(5 * time.Millisecond)
		v, err := w2.InValue()
		if err != nil {
			return err
		}
		if !reflect.DeepEqual(v.Mydat, d2) {
			return errors.New("w2 in value mismatch")
		}
		return nil
	})
	eventually(t, "w3 echo", time.Second, func() error {
		w3.SetOutValue(a2)
		v, err := w3.InValue()
		if err != nil {
			return err
		}
		if !reflect.DeepEqual(v, a2) {
			return errors.New("w3 in value mismatch")
		}
		return nil
	})
	w1.SetInValueLifespan(time.Millisecond)
	time.Sleep(5 * time.Millisecond)
	if _, err := w1.InValue(); err == nil {
		t.Error("w1: expected expired in value")
	}
	expectNoErr(t, "w1 close", w1.Close())
	expectNoErr(t, "w3 close", w3.Close())
}

// TestMemories ports ServiceTestClient::test_m1 and test_m2.
func TestMemories(t *testing.T, r ts1.Testroot) {
	m1 := r.M1()
	eq(t, "m1 length", get(t, "m1 length", m1.Length), uint64(100))
	b := make([]float64, 11)
	expectNoErr(t, "m1 read", m1.Read(10, b, 1, 10))
	eq(t, "m1 read", b, tv.MustGet[float64]("ts1.client.m1_1a"))
	m12a := tv.MustGet[float64]("ts1.client.m1_2a")
	expectNoErr(t, "m1 write", m1.Write(20, m12a, 2, 8))
	m13 := make([]float64, 100)
	expectNoErr(t, "m1 read all", m1.Read(0, m13, 0, 100))
	m14 := tv.MustGet[float64]("ts1.m1.initial")
	copy(m14[20:28], m12a[2:10])
	eq(t, "m1 contents", m13, m14)

	m2 := r.M2()
	eq(t, "m2 dimcount", get(t, "m2 dimcount", m2.DimCount), uint64(5))
	eq(t, "m2 dims", get(t, "m2 dims", m2.Dimensions), []uint64{10, 10, 10, 10, 10})
	rng := rand.New(rand.NewSource(1))
	fill := func(dims ...uint32) *rr.MultiDimArray[float64] {
		a := rr.NewMultiDimArray[float64](dims...)
		for i := range a.Array {
			a.Array[i] = rng.Float64()
		}
		return a
	}
	all := []uint64{10, 10, 10, 10, 10}
	zero := []uint64{0, 0, 0, 0, 0}
	full := fill(10, 10, 10, 10, 10)
	expectNoErr(t, "m2 write", m2.Write(zero, full, zero, all))
	back := rr.NewMultiDimArray[float64](10, 10, 10, 10, 10)
	expectNoErr(t, "m2 read", m2.Read(zero, back, zero, all))
	eq(t, "m2 read all", back.Array, full.Array)
	// write a sub block and read it back with offsets
	sub := fill(2, 8, 8, 2, 2)
	expectNoErr(t, "m2 write sub", m2.Write([]uint64{2, 2, 3, 3, 4}, sub, []uint64{0, 2, 0, 0, 0}, []uint64{1, 5, 5, 2, 1}))
	blk := rr.NewMultiDimArray[float64](1, 5, 5, 2, 1)
	expectNoErr(t, "m2 read sub", m2.Read([]uint64{2, 2, 3, 3, 4}, blk, zero, []uint64{1, 5, 5, 2, 1}))
	// verify every element: blk[idx] == sub[idx + {0,2,0,0,0}] (column-major)
	cnt := []int{1, 5, 5, 2, 1}
	sd := []int{2, 8, 8, 2, 2}
	sp := []int{0, 2, 0, 0, 0}
	idx := make([]int, 5)
	for k := 0; k < len(blk.Array); k++ {
		r := k
		for d := 0; d < 5; d++ {
			idx[d] = r % cnt[d]
			r /= cnt[d]
		}
		so, m := 0, 1
		for d := 0; d < 5; d++ {
			so += (idx[d] + sp[d]) * m
			m *= sd[d]
		}
		if blk.Array[k] != sub.Array[so] {
			t.Errorf("m2 sub block mismatch at %v", idx)
			break
		}
	}
}

// TestAuthentication ports ServiceTestClient::TestAuthentication.
func TestAuthentication(t *testing.T, n *rr.Node, url string) {
	pw := func(p string) map[string]any { return map[string]any{"password": p} }
	r1 := Connect(t, n, url, rr.WithCredentials("testuser1", pw("testpass1")))
	_, err := r1.Func3(2.2, 3.3)
	expectNoErr(t, "testuser1 func3", err)
	n.DisconnectService(r1)
	r2 := Connect(t, n, url, rr.WithCredentials("testuser2", pw("testpass2")))
	_, err = r2.Func3(2.2, 3.3)
	expectNoErr(t, "testuser2 func3", err)
	n.DisconnectService(r2)
	if o, err := n.ConnectService(url, rr.WithCredentials("testuser2", pw("testpass1"))); err == nil {
		n.DisconnectService(o)
		t.Error("expected authentication error for wrong password")
	} else if !errors.Is(err, rr.ErrAuthenticationError) {
		t.Errorf("expected AuthenticationError, got %v", err)
	}
	if o, err := n.ConnectService(url); err == nil {
		_, err = o.(ts1.Testroot).Func3(2.2, 3.3)
		n.DisconnectService(o)
		if err == nil {
			t.Error("expected error without credentials")
		}
	}
}

// TestObjectLock ports the core of ServiceTestClient::TestObjectLock.
func TestObjectLock(t *testing.T, n *rr.Node, url string) {
	pw := func(p string) map[string]any { return map[string]any{"password": p} }
	r6 := Connect(t, n, url, rr.WithCredentials("testsuperuser", pw("superpass1")))
	r6o, err := r6.GetO1()
	must(t, err)
	n.ReleaseObjectLock(r6o)
	n.DisconnectService(r6)

	r1 := Connect(t, n, url, rr.WithCredentials("testuser1", pw("testpass1")))
	r2 := Connect(t, n, url, rr.WithCredentials("testuser1", pw("testpass1")))
	r3 := Connect(t, n, url, rr.WithCredentials("testuser2", pw("testpass2")))
	defer n.DisconnectService(r3)
	r1o, err := r1.GetO1()
	must(t, err)
	r2o, err := r2.GetO1()
	must(t, err)
	r3o, err := r3.GetO1()
	must(t, err)
	d := []float64{1}
	expectNoErr(t, "r1o", r1o.SetD1(d))
	expectNoErr(t, "r3o", r3o.SetD1(d))
	_, err = n.RequestObjectLock(r1o, rr.UserLock)
	expectNoErr(t, "lock", err)
	expectNoErr(t, "r1o locked", r1o.SetD1(d))
	expectNoErr(t, "r2o locked", r2o.SetD1(d))
	if err := r3o.SetD1(d); !errors.Is(err, rr.ErrObjectLockedError) {
		t.Errorf("r3o should be locked: %v", err)
	}
	_, err = n.ReleaseObjectLock(r1o)
	expectNoErr(t, "unlock", err)
	expectNoErr(t, "r3o unlocked", r3o.SetD1(d))
	// client lock
	_, err = n.RequestObjectLock(r1o, rr.ClientLock)
	expectNoErr(t, "client lock", err)
	expectNoErr(t, "r1o client locked", r1o.SetD1(d))
	if err := r2o.SetD1(d); err == nil {
		t.Error("r2o should be locked by client lock")
	}
	// superuser override
	r5 := Connect(t, n, url, rr.WithCredentials("testsuperuser", pw("superpass1")))
	r5o, err := r5.GetO1()
	must(t, err)
	_, err = n.ReleaseObjectLock(r5o)
	expectNoErr(t, "override release", err)
	expectNoErr(t, "r2o after override", r2o.SetD1(d))
	n.DisconnectService(r5)
	n.DisconnectService(r1)
	n.DisconnectService(r2)
}

// TestMonitorLock ports ServiceTestClient::TestMonitorLock.
func TestMonitorLock(t *testing.T, n *rr.Node, url string) {
	r1 := Connect(t, n, url)
	r2 := Connect(t, n, url)
	defer n.DisconnectService(r1)
	defer n.DisconnectService(r2)
	o1, err := r1.GetO1()
	must(t, err)
	o2, err := r2.GetO1()
	must(t, err)
	d := []float64{1}
	var mu sync.Mutex
	held := false
	t2 := false
	started := make(chan struct{})
	done := make(chan error, 1)
	err = n.WithMonitorLock(o1, rr.TimeoutInfinite, func() error {
		mu.Lock()
		held = true
		mu.Unlock()
		go func() {
			close(started)
			done <- n.WithMonitorLock(o2, rr.TimeoutInfinite, func() error {
				mu.Lock()
				defer mu.Unlock()
				if held {
					return errors.New("monitor lock was not exclusive")
				}
				t2 = true
				return o2.SetD1(d)
			})
		}()
		<-started
		time.Sleep(50 * time.Millisecond)
		if _, err := r1.Func3(2.2, 3.3); err != nil {
			return err
		}
		if err := o1.SetD1(d); err != nil {
			return err
		}
		mu.Lock()
		defer mu.Unlock()
		if t2 {
			return errors.New("second client entered the monitor while it was held")
		}
		held = false
		return nil
	})
	expectNoErr(t, "monitor lock 1", err)
	select {
	case err := <-done:
		expectNoErr(t, "monitor lock 2", err)
	case <-time.After(10 * time.Second):
		t.Error("second monitor lock did not complete")
	}
}

// TestAsync exercises asynchronous client calls.
func TestAsync(t *testing.T, r ts1.Testroot) {
	a := r.(ts1.AsyncTestroot)
	done := make(chan error, 8)
	a.AsyncGetD2(rr.TimeoutInfinite, func(v []float64, err error) {
		if err == nil && !reflect.DeepEqual(v, tv.MustGet[float64]("ts1.d2.to_client")) {
			err = errors.New("async d2 mismatch")
		}
		done <- err
	})
	a.AsyncSetD2(tv.MustGet[float64]("ts1.d2.to_service"), rr.TimeoutInfinite, func(err error) { done <- err })
	a.AsyncFunc3(2, 3.45, rr.TimeoutInfinite, func(v float64, err error) {
		if err == nil && v != 5.45 {
			err = errors.New("async func3 mismatch")
		}
		done <- err
	})
	a.AsyncFunc_errtest(rr.TimeoutInfinite, func(err error) {
		if err == nil {
			err = errors.New("expected error")
		} else {
			err = nil
		}
		done <- err
	})
	a.AsyncGetO1(rr.TimeoutInfinite, func(o ts1.Sub1, err error) { done <- err })
	for i := 0; i < 5; i++ {
		select {
		case err := <-done:
			expectNoErr(t, "async", err)
		case <-time.After(10 * time.Second):
			t.Fatal("async call timeout")
		}
	}
}

func sprint(v any) string { return fmt.Sprint(v) }
