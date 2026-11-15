package clienttests

// Go port of ServiceTestClient2 (TestService3).

import (
	"errors"
	"reflect"
	"testing"
	"time"
	"unsafe"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts3 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice3"
	ts "github.com/robotraconteur/robotraconteur_go/test/testservices"
	tv "github.com/robotraconteur/robotraconteur_go/test/testvectors"
)

func cd(v []float64) []complex128 {
	return append([]complex128(nil), unsafe.Slice((*complex128)(unsafe.Pointer(unsafe.SliceData(v))), len(v)/2)...)
}

func cs(v []float32) []complex64 {
	return append([]complex64(nil), unsafe.Slice((*complex64)(unsafe.Pointer(unsafe.SliceData(v))), len(v)/2)...)
}

// Connect3 connects to the TestService3 root.
func Connect3(t testing.TB, n *rr.Node, url string) ts3.Testroot3 {
	t.Helper()
	o, err := n.ConnectService(url)
	if err != nil {
		t.Fatalf("ConnectService(%s): %v", url, err)
	}
	r, ok := o.(ts3.Testroot3)
	if !ok {
		t.Fatalf("unexpected root type %T", o)
	}
	return r
}

// TestWirePeekPoke ports ServiceTestClient2::TestWirePeekPoke.
func TestWirePeekPoke(t *testing.T, r ts3.Testroot3) {
	v, _, err := r.Peekwire().PeekInValue()
	expectNoErr(t, "peek in", err)
	eq(t, "peekwire", v, int32(56295674))
	expectNoErr(t, "poke out", r.Pokewire().PokeOutValue(75738265))
	v2, _, err := r.Pokewire().PeekOutValue()
	expectNoErr(t, "peek out", err)
	eq(t, "pokewire", v2, int32(75738265))
	w, err := r.Pokewire().Connect()
	must(t, err)
	defer w.Close()
	for i := 0; i < 3; i++ {
		expectNoErr(t, "set out", w.SetOutValue(8638356))
	}
	time.Sleep(100 * time.Millisecond)
	v3, _, err := r.Pokewire().PeekOutValue()
	expectNoErr(t, "peek out 2", err)
	eq(t, "pokewire unicast", v3, int32(8638356))
}

// TestEnumsPods ports TestEnums and TestPods.
func TestEnumsPods(t *testing.T, r ts3.Testroot3) {
	eq(t, "testenum1_prop", get(t, "enum", r.GetTestenum1_prop), ts3.Testenum1_anothervalue)
	expectNoErr(t, "set enum", r.SetTestenum1_prop(ts3.Testenum1_hexval1))
	eq(t, "testpod1_prop", get(t, "pod", r.GetTestpod1_prop), ts.FillTestpod1(563921043))
	expectNoErr(t, "set pod", r.SetTestpod1_prop(ts.FillTestpod1(85932659)))
	eq(t, "testpod1_func2", get(t, "pod func2", r.Testpod1_func2), ts.FillTestpod1(95836295))
	expectNoErr(t, "testpod1_func1", r.Testpod1_func1(ts.FillTestpod1(29546592)))
	s3 := get(t, "teststruct3", r.GetTeststruct3_prop)
	want := ts.FillTeststruct3(16483675)
	if s3 != nil {
		s3c := *s3
		s3c.T4, s3c.T5 = want.T4, want.T5
		eq(t, "teststruct3", &s3c, want)
	}
	expectNoErr(t, "set teststruct3", r.SetTeststruct3_prop(ts.FillTeststruct3(858362)))
}

// TestPodMemories ports ServiceTestClient2::TestMemories.
func TestPodMemories(t *testing.T, r ts3.Testroot3) {
	s := make([]ts3.Testpod2, 32)
	for i := range s {
		s[i] = ts.FillTestpod2(uint32(59174 + i))
	}
	m1 := r.Pod_m1()
	eq(t, "pod_m1 length", get(t, "length", m1.Length), uint64(1024))
	expectNoErr(t, "pod_m1 write", m1.Write(52, s, 3, 17))
	s2 := make([]ts3.Testpod2, 32)
	expectNoErr(t, "pod_m1 read", m1.Read(53, s2, 2, 16))
	for i := 2; i < 16; i++ {
		eq(t, "pod_m1 element", s2[i], ts.FillTestpod2(uint32(59174+i+2)))
	}
	md := &rr.PodMultiDimArray[ts3.Testpod2]{Dims: []uint32{3, 3}, PodArray: make([]ts3.Testpod2, 9)}
	for i := range md.PodArray {
		md.PodArray[i] = ts.FillTestpod2(uint32(75721 + i))
	}
	z, c := []uint64{0, 0}, []uint64{3, 3}
	expectNoErr(t, "pod_m2 write", r.Pod_m2().Write(z, md, z, c))
	md2 := &rr.PodMultiDimArray[ts3.Testpod2]{Dims: []uint32{3, 3}, PodArray: make([]ts3.Testpod2, 9)}
	expectNoErr(t, "pod_m2 read", r.Pod_m2().Read(z, md2, z, c))
	eq(t, "pod_m2", md2.PodArray, md.PodArray)
}

// TestGenerators ports ServiceTestClient2::TestGenerators.
func TestGenerators(t *testing.T, r ts3.Testroot3) {
	a := []uint8{2, 3, 4}
	g, err := r.Gen_func4()
	must(t, err)
	for i := 0; i < 6; i++ {
		_, err := g.Next(a)
		expectNoErr(t, "gen next", err)
	}
	b, err := g.Next(a)
	expectNoErr(t, "gen next 7", err)
	eq(t, "gen value", b, []uint8{8, 9, 10})
	expectNoErr(t, "gen abort", g.Abort())
	if _, err := g.Next(a); !errors.Is(err, rr.ErrOperationAborted) {
		t.Errorf("expected OperationAborted, got %v", err)
	}
	g1, err := r.Gen_func1()
	must(t, err)
	all, err := g1.(*rr.Generator2Client[float64]).NextAll()
	expectNoErr(t, "next all", err)
	eq(t, "gen_func1", len(all), 16)
	g2, err := r.Gen_func4()
	must(t, err)
	_, err = g2.Next(a)
	expectNoErr(t, "gen2 next", err)
	expectNoErr(t, "gen2 close", g2.Close())
	if _, err := g2.Next(a); !rr.IsStopIteration(err) {
		t.Errorf("expected StopIteration, got %v", err)
	}
	g3, err := r.Gen_func2("x")
	must(t, err)
	for i := 0; i < 16; i++ {
		v, err := g3.Next()
		expectNoErr(t, "gen_func2", err)
		eq(t, "gen_func2 value", v, []uint8{uint8(i)})
	}
	if _, err := g3.Next(); !rr.IsStopIteration(err) {
		t.Errorf("gen_func2: expected StopIteration, got %v", err)
	}
}

// TestNamedArrays ports ServiceTestClient2::TestNamedArrays.
func TestNamedArrays(t *testing.T, r ts3.Testroot3) {
	expectNoErr(t, "set na1", r.SetTestnamedarray1(ts.FillTransform(3956378).Translation))
	eq(t, "na1", get(t, "na1", r.GetTestnamedarray1), ts.FillTransform(74637).Translation)
	expectNoErr(t, "set na2", r.SetTestnamedarray2(ts.FillTransform(827635)))
	eq(t, "na2", get(t, "na2", r.GetTestnamedarray2), ts.FillTransform(1294))
	expectNoErr(t, "set na3", r.SetTestnamedarray3(ts.FillTransformArray(6, 19274)))
	eq(t, "na3", get(t, "na3", r.GetTestnamedarray3), ts.FillTransformArray(8, 837512))
	expectNoErr(t, "set na4", r.SetTestnamedarray4(ts.FillTransformMultiDimArray(5, 2, 6385)))
	eq(t, "na4", get(t, "na4", r.GetTestnamedarray4), ts.FillTransformMultiDimArray(7, 2, 66134))
	expectNoErr(t, "set na5", r.SetTestnamedarray5(ts.FillTransformMultiDimArray(3, 2, 7732)))
	eq(t, "na5", get(t, "na5", r.GetTestnamedarray5), ts.FillTransformMultiDimArray(3, 2, 773142))

	s := make([]ts3.Transform, 32)
	for i := range s {
		s[i] = ts.FillTransform(uint32(79174 + i))
	}
	m1 := r.Namedarray_m1()
	eq(t, "namedarray_m1 length", get(t, "length", m1.Length), uint64(512))
	expectNoErr(t, "namedarray_m1 write", m1.Write(23, s, 3, 21))
	s2 := make([]ts3.Transform, 32)
	expectNoErr(t, "namedarray_m1 read", m1.Read(24, s2, 2, 18))
	for i := 2; i < 18; i++ {
		eq(t, "namedarray_m1 element", s2[i], ts.FillTransform(uint32(79174+i+2)))
	}
	md := &rr.NamedMultiDimArray[ts3.Transform]{Dims: []uint32{3, 3}, NamedArrayArray: make([]ts3.Transform, 9)}
	for i := range md.NamedArrayArray {
		md.NamedArrayArray[i] = ts.FillTransform(uint32(15721 + i))
	}
	z, c := []uint64{0, 0}, []uint64{3, 3}
	expectNoErr(t, "namedarray_m2 write", r.Namedarray_m2().Write(z, md, z, c))
	md2 := &rr.NamedMultiDimArray[ts3.Transform]{Dims: []uint32{3, 3}, NamedArrayArray: make([]ts3.Transform, 9)}
	expectNoErr(t, "namedarray_m2 read", r.Namedarray_m2().Read(z, md2, z, c))
	eq(t, "namedarray_m2", md2.NamedArrayArray, md.NamedArrayArray)
}

// TestComplex ports ServiceTestClient2::TestComplex and TestComplexMemories.
func TestComplex(t *testing.T, r ts3.Testroot3) {
	v := tv.MustGet[float64]
	f := tv.MustGet[float32]
	u := tv.MustGet[uint32]
	eq(t, "c1", get(t, "c1", r.GetC1), complex(5.708705e+01, -2.328294e-03))
	expectNoErr(t, "set c1", r.SetC1(complex(5.708705e+01, -2.328294e-03)))
	eq(t, "c2", get(t, "c2", r.GetC2), cd(v("ts3.c2.to_client")))
	expectNoErr(t, "set c2", r.SetC2(cd(v("ts3.c2.to_service"))))
	eq(t, "c3", get(t, "c3", r.GetC3), &rr.MultiDimArray[complex128]{Dims: u("ts3.c3.to_client.dims"), Array: cd(v("ts3.c3.to_client"))})
	expectNoErr(t, "set c3", r.SetC3(&rr.MultiDimArray[complex128]{Dims: u("ts3.c3.to_service.dims"), Array: cd(v("ts3.c3.to_service"))}))
	eq(t, "c5", get(t, "c5", r.GetC5), [][]complex128{cd(v("ts3.c5.to_client"))})
	expectNoErr(t, "set c5", r.SetC5([][]complex128{cd(v("ts3.c5.to_service"))}))
	eq(t, "c7", get(t, "c7", r.GetC7), complex64(complex(-5.527021e-18, -9.848457e+03)))
	expectNoErr(t, "set c7", r.SetC7(complex64(complex(9.303345e-12, -3.865684e-05))))
	eq(t, "c8", get(t, "c8", r.GetC8), cs(f("ts3.c8.to_client")))
	expectNoErr(t, "set c8", r.SetC8(cs(f("ts3.c8.to_service"))))
	eq(t, "c9", get(t, "c9", r.GetC9), &rr.MultiDimArray[complex64]{Dims: u("ts3.c9.to_client.dims"), Array: cs(f("ts3.c9.to_client"))})
	expectNoErr(t, "set c9", r.SetC9(&rr.MultiDimArray[complex64]{Dims: u("ts3.c9.to_service.dims"), Array: cs(f("ts3.c9.to_service"))}))

	cm1 := cd(v("ts3.client.c_m1_1"))
	expectNoErr(t, "c_m1 write", r.C_m1().Write(10, cm1, 0, 8))
	cm1b := make([]complex128, 8)
	expectNoErr(t, "c_m1 read", r.C_m1().Read(10, cm1b, 0, 8))
	eq(t, "c_m1", cm1b, cm1[:8])
	cm2 := &rr.MultiDimArray[complex128]{Dims: u("ts3.client.c_m2_2.dims"), Array: cd(v("ts3.client.c_m2_2"))}
	z, c := []uint64{0, 0}, []uint64{3, 3}
	expectNoErr(t, "c_m2 write", r.C_m2().Write(z, cm2, z, c))
	cm2b := &rr.MultiDimArray[complex128]{Dims: cm2.Dims, Array: make([]complex128, 9)}
	expectNoErr(t, "c_m2 read", r.C_m2().Read(z, cm2b, z, c))
	eq(t, "c_m2", cm2b, cm2)
}

// TestBools ports ServiceTestClient2::TestBools and TestBoolMemories.
func TestBools(t *testing.T, r ts3.Testroot3) {
	eq(t, "b1", get(t, "b1", r.GetB1), true)
	expectNoErr(t, "set b1", r.SetB1(true))
	eq(t, "b2", get(t, "b2", r.GetB2), []bool{true, false, true, true, false, true, false})
	expectNoErr(t, "set b2", r.SetB2([]bool{true, false, false, true, true, true, false, true}))
	eq(t, "b3", get(t, "b3", r.GetB3), &rr.MultiDimArray[bool]{Dims: []uint32{2, 2}, Array: []bool{false, true, true, false}})
	expectNoErr(t, "set b3", r.SetB3(&rr.MultiDimArray[bool]{Dims: []uint32{2, 1}, Array: []bool{true, false}}))
	eq(t, "b4", get(t, "b4", r.GetB4), []bool{true})
	expectNoErr(t, "set b4", r.SetB4([]bool{true}))
	eq(t, "b5", get(t, "b5", r.GetB5), [][]bool{{false, true, false, false}})
	expectNoErr(t, "set b5", r.SetB5([][]bool{{true, false}}))
	eq(t, "b6", get(t, "b6", r.GetB6), []*rr.MultiDimArray[bool]{{Dims: []uint32{2, 2}, Array: []bool{false, true, true, false}}})
	expectNoErr(t, "set b6", r.SetB6([]*rr.MultiDimArray[bool]{{Dims: []uint32{2, 1}, Array: []bool{true, false}}}))
	b := []bool{true, false, true, true, false, true, false, true}
	expectNoErr(t, "c_m5 write", r.C_m5().Write(100, b, 1, 6))
	b2 := make([]bool, 8)
	expectNoErr(t, "c_m5 read", r.C_m5().Read(100, b2, 0, 6))
	eq(t, "c_m5", b2[:6], b[1:7])
	md := &rr.MultiDimArray[bool]{Dims: []uint32{2, 2}, Array: []bool{true, false, false, true}}
	z, c := []uint64{0, 0}, []uint64{2, 2}
	expectNoErr(t, "c_m6 write", r.C_m6().Write(z, md, z, c))
	md2 := &rr.MultiDimArray[bool]{Dims: []uint32{2, 2}, Array: make([]bool, 4)}
	expectNoErr(t, "c_m6 read", r.C_m6().Read(z, md2, z, c))
	eq(t, "c_m6", md2, md)
}

// TestNoLock ports ServiceTestClient2::TestNoLock.
func TestNoLock(t *testing.T, r ts3.Testroot3) {
	o5, err := r.GetNolock_test()
	must(t, err)
	locked := func(name string, err error) {
		t.Helper()
		if !errors.Is(err, rr.ErrObjectLockedError) {
			t.Errorf("%s: expected ObjectLockedError, got %v", name, err)
		}
	}
	_, err = o5.GetP1()
	locked("p1", err)
	_, err = o5.GetP2()
	expectNoErr(t, "p2", err)
	expectNoErr(t, "set p2", o5.SetP2(0))
	_, err = o5.GetP3()
	expectNoErr(t, "p3", err)
	locked("set p1", o5.SetP1(0))
	_, err = o5.F1()
	locked("f1", err)
	_, err = o5.F2()
	expectNoErr(t, "f2", err)
	_, err = o5.Q1().Connect(-1)
	locked("q1", err)
	q2, err := o5.Q2().Connect(-1)
	expectNoErr(t, "q2", err)
	if q2 != nil {
		q2.Close()
	}
	_, err = o5.W1().Connect()
	locked("w1", err)
	w2, err := o5.W2().Connect()
	expectNoErr(t, "w2", err)
	if w2 != nil {
		w2.Close()
	}
	_, err = o5.M1().Length()
	locked("m1", err)
	b := make([]int32, 100)
	_, err = o5.M2().Length()
	expectNoErr(t, "m2 length", err)
	expectNoErr(t, "m2 read", o5.M2().Read(0, b, 0, 10))
	expectNoErr(t, "m2 write", o5.M2().Write(0, b, 0, 10))
	expectNoErr(t, "m3 read", o5.M3().Read(0, b, 0, 10))
	locked("m3 write", o5.M3().Write(0, b, 0, 10))
}

// TestExceptionParams ports ServiceTestClient2::TestExceptionParams.
func TestExceptionParams(t *testing.T, r ts3.Testroot3) {
	err := r.Test_exception_params1()
	var e *rr.Error
	if !errors.Is(err, rr.ErrInvalidOperation) || !errors.As(err, &e) {
		t.Fatalf("expected InvalidOperation, got %v", err)
	}
	eq(t, "message", e.Message, "test error")
	eq(t, "subname", e.SubName, "my_error")
	eq(t, "param", e.Param, any(map[string]any{"param1": []int32{10}, "param2": "20"}))
	err = r.Test_exception_params2()
	var e4 *ts3.Test_exception4
	if !errors.As(err, &e4) {
		t.Fatalf("expected test_exception4, got %T %v", err, err)
	}
	eq(t, "message 2", e4.E.Message, "test error2")
	eq(t, "subname 2", e4.E.SubName, "my_error2")
	if !reflect.DeepEqual(e4.E.Param, any(map[string]any{"param1": []int32{30}, "param2": "40"})) {
		t.Errorf("param 2: %v", e4.E.Param)
	}
}

// RunTestService3 runs the TestService3 client tests against
// RobotRaconteurTestService2 at baseURL.
func RunTestService3(t *testing.T, n *rr.Node, baseURL string) {
	url := baseURL + "?service=RobotRaconteurTestService2"
	for _, tc := range []struct {
		name string
		f    func(*testing.T, ts3.Testroot3)
	}{
		{"WirePeekPoke", TestWirePeekPoke}, {"EnumsPods", TestEnumsPods}, {"PodMemories", TestPodMemories},
		{"Generators", TestGenerators}, {"NamedArrays", TestNamedArrays}, {"Complex", TestComplex},
		{"Bools", TestBools}, {"NoLock", TestNoLock}, {"ExceptionParams", TestExceptionParams},
	} {
		t.Run(tc.name, func(t *testing.T) {
			r := Connect3(t, n, url)
			defer n.DisconnectService(r)
			tc.f(t, r)
		})
	}
}
