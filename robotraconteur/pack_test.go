package robotraconteur

import (
	"errors"
	"reflect"
	"testing"
)

// Hand written equivalents of generated types (see RobotRaconteurGenGo).

type testEnum int32

type testStruct struct {
	D  float64
	A  []int32
	S  string
	M  map[string]float64
	L  []*testStruct
	V  any
	P  testPod
	NA testVec3
}

func (s *testStruct) RRType() string { return "experimental.gotest.teststruct" }
func (s *testStruct) RRPack() ([]*Element, error) {
	m, err := PackMapString("m", s.M, func(n string, v float64) (*Element, error) { return PackScalar(n, v), nil })
	if err != nil {
		return nil, err
	}
	l, err := PackList("l", s.L, PackStructure[testStruct])
	if err != nil {
		return nil, err
	}
	v, err := PackVarValue("v", s.V)
	if err != nil {
		return nil, err
	}
	p, err := PackPodToArray("p", s.P)
	if err != nil {
		return nil, err
	}
	return []*Element{PackScalar("d", s.D), PackArray("a", s.A), PackString("s", s.S), m, l, v, p,
		PackNamedArrayToArray[testVec3, float64]("na", s.NA)}, nil
}
func (s *testStruct) RRUnpack(elems []*Element) (err error) {
	get := func(n string) *Element { e, _ := FindElement(elems, n); return e }
	if s.D, err = UnpackScalar[float64](get("d")); err != nil {
		return
	}
	if s.A, err = UnpackArray[int32](get("a")); err != nil {
		return
	}
	if s.S, err = UnpackString(get("s")); err != nil {
		return
	}
	if s.M, err = UnpackMapString(get("m"), UnpackScalar[float64]); err != nil {
		return
	}
	if s.L, err = UnpackList(get("l"), UnpackStructure[testStruct]); err != nil {
		return
	}
	if s.V, err = UnpackVarValue(get("v")); err != nil {
		return
	}
	if s.P, err = UnpackPodFromArray[testPod](get("p")); err != nil {
		return
	}
	s.NA, err = UnpackNamedArrayFromArray[testVec3, float64](get("na"))
	return
}

type testPod struct {
	A int16
	B [3]float32
	E testEnum
}

func (testPod) RRType() string { return "experimental.gotest.testpod" }
func (p testPod) RRPackPod() ([]*Element, error) {
	return []*Element{PackScalar("a", p.A), PackArray("b", p.B[:]), PackEnum("e", p.E)}, nil
}
func (p *testPod) RRUnpackPod(elems []*Element) error {
	get := func(n string) *Element { e, _ := FindElement(elems, n); return e }
	var err error
	if p.A, err = UnpackScalar[int16](get("a")); err != nil {
		return err
	}
	b, err := UnpackArray[float32](get("b"))
	if err != nil {
		return err
	}
	if _, err := VerifyArrayLength(b, 3, false); err != nil {
		return err
	}
	copy(p.B[:], b)
	p.E, err = UnpackEnum[testEnum](get("e"))
	return err
}

type testVec3 struct{ X, Y, Z float64 }

func (testVec3) RRType() string        { return "experimental.gotest.vec3" }
func (testVec3) RRElementCount() int   { return 3 }
func (v testVec3) RRToArray(d []float64) { d[0], d[1], d[2] = v.X, v.Y, v.Z }
func (v *testVec3) RRFromArray(s []float64) { v.X, v.Y, v.Z = s[0], s[1], s[2] }

func init() {
	RegisterStructureType[testStruct]()
	RegisterPodType[testPod]()
	RegisterNamedArrayType[testVec3, float64]()
}

func roundTrip(t *testing.T, e *Element) *Element {
	t.Helper()
	if e == nil {
		t.Fatal("nil element")
	}
	out, err := elementRoundTrip(e)
	if err != nil {
		t.Fatalf("round trip %q: %v", e.Name, err)
	}
	return out
}

func TestPackPrimitivesRoundTrip(t *testing.T) {
	if v, err := UnpackScalar[float64](roundTrip(t, PackScalar("x", 3.25))); err != nil || v != 3.25 {
		t.Errorf("scalar %v %v", v, err)
	}
	if v, err := UnpackScalar[bool](roundTrip(t, PackScalar("b", true))); err != nil || !v {
		t.Errorf("bool %v %v", v, err)
	}
	if v, err := UnpackScalar[complex128](roundTrip(t, PackScalar("c", complex(1, -2)))); err != nil || v != complex(1, -2) {
		t.Errorf("cdouble %v %v", v, err)
	}
	if v, err := UnpackArray[complex64](roundTrip(t, PackArray("c", []complex64{1i, 2}))); err != nil || v[0] != 1i || v[1] != 2 {
		t.Errorf("csingle %v %v", v, err)
	}
	for _, s := range []string{"", "hello", "unicode ✓ ünïcödé"} {
		if v, err := UnpackString(roundTrip(t, PackString("s", s))); err != nil || v != s {
			t.Errorf("string %q %v", v, err)
		}
	}
	a := []uint64{0, 1, 1 << 63}
	if v, err := UnpackArray[uint64](roundTrip(t, PackArray("u", a))); err != nil || !reflect.DeepEqual(v, a) {
		t.Errorf("uint64 %v %v", v, err)
	}
	if v, err := UnpackArray[int8](roundTrip(t, PackArray[int8]("e", nil))); err != nil || len(v) != 0 {
		t.Errorf("empty %v %v", v, err)
	}
	if v, err := UnpackEnum[testEnum](roundTrip(t, PackEnum("e", testEnum(-5)))); err != nil || v != -5 {
		t.Errorf("enum %v %v", v, err)
	}
	// type mismatch is an error, not a panic
	if _, err := UnpackArray[float32](roundTrip(t, PackArray("x", []float64{1}))); !errors.Is(err, ErrDataTypeMismatch) {
		t.Errorf("expected DataTypeMismatch, got %v", err)
	}
	e := roundTrip(t, NewNumberedElement(7, []int32{1}))
	if n, err := e.ElementNumber(); err != nil || n != 7 {
		t.Errorf("element number %v %v", n, err)
	}
}

func TestPackMultiDimArray(t *testing.T) {
	m := &MultiDimArray[float64]{Dims: []uint32{2, 3}, Array: []float64{1, 2, 3, 4, 5, 6}}
	e, err := PackMultiDimArray("m", m)
	if err != nil {
		t.Fatal(err)
	}
	m2, err := UnpackMultiDimArray[float64](roundTrip(t, e))
	if err != nil || !reflect.DeepEqual(m, m2) {
		t.Fatalf("%v %v", m2, err)
	}
	if _, err := PackMultiDimArray("m", &MultiDimArray[float64]{Dims: []uint32{2}, Array: []float64{1}}); err == nil {
		t.Error("expected dims error")
	}
}

func TestPackStructurePodNamedArray(t *testing.T) {
	s := &testStruct{
		D: 1.5, A: []int32{1, 2, 3}, S: "abc",
		M: map[string]float64{"x": 1, "y": 2},
		L: []*testStruct{{D: 2, A: []int32{}, S: "child", P: testPod{A: 1}}},
		V: []float64{4, 5},
		P: testPod{A: -3, B: [3]float32{1, 2, 3}, E: 2},
		NA: testVec3{1, 2, 3},
	}
	e, err := PackStructure("s", s)
	if err != nil {
		t.Fatal(err)
	}
	s2, err := UnpackStructure[testStruct](roundTrip(t, e))
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(s, s2) {
		t.Fatalf("structure mismatch\n%+v\n%+v", s, s2)
	}
	// nil structure
	e, _ = PackStructure[testStruct]("n", nil)
	if s3, err := UnpackStructure[testStruct](roundTrip(t, e)); err != nil || s3 != nil {
		t.Errorf("nil structure %v %v", s3, err)
	}
	// pod arrays and multidim
	pods := []testPod{{A: 1}, {A: 2, E: 1}}
	e, _ = PackPodArray("p", pods)
	if p2, err := UnpackPodArray[testPod](roundTrip(t, e)); err != nil || !reflect.DeepEqual(pods, p2) {
		t.Errorf("pod array %v %v", p2, err)
	}
	pm := &PodMultiDimArray[testPod]{Dims: []uint32{1, 2}, PodArray: pods}
	e, _ = PackPodMultiDimArray("pm", pm)
	if p3, err := UnpackPodMultiDimArray[testPod](roundTrip(t, e)); err != nil || !reflect.DeepEqual(pm, p3) {
		t.Errorf("pod multidim %v %v", p3, err)
	}
	na := []testVec3{{1, 2, 3}, {4, 5, 6}}
	if n2, err := UnpackNamedArray[testVec3, float64](roundTrip(t, PackNamedArray[testVec3, float64]("n", na))); err != nil || !reflect.DeepEqual(na, n2) {
		t.Errorf("namedarray %v %v", n2, err)
	}
	nm := &NamedMultiDimArray[testVec3]{Dims: []uint32{2}, NamedArrayArray: na}
	e, _ = PackNamedMultiDimArray[testVec3, float64]("nm", nm)
	if n3, err := UnpackNamedMultiDimArray[testVec3, float64](roundTrip(t, e)); err != nil || !reflect.DeepEqual(nm, n3) {
		t.Errorf("namedarray multidim %v %v", n3, err)
	}
}

func TestPackVarValue(t *testing.T) {
	vals := []any{
		nil, "str", []float64{1, 2}, []bool{true, false}, []complex128{1 + 2i},
		&MultiDimArray[int32]{Dims: []uint32{2}, Array: []int32{1, 2}},
		map[string]any{"a": []int32{1}, "b": "x"},
		map[int32]any{1: []uint8{9}},
		[]any{"x", []float32{1}},
		&testStruct{A: []int32{}, M: nil},
		[]testPod{{A: 7}},
		&PodMultiDimArray[testPod]{Dims: []uint32{1}, PodArray: []testPod{{A: 8}}},
		[]testVec3{{1, 1, 1}},
	}
	for _, v := range vals {
		e, err := PackVarValue("v", v)
		if err != nil {
			t.Fatalf("%T: %v", v, err)
		}
		v2, err := UnpackVarValue(roundTrip(t, e))
		if err != nil {
			t.Fatalf("%T: %v", v, err)
		}
		if !reflect.DeepEqual(v, v2) {
			t.Errorf("varvalue %T: got %#v want %#v", v, v2, v)
		}
	}
	// scalars become length one arrays (as in the other bindings)
	e, _ := PackVarValue("v", int32(5))
	if v, _ := UnpackVarValue(roundTrip(t, e)); !reflect.DeepEqual(v, []int32{5}) {
		t.Errorf("scalar varvalue %#v", v)
	}
	if _, err := PackVarValue("v", struct{}{}); !errors.Is(err, ErrDataTypeError) {
		t.Errorf("invalid varvalue: %v", err)
	}
}
