package testservices

// Go port of testroot3_impl (robotraconteur/test/cpp/service/ServiceTest2.cpp).

import (
	"context"
	"sync"
	"time"
	"unsafe"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts1 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice1"
	ts3 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice3"
)

// ---- deterministic test sequences (ServiceTest2_test_sequence_gen)

var (
	seqDoubles = vec[float64]("seq.double_constants")
	seqUint32s = vec[uint32]("seq.uint32_constants")
)

// SeqGen is the Go port of ServiceTest2_test_sequence_gen.
type SeqGen struct{ counter, step uint32 }

// NewSeqGen creates a sequence generator.
func NewSeqGen(seed uint32) *SeqGen { return &SeqGen{counter: seed % 7823, step: seed%17 + 7} }

func (g *SeqGen) Double() float64 {
	g.counter += g.step
	return seqDoubles[g.counter%uint32(len(seqDoubles))]
}

func (g *SeqGen) Uint32() uint32 {
	g.counter += g.step
	return seqUint32s[g.counter%uint32(len(seqUint32s))]
}

func (g *SeqGen) Int8() int8 {
	g.counter += g.step
	return int8(int32(g.counter%0xFF) - 128)
}

// FillTestpod2 ports ServiceTest2_fill_testpod2.
func FillTestpod2(seed uint32) ts3.Testpod2 {
	g := NewSeqGen(seed)
	var v ts3.Testpod2
	v.I1 = g.Int8()
	for i := range v.I2 {
		v.I2[i] = g.Int8()
	}
	n := g.Uint32() % 15
	v.I3 = make([]int8, n)
	for i := range v.I3 {
		v.I3[i] = g.Int8()
	}
	return v
}

// FillTransform ports ServiceTest2_fill_transform.
func FillTransform(seed uint32) ts3.Transform {
	g := NewSeqGen(seed)
	a := make([]float64, 7)
	for i := range a {
		a[i] = g.Double()
	}
	var t ts3.Transform
	t.RRFromArray(a)
	return t
}

// FillTransformArray ports ServiceTest2_fill_transform_array.
func FillTransformArray(n int, seed uint32) []ts3.Transform {
	g := NewSeqGen(seed)
	o := make([]ts3.Transform, n)
	for i := range o {
		o[i] = FillTransform(g.Uint32())
	}
	return o
}

// FillTransformMultiDimArray ports ServiceTest2_fill_transform_multidimarray.
func FillTransformMultiDimArray(m, n int, seed uint32) *rr.NamedMultiDimArray[ts3.Transform] {
	return &rr.NamedMultiDimArray[ts3.Transform]{Dims: []uint32{uint32(m), uint32(n)}, NamedArrayArray: FillTransformArray(m*n, seed)}
}

// FillTestpod1 ports ServiceTest2_fill_testpod1.
func FillTestpod1(seed uint32) ts3.Testpod1 {
	g := NewSeqGen(seed)
	var v ts3.Testpod1
	v.D1 = g.Double()
	for i := range v.D2 {
		v.D2[i] = g.Double()
	}
	n := g.Uint32() % 6
	v.D3 = make([]float64, n)
	for i := range v.D3 {
		v.D3[i] = g.Double()
	}
	for i := range v.D4 {
		v.D4[i] = g.Double()
	}
	v.S1 = FillTestpod2(g.Uint32())
	for i := range v.S2 {
		v.S2[i] = FillTestpod2(g.Uint32())
	}
	n = g.Uint32() % 9
	v.S3 = make([]ts3.Testpod2, n)
	for i := range v.S3 {
		v.S3[i] = FillTestpod2(g.Uint32())
	}
	for i := range v.S4 {
		v.S4[i] = FillTestpod2(g.Uint32())
	}
	v.T1 = FillTransform(g.Uint32())
	for i := range v.T2 {
		v.T2[i] = FillTransform(g.Uint32())
	}
	n = g.Uint32() % 15
	v.T3 = make([]ts3.Transform, n)
	for i := range v.T3 {
		v.T3[i] = FillTransform(g.Uint32())
	}
	for i := range v.T4 {
		v.T4[i] = FillTransform(g.Uint32())
	}
	return v
}

// FillTestpod1Array ports ServiceTest2_fill_testpod1_array.
func FillTestpod1Array(n int, seed uint32) []ts3.Testpod1 {
	g := NewSeqGen(seed)
	o := make([]ts3.Testpod1, n)
	for i := range o {
		o[i] = FillTestpod1(g.Uint32())
	}
	return o
}

// FillTestpod1MultiDimArray ports ServiceTest2_fill_testpod1_multidimarray.
func FillTestpod1MultiDimArray(m, n int, seed uint32) *rr.PodMultiDimArray[ts3.Testpod1] {
	return &rr.PodMultiDimArray[ts3.Testpod1]{Dims: []uint32{uint32(m), uint32(n)}, PodArray: FillTestpod1Array(m*n, seed)}
}

// FillTeststruct3 ports ServiceTest2_fill_teststruct3. Varvalue fields hold
// the Go representation produced by unpacking (pods as slices).
func FillTeststruct3(seed uint32) *ts3.Teststruct3 {
	g := NewSeqGen(seed)
	o := &ts3.Teststruct3{}
	o.S1 = FillTestpod1(g.Uint32())
	s2 := g.Uint32()
	o.S2 = FillTestpod1Array(int(s2%17), s2)
	o.S3 = FillTestpod1Array(11, g.Uint32())
	s4 := g.Uint32()
	o.S4 = FillTestpod1Array(int(s4%16), s4)
	o.S5 = FillTestpod1MultiDimArray(3, 3, g.Uint32())
	s6 := g.Uint32()
	o.S6 = FillTestpod1MultiDimArray(int(s6%6), int(s6%3), s6)
	o.S7 = []ts3.Testpod1{FillTestpod1(g.Uint32())}
	o.S8 = [][]ts3.Testpod1{FillTestpod1Array(2, g.Uint32()), FillTestpod1Array(4, g.Uint32())}
	o.S9 = []*rr.PodMultiDimArray[ts3.Testpod1]{FillTestpod1MultiDimArray(2, 3, g.Uint32()), FillTestpod1MultiDimArray(4, 5, g.Uint32())}
	o.S10 = []ts3.Testpod1{FillTestpod1(g.Uint32())}
	o.S11 = FillTestpod1Array(3, g.Uint32())
	o.S12 = FillTestpod1MultiDimArray(2, 2, g.Uint32())
	o.S13 = []any{[]ts3.Testpod1{FillTestpod1(g.Uint32())}}
	o.S14 = []any{FillTestpod1Array(3, g.Uint32()), FillTestpod1Array(5, g.Uint32())}
	o.S15 = []any{FillTestpod1MultiDimArray(7, 2, g.Uint32()), FillTestpod1MultiDimArray(5, 1, g.Uint32())}
	o.T1 = FillTransform(g.Uint32())
	o.T2 = FillTransformArray(4, g.Uint32())
	o.T3 = FillTransformMultiDimArray(2, 4, g.Uint32())
	o.T4 = FillTransformArray(10, g.Uint32())
	o.T5 = FillTransformMultiDimArray(6, 5, g.Uint32())
	o.T6 = []ts3.Transform{FillTransform(g.Uint32())}
	o.T7 = [][]ts3.Transform{FillTransformArray(4, g.Uint32()), FillTransformArray(4, g.Uint32())}
	o.T8 = []*rr.NamedMultiDimArray[ts3.Transform]{FillTransformMultiDimArray(2, 4, g.Uint32()), FillTransformMultiDimArray(2, 4, g.Uint32())}
	o.T9 = []any{[]ts3.Transform{FillTransform(g.Uint32())}}
	o.T10 = []any{FillTransformArray(3, g.Uint32()), FillTransformArray(5, g.Uint32())}
	o.T11 = []any{FillTransformMultiDimArray(7, 2, g.Uint32()), FillTransformMultiDimArray(5, 1, g.Uint32())}
	return o
}

// normalizePods replaces nil slices in pods with empty slices (received pods
// always have non-nil slices) so DeepEqual comparisons are meaningful.
func cdoubles(v []float64) []complex128 {
	return append([]complex128(nil), unsafe.Slice((*complex128)(unsafe.Pointer(unsafe.SliceData(v))), len(v)/2)...)
}

func csingles(v []float32) []complex64 {
	return append([]complex64(nil), unsafe.Slice((*complex64)(unsafe.Pointer(unsafe.SliceData(v))), len(v)/2)...)
}

// ---- testroot3

// Testroot3 implements com.robotraconteur.testing.TestService3.testroot3.
type Testroot3 struct {
	ts3.Testroot3DefaultImpl
	podM1   *rr.SlicePodArrayMemory[ts3.Testpod2]
	podM2   *rr.SlicePodMultiDimArrayMemory[ts3.Testpod2]
	namedM1 *rr.SlicePodArrayMemory[ts3.Transform]
	namedM2 *rr.SliceNamedMultiDimArrayMemory[ts3.Transform]
	cM1     *rr.SliceArrayMemory[complex128]
	cM2     *rr.SliceMultiDimArrayMemory[complex128]
	cM5     *rr.SliceArrayMemory[bool]
	cM6     *rr.SliceMultiDimArrayMemory[bool]
	obj5    *obj5Impl
	stop    chan struct{}
}

func zeroPods(n int) []ts3.Testpod2 {
	o := make([]ts3.Testpod2, n)
	for i := range o {
		o[i].I3 = []int8{}
	}
	return o
}

// NewTestroot3 creates the TestService3 root object.
func NewTestroot3() *Testroot3 {
	return &Testroot3{
		podM1:   rr.NewPodArrayMemory(zeroPods(1024)),
		podM2:   rr.NewPodMultiDimArrayMemory(&rr.PodMultiDimArray[ts3.Testpod2]{Dims: []uint32{6, 6}, PodArray: zeroPods(36)}),
		namedM1: rr.NewNamedArrayMemory(make([]ts3.Transform, 512)),
		namedM2: rr.NewNamedMultiDimArrayMemory(&rr.NamedMultiDimArray[ts3.Transform]{Dims: []uint32{10, 20}, NamedArrayArray: make([]ts3.Transform, 200)}),
		cM1:     rr.NewArrayMemory(make([]complex128, 512)),
		cM2:     rr.NewMultiDimArrayMemory(&rr.MultiDimArray[complex128]{Dims: []uint32{10, 10}, Array: make([]complex128, 100)}),
		cM5:     rr.NewArrayMemory(make([]bool, 512)),
		cM6:     rr.NewMultiDimArrayMemory(&rr.MultiDimArray[bool]{Dims: []uint32{10, 10}, Array: make([]bool, 100)}),
		obj5:    newObj5(),
		stop:    make(chan struct{}),
	}
}

// Shutdown stops the timers.
func (t *Testroot3) Shutdown() {
	select {
	case <-t.stop:
	default:
		close(t.stop)
	}
}

func (t *Testroot3) Set_peekwire(w *rr.Wire[int32]) {
	t.Testroot3DefaultImpl.Set_peekwire(w)
	b := t.RRBroadcaster_peekwire
	if b == nil {
		return
	}
	b.SetOutValue(56295674)
	go func() {
		tk := time.NewTicker(100 * time.Millisecond)
		defer tk.Stop()
		for {
			select {
			case <-t.stop:
				return
			case <-tk.C:
				b.SetOutValue(56295674)
			}
		}
	}()
}

func (t *Testroot3) GetUnknown_modifier() (int32, error)            { return 0, nil }
func (t *Testroot3) SetUnknown_modifier(int32) error                { return nil }
func (t *Testroot3) Get_readme(ctx context.Context) (int32, error)  { return 0, nil }
func (t *Testroot3) Set_writeme(ctx context.Context, _ int32) error { return nil }
func (t *Testroot3) Get_readmem() rr.ArrayMemory[float64]           { return nil }
func (t *Testroot3) Get_o4(ctx context.Context) (ts3.Obj4, error)   { return &obj4Impl{}, nil }

func (t *Testroot3) Get_testenum1_prop(ctx context.Context) (ts3.Testenum1, error) {
	return ts3.Testenum1_anothervalue, nil
}
func (t *Testroot3) Set_testenum1_prop(ctx context.Context, v ts3.Testenum1) error {
	return check("testenum1_prop", v, ts3.Testenum1_hexval1)
}

func (t *Testroot3) Get_testpod1_prop(ctx context.Context) (ts3.Testpod1, error) {
	return FillTestpod1(563921043), nil
}
func (t *Testroot3) Set_testpod1_prop(ctx context.Context, v ts3.Testpod1) error {
	return check("testpod1_prop", v, FillTestpod1(85932659))
}
func (t *Testroot3) Testpod1_func1(ctx context.Context, s ts3.Testpod1) error {
	return check("testpod1_func1", s, FillTestpod1(29546592))
}
func (t *Testroot3) Testpod1_func2(ctx context.Context) (ts3.Testpod1, error) {
	return FillTestpod1(95836295), nil
}

func (t *Testroot3) Get_teststruct3_prop(ctx context.Context) (*ts3.Teststruct3, error) {
	return FillTeststruct3(16483675), nil
}
func (t *Testroot3) Set_teststruct3_prop(ctx context.Context, v *ts3.Teststruct3) error {
	want := FillTeststruct3(858362)
	if v == nil {
		return rr.NewNullValue("teststruct3 is null")
	}
	// t4 and t5 are not verified by the C++ implementation either
	v2 := *v
	v2.T4, v2.T5 = want.T4, want.T5
	return check("teststruct3_prop", &v2, want)
}

func (t *Testroot3) Get_pod_m1() rr.PodArrayMemory[ts3.Testpod2]         { return t.podM1 }
func (t *Testroot3) Get_pod_m2() rr.PodMultiDimArrayMemory[ts3.Testpod2] { return t.podM2 }

type func4Gen struct {
	mu      sync.Mutex
	j       int
	aborted bool
}

func (g *func4Gen) Next(_ context.Context, v []uint8) ([]uint8, error) {
	g.mu.Lock()
	defer g.mu.Unlock()
	if g.aborted {
		return nil, rr.NewOperationAborted("")
	}
	if g.j >= 8 {
		return nil, rr.NewStopIteration("")
	}
	a := make([]uint8, len(v))
	for i := range v {
		a[i] = v[i] + uint8(g.j)
	}
	g.j++
	return a, nil
}
func (g *func4Gen) Abort(context.Context) error {
	g.mu.Lock()
	g.aborted = true
	g.mu.Unlock()
	return nil
}
func (g *func4Gen) Close(context.Context) error { g.mu.Lock(); g.j = 1000; g.mu.Unlock(); return nil }

func (t *Testroot3) Gen_func4(ctx context.Context) (rr.Generator1[[]uint8, []uint8], error) {
	return &func4Gen{}, nil
}

type func1Gen struct {
	mu      sync.Mutex
	j       float64
	aborted bool
}

func (g *func1Gen) Next(context.Context) (float64, error) {
	g.mu.Lock()
	defer g.mu.Unlock()
	if g.aborted {
		return 0, rr.NewOperationAborted("")
	}
	if g.j >= 16 {
		return 0, rr.NewStopIteration("")
	}
	v := g.j
	g.j++
	return v, nil
}
func (g *func1Gen) Abort(context.Context) error {
	g.mu.Lock()
	g.aborted = true
	g.mu.Unlock()
	return nil
}
func (g *func1Gen) Close(context.Context) error { g.mu.Lock(); g.j = 1000; g.mu.Unlock(); return nil }

func (t *Testroot3) Gen_func1(ctx context.Context) (rr.Generator2[float64], error) {
	return &func1Gen{}, nil
}
func (t *Testroot3) Gen_func2(ctx context.Context, name string) (rr.Generator2[[]uint8], error) {
	r := make([][]uint8, 16)
	for i := range r {
		r[i] = []uint8{uint8(i)}
	}
	return rr.NewSliceGenerator2(r), nil
}
func (t *Testroot3) Gen_func3(ctx context.Context, name string) (rr.Generator3[[]uint8], error) {
	return nil, nil
}
func (t *Testroot3) Gen_func5(ctx context.Context) (rr.Generator1[*ts1.Teststruct2, *ts1.Teststruct2], error) {
	return nil, nil
}

func (t *Testroot3) Get_testnamedarray1(ctx context.Context) (ts3.Vector3, error) {
	return FillTransform(74637).Translation, nil
}
func (t *Testroot3) Set_testnamedarray1(ctx context.Context, v ts3.Vector3) error {
	return check("testnamedarray1", v, FillTransform(3956378).Translation)
}
func (t *Testroot3) Get_testnamedarray2(ctx context.Context) (ts3.Transform, error) {
	return FillTransform(1294), nil
}
func (t *Testroot3) Set_testnamedarray2(ctx context.Context, v ts3.Transform) error {
	return check("testnamedarray2", v, FillTransform(827635))
}
func (t *Testroot3) Get_testnamedarray3(ctx context.Context) ([]ts3.Transform, error) {
	return FillTransformArray(8, 837512), nil
}
func (t *Testroot3) Set_testnamedarray3(ctx context.Context, v []ts3.Transform) error {
	return check("testnamedarray3", v, FillTransformArray(6, 19274))
}
func (t *Testroot3) Get_testnamedarray4(ctx context.Context) (*rr.NamedMultiDimArray[ts3.Transform], error) {
	return FillTransformMultiDimArray(7, 2, 66134), nil
}
func (t *Testroot3) Set_testnamedarray4(ctx context.Context, v *rr.NamedMultiDimArray[ts3.Transform]) error {
	return check("testnamedarray4", v, FillTransformMultiDimArray(5, 2, 6385))
}
func (t *Testroot3) Get_testnamedarray5(ctx context.Context) (*rr.NamedMultiDimArray[ts3.Transform], error) {
	return FillTransformMultiDimArray(3, 2, 773142), nil
}
func (t *Testroot3) Set_testnamedarray5(ctx context.Context, v *rr.NamedMultiDimArray[ts3.Transform]) error {
	return check("testnamedarray5", v, FillTransformMultiDimArray(3, 2, 7732))
}
func (t *Testroot3) Get_namedarray_m1() rr.NamedArrayMemory[ts3.Transform]         { return t.namedM1 }
func (t *Testroot3) Get_namedarray_m2() rr.NamedMultiDimArrayMemory[ts3.Transform] { return t.namedM2 }

func (t *Testroot3) Get_c1(ctx context.Context) (complex128, error) {
	return complex(5.708705e+01, -2.328294e-03), nil
}
func (t *Testroot3) Set_c1(ctx context.Context, v complex128) error {
	return check("c1", v, complex(5.708705e+01, -2.328294e-03))
}
func (t *Testroot3) Get_c2(ctx context.Context) ([]complex128, error) {
	return cdoubles(vec[float64]("ts3.c2.to_client")), nil
}
func (t *Testroot3) Set_c2(ctx context.Context, v []complex128) error {
	return check("c2", v, cdoubles(vec[float64]("ts3.c2.to_service")))
}
func (t *Testroot3) Get_c3(ctx context.Context) (*rr.MultiDimArray[complex128], error) {
	return &rr.MultiDimArray[complex128]{Dims: vec[uint32]("ts3.c3.to_client.dims"), Array: cdoubles(vec[float64]("ts3.c3.to_client"))}, nil
}
func (t *Testroot3) Set_c3(ctx context.Context, v *rr.MultiDimArray[complex128]) error {
	return check("c3", v, &rr.MultiDimArray[complex128]{Dims: vec[uint32]("ts3.c3.to_service.dims"), Array: cdoubles(vec[float64]("ts3.c3.to_service"))})
}
func (t *Testroot3) Get_c5(ctx context.Context) ([][]complex128, error) {
	return [][]complex128{cdoubles(vec[float64]("ts3.c5.to_client"))}, nil
}
func (t *Testroot3) Set_c5(ctx context.Context, v [][]complex128) error {
	return check("c5", v, [][]complex128{cdoubles(vec[float64]("ts3.c5.to_service"))})
}
func (t *Testroot3) Get_c7(ctx context.Context) (complex64, error) {
	return complex64(complex(-5.527021e-18, -9.848457e+03)), nil
}
func (t *Testroot3) Set_c7(ctx context.Context, v complex64) error {
	return check("c7", v, complex64(complex(9.303345e-12, -3.865684e-05)))
}
func (t *Testroot3) Get_c8(ctx context.Context) ([]complex64, error) {
	return csingles(vec[float32]("ts3.c8.to_client")), nil
}
func (t *Testroot3) Set_c8(ctx context.Context, v []complex64) error {
	return check("c8", v, csingles(vec[float32]("ts3.c8.to_service")))
}
func (t *Testroot3) Get_c9(ctx context.Context) (*rr.MultiDimArray[complex64], error) {
	return &rr.MultiDimArray[complex64]{Dims: vec[uint32]("ts3.c9.to_client.dims"), Array: csingles(vec[float32]("ts3.c9.to_client"))}, nil
}
func (t *Testroot3) Set_c9(ctx context.Context, v *rr.MultiDimArray[complex64]) error {
	return check("c9", v, &rr.MultiDimArray[complex64]{Dims: vec[uint32]("ts3.c9.to_service.dims"), Array: csingles(vec[float32]("ts3.c9.to_service"))})
}
func (t *Testroot3) Get_c_m1() rr.ArrayMemory[complex128]         { return t.cM1 }
func (t *Testroot3) Get_c_m2() rr.MultiDimArrayMemory[complex128] { return t.cM2 }
func (t *Testroot3) Get_c_m5() rr.ArrayMemory[bool]               { return t.cM5 }
func (t *Testroot3) Get_c_m6() rr.MultiDimArrayMemory[bool]       { return t.cM6 }

func (t *Testroot3) Get_nolock_test(ctx context.Context) (ts3.Obj5, error) { return t.obj5, nil }

func (t *Testroot3) Get_b1(ctx context.Context) (bool, error) { return true, nil }
func (t *Testroot3) Set_b1(ctx context.Context, v bool) error { return check("b1", v, true) }
func (t *Testroot3) Get_b2(ctx context.Context) ([]bool, error) {
	return []bool{true, false, true, true, false, true, false}, nil
}
func (t *Testroot3) Set_b2(ctx context.Context, v []bool) error {
	return check("b2", v, []bool{true, false, false, true, true, true, false, true})
}
func (t *Testroot3) Get_b3(ctx context.Context) (*rr.MultiDimArray[bool], error) {
	return &rr.MultiDimArray[bool]{Dims: []uint32{2, 2}, Array: []bool{false, true, true, false}}, nil
}
func (t *Testroot3) Set_b3(ctx context.Context, v *rr.MultiDimArray[bool]) error {
	return check("b3", v, &rr.MultiDimArray[bool]{Dims: []uint32{2, 1}, Array: []bool{true, false}})
}
func (t *Testroot3) Get_b4(ctx context.Context) ([]bool, error) { return []bool{true}, nil }
func (t *Testroot3) Set_b4(ctx context.Context, v []bool) error { return check("b4", v, []bool{true}) }
func (t *Testroot3) Get_b5(ctx context.Context) ([][]bool, error) {
	return [][]bool{{false, true, false, false}}, nil
}
func (t *Testroot3) Set_b5(ctx context.Context, v [][]bool) error {
	return check("b5", v, [][]bool{{true, false}})
}
func (t *Testroot3) Get_b6(ctx context.Context) ([]*rr.MultiDimArray[bool], error) {
	return []*rr.MultiDimArray[bool]{{Dims: []uint32{2, 2}, Array: []bool{false, true, true, false}}}, nil
}
func (t *Testroot3) Set_b6(ctx context.Context, v []*rr.MultiDimArray[bool]) error {
	return check("b6", v, []*rr.MultiDimArray[bool]{{Dims: []uint32{2, 1}, Array: []bool{true, false}}})
}

func (t *Testroot3) Test_exception_params1(ctx context.Context) error {
	e := rr.NewInvalidOperation("test error")
	e.SubName = "my_error"
	e.Param = map[string]any{"param1": int32(10), "param2": "20"}
	return e
}

func (t *Testroot3) Test_exception_params2(ctx context.Context) error {
	e := ts3.NewTest_exception4("test error2")
	e.E.SubName = "my_error2"
	e.E.Param = map[string]any{"param1": int32(30), "param2": "40"}
	return e
}

func (t *Testroot3) Enum_generator1(ctx context.Context) (rr.Generator2[ts3.Testenum1], error) {
	return rr.NewSliceGenerator2([]ts3.Testenum1{ts3.Testenum1_anothervalue, ts3.Testenum1_hexval1}), nil
}

func (t *Testroot3) Enum_generator2(ctx context.Context, a, b int32) (rr.Generator1[ts3.Testenum1, ts3.Testenum1], error) {
	return &rr.Generator1Func[ts3.Testenum1, ts3.Testenum1]{NextFunc: func(_ context.Context, v ts3.Testenum1) (ts3.Testenum1, error) { return v, nil }}, nil
}

type obj4Impl struct{ ts3.Obj4DefaultImpl }

func (o *obj4Impl) Get_s_ind(ctx context.Context) (string, error) { return "", nil }
func (o *obj4Impl) Get_i_ind(ctx context.Context) (int32, error)  { return 0, nil }
func (o *obj4Impl) Get_data(ctx context.Context) (string, error)  { return "", nil }

type obj5Impl struct {
	ts3.Obj5DefaultImpl
	m1, m2, m3 *rr.SliceArrayMemory[int32]
}

func newObj5() *obj5Impl {
	return &obj5Impl{m1: rr.NewArrayMemory(make([]int32, 100)), m2: rr.NewArrayMemory(make([]int32, 100)),
		m3: rr.NewArrayMemory(make([]int32, 100))}
}

func (o *obj5Impl) Get_m1() rr.ArrayMemory[int32]         { return o.m1 }
func (o *obj5Impl) Get_m2() rr.ArrayMemory[int32]         { return o.m2 }
func (o *obj5Impl) Get_m3() rr.ArrayMemory[int32]         { return o.m3 }
func (o *obj5Impl) F1(ctx context.Context) (int32, error) { return 10, nil }
func (o *obj5Impl) F2(ctx context.Context) (int32, error) { return 11, nil }

// RegisterTestService3 registers RobotRaconteurTestService2 (TestService3)
// as the C++ RobotRaconteurTestService2Support.
func RegisterTestService3(n *rr.Node) (*Testroot3, error) {
	t := NewTestroot3()
	ctx, err := n.RegisterService("RobotRaconteurTestService2", "com.robotraconteur.testing.TestService3.testroot3", t)
	if err != nil {
		return nil, err
	}
	if err := ctx.RequestObjectLock("RobotRaconteurTestService2.nolock_test", "server"); err != nil {
		return nil, err
	}
	return t, nil
}
