package testservices

// Go port of RobotRaconteurTest_testroot and friends
// (robotraconteur/test/cpp/service/ServiceTest.cpp).

import (
	"context"
	"errors"
	"sync"
	"time"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts1 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice1"
	ts2 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice2"
)

// Testroot implements com.robotraconteur.testing.TestService1.testroot.
type Testroot struct {
	ts1.TestrootDefaultImpl

	mu        sync.Mutex
	o1        *sub1Impl
	o2, o3    map[int32]*sub1Impl
	o4        map[string]*sub1Impl
	o5        *TestService2SubObj
	o6        any
	var1      any
	var2      map[int32]any
	m1        *rr.SliceArrayMemory[float64]
	m2        *rr.SliceMultiDimArrayMemory[float64]
	m3        *rr.SliceMultiDimArrayMemory[uint8]
	p1Lock    sync.Mutex
	packetNum uint32
	sent      bool
	ackRecv   bool
	stop      chan struct{}
	bpipe     *rr.PipeBroadcaster[float64]
	bwire     *rr.WireBroadcaster[float64]
}

// NewTestroot creates the TestService1 root object.
func NewTestroot() *Testroot {
	t := &Testroot{
		o1: newSub1(), o2: map[int32]*sub1Impl{}, o3: map[int32]*sub1Impl{}, o4: map[string]*sub1Impl{},
		o5:   &TestService2SubObj{},
		m1:   rr.NewArrayMemory(vec[float64]("ts1.m1.initial")),
		m2:   rr.NewMultiDimArrayMemory(&rr.MultiDimArray[float64]{Dims: []uint32{10, 10, 10, 10, 10}, Array: make([]float64, 100000)}),
		m3:   rr.NewMultiDimArrayMemory(&rr.MultiDimArray[uint8]{Dims: []uint32{1024, 1024}, Array: make([]uint8, 1024*1024)}),
		stop: make(chan struct{}),
	}
	return t
}

// Shutdown stops the broadcast timers.
func (t *Testroot) Shutdown() {
	select {
	case <-t.stop:
	default:
		close(t.stop)
	}
}

// ---- numeric properties

func (t *Testroot) Get_d1(ctx context.Context) (float64, error) { return 12.345, nil }
func (t *Testroot) Set_d1(ctx context.Context, v float64) error { return check("d1", v, 3.456) }
func (t *Testroot) Get_d2(ctx context.Context) ([]float64, error) {
	return vec[float64]("ts1.d2.to_client"), nil
}
func (t *Testroot) Set_d2(ctx context.Context, v []float64) error {
	return check("d2", v, vec[float64]("ts1.d2.to_service"))
}
func (t *Testroot) Get_d3(ctx context.Context) ([]float64, error) {
	return vec[float64]("ts1.d3.to_client"), nil
}
func (t *Testroot) Set_d3(ctx context.Context, v []float64) error {
	return check("d3", v, vec[float64]("ts1.d3.to_service"))
}
func (t *Testroot) Get_d4(ctx context.Context) ([]float64, error) {
	return vec[float64]("ts1.d4.to_client"), nil
}
func (t *Testroot) Set_d4(ctx context.Context, v []float64) error {
	return check("d4", v, vec[float64]("ts1.d4.to_service"))
}
func (t *Testroot) Get_d5(ctx context.Context) (*rr.MultiDimArray[float64], error) {
	return mdaVec("ts1.d5.to_client"), nil
}
func (t *Testroot) Set_d5(ctx context.Context, v *rr.MultiDimArray[float64]) error {
	return check("d5", v, mdaVec("ts1.d5.to_service"))
}
func (t *Testroot) Get_d6(ctx context.Context) (*rr.MultiDimArray[float64], error) {
	return mdaVec("ts1.d6.to_client"), nil
}
func (t *Testroot) Set_d6(ctx context.Context, v *rr.MultiDimArray[float64]) error {
	return check("d6", v, mdaVec("ts1.d6.to_service"))
}
func (t *Testroot) Get_s1(ctx context.Context) (float32, error) { return 7.8573, nil }
func (t *Testroot) Set_s1(ctx context.Context, v float32) error {
	return check("s1", v, float32(3847.9283))
}
func (t *Testroot) Get_s2(ctx context.Context) ([]float32, error) {
	return vec[float32]("ts1.s2.to_client"), nil
}
func (t *Testroot) Set_s2(ctx context.Context, v []float32) error {
	return check("s2", v, vec[float32]("ts1.s2.to_service"))
}
func (t *Testroot) Get_i8_1(ctx context.Context) (int8, error) { return -66, nil }
func (t *Testroot) Set_i8_1(ctx context.Context, v int8) error { return check("i8_1", v, int8(45)) }
func (t *Testroot) Get_i8_2(ctx context.Context) ([]int8, error) {
	return vec[int8]("ts1.i8_2.to_client"), nil
}
func (t *Testroot) Set_i8_2(ctx context.Context, v []int8) error {
	return check("i8_2", v, vec[int8]("ts1.i8_2.to_service"))
}
func (t *Testroot) Get_u8_1(ctx context.Context) (uint8, error) { return 222, nil }
func (t *Testroot) Set_u8_1(ctx context.Context, v uint8) error { return check("u8_1", v, uint8(232)) }
func (t *Testroot) Get_u8_2(ctx context.Context) ([]uint8, error) {
	return vec[uint8]("ts1.u8_2.to_client"), nil
}
func (t *Testroot) Set_u8_2(ctx context.Context, v []uint8) error {
	return check("u8_2", v, vec[uint8]("ts1.u8_2.to_service"))
}
func (t *Testroot) Get_u8_3(ctx context.Context) (*rr.MultiDimArray[uint8], error) {
	return &rr.MultiDimArray[uint8]{Dims: vec[uint32]("ts1.u8_3.to_client.dims"), Array: vec[uint8]("ts1.u8_3.to_client")}, nil
}
func (t *Testroot) Set_u8_3(ctx context.Context, v *rr.MultiDimArray[uint8]) error {
	return check("u8_3", v, &rr.MultiDimArray[uint8]{Dims: vec[uint32]("ts1.u8_3.to_service.dims"), Array: vec[uint8]("ts1.u8_3.to_service")})
}
func (t *Testroot) Get_i16_1(ctx context.Context) (int16, error) { return -13428, nil }
func (t *Testroot) Set_i16_1(ctx context.Context, v int16) error {
	return check("i16_1", v, int16(2387))
}
func (t *Testroot) Get_i16_2(ctx context.Context) ([]int16, error) {
	return vec[int16]("ts1.i16_2.to_client"), nil
}
func (t *Testroot) Set_i16_2(ctx context.Context, v []int16) error {
	return check("i16_2", v, vec[int16]("ts1.i16_2.to_service"))
}
func (t *Testroot) Get_u16_1(ctx context.Context) (uint16, error) { return 60981, nil }
func (t *Testroot) Set_u16_1(ctx context.Context, v uint16) error {
	return check("u16_1", v, uint16(54732))
}
func (t *Testroot) Get_u16_2(ctx context.Context) ([]uint16, error) {
	return vec[uint16]("ts1.u16_2.to_client"), nil
}
func (t *Testroot) Set_u16_2(ctx context.Context, v []uint16) error {
	return check("u16_2", v, vec[uint16]("ts1.u16_2.to_service"))
}
func (t *Testroot) Get_i32_1(ctx context.Context) (int32, error) { return 898734, nil }
func (t *Testroot) Set_i32_1(ctx context.Context, v int32) error {
	return check("i32_1", v, int32(-9837284))
}
func (t *Testroot) Get_i32_2(ctx context.Context) ([]int32, error) {
	return vec[int32]("ts1.i32_2.to_client"), nil
}
func (t *Testroot) Set_i32_2(ctx context.Context, v []int32) error {
	return check("i32_2", v, vec[int32]("ts1.i32_2.to_service"))
}
func (t *Testroot) Get_i32_huge(ctx context.Context) ([]int32, error) {
	o := make([]int32, 2621440)
	for i := range o {
		o[i] = int32(i)
	}
	return o, nil
}
func (t *Testroot) Set_i32_huge(ctx context.Context, v []int32) error {
	if len(v) != 2621440 {
		return rr.NewOperationFailed("i32_huge: wrong length")
	}
	for i := range v {
		if v[i] != int32(-i) {
			return rr.NewOperationFailed("i32_huge: wrong value")
		}
	}
	return nil
}
func (t *Testroot) Get_u32_1(ctx context.Context) (uint32, error) { return 547919123, nil }
func (t *Testroot) Set_u32_1(ctx context.Context, v uint32) error {
	return check("u32_1", v, uint32(1550099195))
}
func (t *Testroot) Get_u32_2(ctx context.Context) ([]uint32, error) {
	return vec[uint32]("ts1.u32_2.to_client"), nil
}
func (t *Testroot) Set_u32_2(ctx context.Context, v []uint32) error {
	return check("u32_2", v, vec[uint32]("ts1.u32_2.to_service"))
}
func (t *Testroot) Get_i64_1(ctx context.Context) (int64, error) { return -1357833931563696072, nil }
func (t *Testroot) Set_i64_1(ctx context.Context, v int64) error {
	return check("i64_1", v, int64(8621740821050813024))
}
func (t *Testroot) Get_i64_2(ctx context.Context) ([]int64, error) {
	return vec[int64]("ts1.i64_2.to_client"), nil
}
func (t *Testroot) Set_i64_2(ctx context.Context, v []int64) error {
	return check("i64_2", v, vec[int64]("ts1.i64_2.to_service"))
}
func (t *Testroot) Get_u64_1(ctx context.Context) (uint64, error) { return 13389861970863644378, nil }
func (t *Testroot) Set_u64_1(ctx context.Context, v uint64) error {
	return check("u64_1", v, uint64(1465640522145789825))
}
func (t *Testroot) Get_u64_2(ctx context.Context) ([]uint64, error) {
	return vec[uint64]("ts1.u64_2.to_client"), nil
}
func (t *Testroot) Set_u64_2(ctx context.Context, v []uint64) error {
	return check("u64_2", v, vec[uint64]("ts1.u64_2.to_service"))
}
func (t *Testroot) Get_str1(ctx context.Context) (string, error) { return "Hello Client!", nil }
func (t *Testroot) Set_str1(ctx context.Context, v string) error {
	return check("str1", v, "Hello Server!")
}

// ---- structures

func ts2s(name string) *ts1.Teststruct2 { return &ts1.Teststruct2{Mydat: vec[float64](name)} }

func (t *Testroot) Get_struct1(ctx context.Context) (*ts1.Teststruct1, error) {
	return &ts1.Teststruct1{
		Dat1: vec[float64]("ts1.struct1.dat1.to_client"),
		Str2: "Hello world!",
		Vec3: map[int32]string{1: "Hello Client!", 2: "Hello Client, again", 4372: "This is yet another test string"},
		Dict4: map[string]string{"teststring1": "Hello Client!", "teststring2": "Hello Client, again",
			"anotherstr": "This is yet another test string"},
		List5:   []string{"Hello Client!", "Hello Client, again", "This is yet another test string"},
		Struct1: ts2s("ts1.struct1.struct1_mydat.to_client"),
		Dstruct2: map[string]*ts1.Teststruct2{"test1": ts2s("ts1.struct1.dstruct2_1.to_client"),
			"anothertest": ts2s("ts1.struct1.dstruct2_3.to_client")},
		Lstruct3:      []*ts1.Teststruct2{ts2s("ts1.struct1.dstruct2_1.to_client"), ts2s("ts1.struct1.dstruct2_3.to_client")},
		Multidimarray: mdaVec("ts1.struct1.multidimarray.to_client"),
		Var3:          "This is a vartype string",
	}, nil
}

func (t *Testroot) Set_struct1(ctx context.Context, s *ts1.Teststruct1) error {
	if s == nil {
		return rr.NewNullValue("struct1 is null")
	}
	return errors.Join(
		check("dat1", s.Dat1, vec[float64]("ts1.struct1.dat1.to_service")),
		check("str2", s.Str2, "Hello world 2!"),
		check("vec3", s.Vec3, map[int32]string{10: "Hello Server!", 11: "Hello Server, again", 46372: "Test string!", 46373: "Test string again"}),
		check("dict4", s.Dict4, map[string]string{"cteststring1": "Hello Server!", "cteststring2": "Hello Server, again"}),
		check("list5", s.List5, []string{"Hello Server!", "Hello Server, again"}),
		check("struct1", s.Struct1, ts2s("ts1.struct1.struct1_mydat.to_service")),
		check("dstruct2", s.Dstruct2, map[string]*ts1.Teststruct2{"ctest1": ts2s("ts1.struct1.dstruct2_1.to_service"),
			"anothertest": ts2s("ts1.struct1.dstruct2_2.to_service")}),
		check("lstruct3", s.Lstruct3, []*ts1.Teststruct2{ts2s("ts1.struct1.dstruct2_1.to_service"), ts2s("ts1.struct1.dstruct2_2.to_service")}),
		check("multidimarray", s.Multidimarray, mdaVec("ts1.struct1.multidimarray.to_service")),
		check("var3", s.Var3, vec[float64]("ts1.struct1.var3.to_service")),
	)
}

func (t *Testroot) Get_struct2(ctx context.Context) (*ts1.Teststruct2, error) {
	return ts2s("ts1.struct2.to_client"), nil
}
func (t *Testroot) Set_struct2(ctx context.Context, v *ts1.Teststruct2) error {
	return check("struct2", v, ts2s("ts1.struct2.to_service"))
}

// ---- maps

func (t *Testroot) Get_is_d1(ctx context.Context) (map[int32]float64, error) {
	return map[int32]float64{9285: 1.643392e-01, 74822: 1.537133e+09, 4: 1.369505e-03}, nil
}
func (t *Testroot) Set_is_d1(ctx context.Context, v map[int32]float64) error {
	return check("is_d1", v, map[int32]float64{928: 4.074501e-07, 394820: -4.535303e+05, 623: -2.956241e-20})
}
func (t *Testroot) Get_is_d2(ctx context.Context) (map[string]float64, error) {
	return map[string]float64{"testval1": -1.079664e+16, "testval2": 2.224846e+00}, nil
}
func (t *Testroot) Set_is_d2(ctx context.Context, v map[string]float64) error {
	return check("is_d2", v, map[string]float64{"testval3": 5.242474e+10, "testval4": 2.208636e+08})
}
func (t *Testroot) Get_is_d3(ctx context.Context) (map[int32][]float64, error) {
	return map[int32][]float64{12: vec[float64]("ts1.is_d3.to_client.a"), 832: vec[float64]("ts1.is_d3.to_client.b")}, nil
}
func (t *Testroot) Set_is_d3(ctx context.Context, v map[int32][]float64) error {
	return check("is_d3", v, map[int32][]float64{47: vec[float64]("ts1.is_d3.to_service.a"), 324: vec[float64]("ts1.is_d3.to_service.b")})
}
func (t *Testroot) Get_is_d4(ctx context.Context) (map[string][]float64, error) {
	return map[string][]float64{"testval1": vec[float64]("ts1.is_d4.to_client.a"), "testval2": vec[float64]("ts1.is_d4.to_client.b")}, nil
}
func (t *Testroot) Set_is_d4(ctx context.Context, v map[string][]float64) error {
	return check("is_d4", v, map[string][]float64{"testval3": vec[float64]("ts1.is_d4.to_service.a"), "testval4": vec[float64]("ts1.is_d4.to_service.b")})
}
func (t *Testroot) Get_is_d5(ctx context.Context) (map[int32]*rr.MultiDimArray[float64], error) {
	return map[int32]*rr.MultiDimArray[float64]{564: mdaVec("ts1.is_d5.to_client")}, nil
}
func (t *Testroot) Set_is_d5(ctx context.Context, v map[int32]*rr.MultiDimArray[float64]) error {
	return check("is_d5", v, map[int32]*rr.MultiDimArray[float64]{328: mdaVec("ts1.is_d5.to_service")})
}
func (t *Testroot) Get_is_d6(ctx context.Context) (map[string]*rr.MultiDimArray[float64], error) {
	return map[string]*rr.MultiDimArray[float64]{"testval1": mdaVec("ts1.is_d6.to_client")}, nil
}
func (t *Testroot) Set_is_d6(ctx context.Context, v map[string]*rr.MultiDimArray[float64]) error {
	return check("is_d6", v, map[string]*rr.MultiDimArray[float64]{"testval2": mdaVec("ts1.is_d6.to_service")})
}
func (t *Testroot) Get_is_str1(ctx context.Context) (map[int32]string, error) {
	return map[int32]string{23: "Hello server"}, nil
}
func (t *Testroot) Set_is_str1(ctx context.Context, v map[int32]string) error {
	return check("is_str1", v, map[int32]string{24: "Hello client"})
}
func (t *Testroot) Get_is_str2(ctx context.Context) (map[string]string, error) {
	return map[string]string{"testval1": "Hello server"}, nil
}
func (t *Testroot) Set_is_str2(ctx context.Context, v map[string]string) error {
	return check("is_str2", v, map[string]string{"testval2": "Hello client"})
}
func (t *Testroot) Get_is_struct1(ctx context.Context) (map[int32]*ts1.Teststruct2, error) {
	return map[int32]*ts1.Teststruct2{748: ts2s("ts1.is_struct1.to_client")}, nil
}
func (t *Testroot) Set_is_struct1(ctx context.Context, v map[int32]*ts1.Teststruct2) error {
	return check("is_struct1", v, map[int32]*ts1.Teststruct2{372: ts2s("ts1.is_struct1.to_service")})
}
func (t *Testroot) Get_is_struct2(ctx context.Context) (map[string]*ts1.Teststruct2, error) {
	return map[string]*ts1.Teststruct2{"testval1": ts2s("ts1.is_struct2.to_client")}, nil
}
func (t *Testroot) Set_is_struct2(ctx context.Context, v map[string]*ts1.Teststruct2) error {
	return check("is_struct2", v, map[string]*ts1.Teststruct2{"testval2": ts2s("ts1.is_struct2.to_service")})
}

// ---- lists

func (t *Testroot) Get_list_d1(ctx context.Context) ([]float64, error) {
	return []float64{1.643392e-01, 1.537133e+09, 1.369505e-03}, nil
}
func (t *Testroot) Set_list_d1(ctx context.Context, v []float64) error {
	return check("list_d1", v, []float64{4.074501e-07, -4.535303e+05, -2.956241e-20})
}
func (t *Testroot) Get_list_d3(ctx context.Context) ([][]float64, error) {
	return [][]float64{vec[float64]("ts1.is_d3.to_client.a"), vec[float64]("ts1.is_d3.to_client.b")}, nil
}
func (t *Testroot) Set_list_d3(ctx context.Context, v [][]float64) error {
	return check("list_d3", v, [][]float64{vec[float64]("ts1.is_d3.to_service.a"), vec[float64]("ts1.is_d3.to_service.b")})
}
func (t *Testroot) Get_list_d5(ctx context.Context) ([]*rr.MultiDimArray[float64], error) {
	return []*rr.MultiDimArray[float64]{mdaVec("ts1.is_d5.to_client")}, nil
}
func (t *Testroot) Set_list_d5(ctx context.Context, v []*rr.MultiDimArray[float64]) error {
	return check("list_d5", v, []*rr.MultiDimArray[float64]{mdaVec("ts1.is_d5.to_service")})
}
func (t *Testroot) Get_list_str1(ctx context.Context) ([]string, error) {
	return []string{"Hello server"}, nil
}
func (t *Testroot) Set_list_str1(ctx context.Context, v []string) error {
	return check("list_str1", v, []string{"Hello client"})
}
func (t *Testroot) Get_list_struct1(ctx context.Context) ([]*ts1.Teststruct2, error) {
	return []*ts1.Teststruct2{ts2s("ts1.is_struct1.to_client")}, nil
}
func (t *Testroot) Set_list_struct1(ctx context.Context, v []*ts1.Teststruct2) error {
	return check("list_struct1", v, []*ts1.Teststruct2{ts2s("ts1.is_struct1.to_service")})
}

func (t *Testroot) Get_struct3(ctx context.Context) (*ts2.Ostruct2, error) {
	return &ts2.Ostruct2{A1: vec[float64]("ts1.struct3.to_client")}, nil
}
func (t *Testroot) Set_struct3(ctx context.Context, v *ts2.Ostruct2) error {
	return check("struct3", v, &ts2.Ostruct2{A1: vec[float64]("ts1.struct3.to_service")})
}

// ---- varvalue

func (t *Testroot) Get_var1(ctx context.Context) (any, error) {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.var1, nil
}
func (t *Testroot) Set_var1(ctx context.Context, v any) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.var1 = v
	return nil
}
func (t *Testroot) Get_var2(ctx context.Context) (map[int32]any, error) {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.var2, nil
}
func (t *Testroot) Set_var2(ctx context.Context, v map[int32]any) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.var2 = v
	return nil
}
func (t *Testroot) Get_var_num(ctx context.Context) (any, error) {
	return vec[int32]("ts1.var_num.to_client"), nil
}
func (t *Testroot) Set_var_num(ctx context.Context, v any) error {
	return check("var_num", v, vec[int32]("ts1.var_num.to_service"))
}
func (t *Testroot) Get_var_str(ctx context.Context) (any, error) { return "Hello Client!", nil }
func (t *Testroot) Set_var_str(ctx context.Context, v any) error {
	return check("var_str", v, "Hello Server!")
}
func (t *Testroot) Get_var_struct(ctx context.Context) (any, error) {
	return ts2s("ts1.var_struct.to_client"), nil
}
func (t *Testroot) Set_var_struct(ctx context.Context, v any) error {
	return check("var_struct", v, ts2s("ts1.var_struct.to_service"))
}
func (t *Testroot) Get_var_vector(ctx context.Context) (any, error) {
	return map[int32]any{10: "Hello Client!"}, nil
}
func (t *Testroot) Set_var_vector(ctx context.Context, v any) error {
	return check("var_vector", v, map[int32]any{11: "Hello Server!"})
}
func (t *Testroot) Get_var_dictionary(ctx context.Context) (any, error) {
	return map[string]any{"test1": "Hello Client!"}, nil
}
func (t *Testroot) Set_var_dictionary(ctx context.Context, v any) error {
	return check("var_dictionary", v, map[string]any{"test2": "Hello Server!"})
}
func (t *Testroot) Get_var_list(ctx context.Context) (any, error) { return []any{"Hello Client!"}, nil }
func (t *Testroot) Set_var_list(ctx context.Context, v any) error {
	return check("var_list", v, []any{"Hello Server!"})
}
func (t *Testroot) Get_var_multidimarray(ctx context.Context) (any, error) {
	return mdaVec("ts1.var_multidimarray.to_client"), nil
}
func (t *Testroot) Set_var_multidimarray(ctx context.Context, v any) error {
	return check("var_multidimarray", v, mdaVec("ts1.var_multidimarray.to_service"))
}

func (t *Testroot) Get_errtest(ctx context.Context) (float64, error) {
	return 0, rr.NewRemoteError("std::runtime_error", "This is a test")
}
func (t *Testroot) Set_errtest(ctx context.Context, _ float64) error {
	return rr.NewRemoteError("std::runtime_error", "This is a test")
}
func (t *Testroot) Get_nulltest(ctx context.Context) (*ts1.Teststruct1, error) { return nil, nil }
func (t *Testroot) Set_nulltest(ctx context.Context, v *ts1.Teststruct1) error {
	if v != nil {
		return rr.NewOperationFailed("nulltest: expected null")
	}
	return nil
}

// ---- functions and events

func (t *Testroot) Func1(ctx context.Context) error {
	go func() {
		time.Sleep(100 * time.Millisecond)
		for _, h := range t.Get_ev1().Handlers() {
			h()
		}
	}()
	return nil
}

func (t *Testroot) Func2(ctx context.Context, d1, d2 float64) error {
	go func() {
		time.Sleep(200 * time.Millisecond)
		s := &ts1.Teststruct2{Mydat: []float64{d2}}
		for _, h := range t.Get_ev2().Handlers() {
			h(d1, s)
		}
	}()
	return nil
}

func (t *Testroot) Func3(ctx context.Context, d1, d2 float64) (float64, error) { return d1 + d2, nil }
func (t *Testroot) Meaning_of_life(ctx context.Context) (int32, error)         { return 42, nil }
func (t *Testroot) Func_errtest(ctx context.Context) error {
	return rr.NewRemoteError("std::runtime_error", "This is a test")
}
func (t *Testroot) Func_errtest1(ctx context.Context) error {
	return rr.NewDataTypeError("This is a test 1")
}
func (t *Testroot) Func_errtest2(ctx context.Context) error {
	return ts1.NewTestexception1("This is a test 2")
}
func (t *Testroot) Func_errtest3(ctx context.Context) error {
	return ts2.NewTestexception3("This is a test 3")
}

// ---- objrefs

func (t *Testroot) Get_o1(ctx context.Context) (ts1.Sub1, error) { return t.o1, nil }

func getOrCreate[K comparable](mu *sync.Mutex, m map[K]*sub1Impl, k K, init func(*sub1Impl)) *sub1Impl {
	mu.Lock()
	defer mu.Unlock()
	if o, ok := m[k]; ok {
		return o
	}
	o := newSub1()
	init(o)
	m[k] = o
	return o
}

func (t *Testroot) Get_o2(ctx context.Context, ind int32) (ts1.Sub1, error) {
	return getOrCreate(&t.mu, t.o2, ind, func(o *sub1Impl) { o.Set_i_ind(ctx, ind) }), nil
}
func (t *Testroot) Get_o3(ctx context.Context, ind int32) (ts1.Sub1, error) {
	return getOrCreate(&t.mu, t.o3, ind, func(o *sub1Impl) { o.Set_i_ind(ctx, ind) }), nil
}
func (t *Testroot) Get_o4(ctx context.Context, ind string) (ts1.Sub1, error) {
	return getOrCreate(&t.mu, t.o4, ind, func(o *sub1Impl) { o.Set_s_ind(ctx, ind) }), nil
}
func (t *Testroot) Get_o5(ctx context.Context) (ts2.Subobj, error) { return t.o5, nil }
func (t *Testroot) Get_o6(ctx context.Context) (any, error) {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.o6, nil
}

func (t *Testroot) O6_op(ctx context.Context, op int32) error {
	t.mu.Lock()
	defer t.mu.Unlock()
	if path, err := rr.CurrentServicePath(ctx); err == nil {
		if sc, err := rr.CurrentServerContext(ctx); err == nil {
			_ = sc.ReleaseServicePath(path + ".o6")
		}
	}
	switch op {
	case 0:
		t.o6 = newSub1()
	case 1:
		t.o6 = &sub2Impl{o3_1: map[string]*sub3Impl{}}
	case 2:
		t.o6 = &TestService2SubObj{}
	default:
		return rr.NewInvalidArgument("invalid op")
	}
	return nil
}

// ---- pipes

func (t *Testroot) Set_p1(p *rr.Pipe[[]float64]) {
	t.TestrootDefaultImpl.Set_p1(p)
	p.SetPipeConnectCallback(func(ep *rr.PipeEndpoint[[]float64]) {
		ep.PacketReceivedEvent().Connect(func(ep *rr.PipeEndpoint[[]float64]) {
			go func() {
				time.Sleep(10 * time.Millisecond)
				t.p1Lock.Lock()
				defer t.p1Lock.Unlock()
				for ep.Available() > 0 {
					v, err := ep.ReceivePacket()
					if err != nil {
						continue
					}
					n, err := ep.SendPacket(context.Background(), v)
					if err == nil && !t.sent {
						t.packetNum = n
						t.sent = true
					}
				}
			}()
		})
		ep.PacketAckReceivedEvent().Connect(func(ep *rr.PipeEndpoint[[]float64], n uint32) {
			t.p1Lock.Lock()
			defer t.p1Lock.Unlock()
			if n == t.packetNum {
				t.ackRecv = true
			}
		})
		ep.SetRequestPacketAck(true)
	})
}

func (t *Testroot) Set_p2(p *rr.Pipe[*ts1.Teststruct2]) {
	t.TestrootDefaultImpl.Set_p2(p)
	var lock sync.Mutex
	p.SetPipeConnectCallback(func(ep *rr.PipeEndpoint[*ts1.Teststruct2]) {
		ep.PacketReceivedEvent().Connect(func(ep *rr.PipeEndpoint[*ts1.Teststruct2]) {
			go func() {
				time.Sleep(100 * time.Millisecond)
				lock.Lock()
				defer lock.Unlock()
				for ep.Available() > 0 {
					v, err := ep.ReceivePacket()
					if err != nil {
						return
					}
					ep.SendPacket(context.Background(), v)
				}
			}()
		})
	})
}

func (t *Testroot) Pipe_check_error(ctx context.Context) error {
	t.p1Lock.Lock()
	defer t.p1Lock.Unlock()
	if !t.ackRecv {
		return rr.NewOperationFailed("packet ack not received")
	}
	return nil
}

func (t *Testroot) Set_broadcastpipe(p *rr.Pipe[float64]) {
	t.TestrootDefaultImpl.Set_broadcastpipe(p)
	b, err := rr.NewPipeBroadcaster(p, 3)
	if err != nil {
		return
	}
	t.bpipe = b
	go func() {
		tk := time.NewTicker(100 * time.Millisecond)
		defer tk.Stop()
		for {
			select {
			case <-t.stop:
				return
			case <-tk.C:
				for i := 0; i < 100; i++ {
					b.SendPacket(float64(i))
				}
			}
		}
	}()
}

// ---- callbacks

func (t *Testroot) Test_callbacks(ctx context.Context) error {
	ep, err := rr.CurrentEndpoint(ctx)
	if err != nil {
		return err
	}
	cb1, err := t.Get_cb1().GetClientFunction(ep)
	if err != nil {
		return err
	}
	if err := cb1(); err != nil {
		return err
	}
	cb2, _ := t.Get_cb2().GetClientFunction(ep)
	if err := cb2(739.2, 0.392); err != nil {
		return err
	}
	cb3, _ := t.Get_cb3().GetClientFunction(ep)
	res, err := cb3(34, 45)
	if err != nil {
		return err
	}
	if err := check("cb3", res, 34+45+3.14); err != nil {
		return err
	}
	mol, _ := t.Get_cb_meaning_of_life().GetClientFunction(ep)
	v, err := mol()
	if err != nil {
		return err
	}
	if err := check("cb_meaning_of_life", v, int32(42)); err != nil {
		return err
	}
	cbe, _ := t.Get_cb_errtest().GetClientFunction(ep)
	if err := cbe(); err == nil {
		return rr.NewOperationFailed("cb_errtest: expected error")
	}
	return nil
}

// ---- wires

func echoWire[T any](w *rr.Wire[T]) {
	w.SetWireConnectCallback(func(c *rr.WireConnection[T]) {
		c.WireValueChanged().Connect(func(c *rr.WireConnection[T], v T, ts rr.TimeSpec) {
			if in, err := c.InValue(); err == nil {
				c.SetOutValue(in)
			}
		})
	})
}

func (t *Testroot) Set_w1(w *rr.Wire[[]float64]) {
	t.TestrootDefaultImpl.Set_w1(w)
	echoWire(w)
}

func (t *Testroot) Set_w2(w *rr.Wire[*ts1.Teststruct2]) {
	t.TestrootDefaultImpl.Set_w2(w)
	echoWire(w)
}

func (t *Testroot) Set_w3(w *rr.Wire[*rr.MultiDimArray[int32]]) {
	t.TestrootDefaultImpl.Set_w3(w)
	echoWire(w)
}

func (t *Testroot) Set_broadcastwire(w *rr.Wire[float64]) {
	t.TestrootDefaultImpl.Set_broadcastwire(w)
	b, err := rr.NewWireBroadcaster(w)
	if err != nil {
		return
	}
	t.bwire = b
	go func() {
		tk := time.NewTicker(100 * time.Millisecond)
		defer tk.Stop()
		for {
			select {
			case <-t.stop:
				return
			case <-tk.C:
				for i := 0; i < 100; i++ {
					b.SetOutValue(float64(i))
				}
			}
		}
	}()
}

// ---- memories

func (t *Testroot) Get_m1() rr.ArrayMemory[float64]         { return t.m1 }
func (t *Testroot) Get_m2() rr.MultiDimArrayMemory[float64] { return t.m2 }
func (t *Testroot) Get_m3() rr.MultiDimArrayMemory[uint8]   { return t.m3 }

// ---- sub objects

type sub1Impl struct {
	ts1.Sub1DefaultImpl
	mu   sync.Mutex
	o2_1 *sub2Impl
	o2_2 map[int32]*sub2Impl
	o2_3 map[string]*sub2Impl
}

func newSub1() *sub1Impl {
	return &sub1Impl{o2_1: newSub2(), o2_2: map[int32]*sub2Impl{}, o2_3: map[string]*sub2Impl{}}
}

func (s *sub1Impl) Get_o2_1(ctx context.Context) (ts1.Sub2, error) { return s.o2_1, nil }
func (s *sub1Impl) Get_o2_2(ctx context.Context, ind int32) (ts1.Sub2, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if o, ok := s.o2_2[ind]; ok {
		return o, nil
	}
	o := newSub2()
	o.Set_i_ind(ctx, ind)
	s.o2_2[ind] = o
	return o, nil
}
func (s *sub1Impl) Get_o2_3(ctx context.Context, ind string) (ts1.Sub2, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if o, ok := s.o2_3[ind]; ok {
		return o, nil
	}
	o := newSub2()
	o.Set_s_ind(ctx, ind)
	s.o2_3[ind] = o
	return o, nil
}

// RRObjectType is required for varobject objrefs (o6).
func (s *sub1Impl) RRObjectType() string { return "com.robotraconteur.testing.TestService1.sub1" }

type sub2Impl struct {
	ts1.Sub2DefaultImpl
	mu   sync.Mutex
	o3_1 map[string]*sub3Impl
}

func newSub2() *sub2Impl { return &sub2Impl{o3_1: map[string]*sub3Impl{}} }

func (s *sub2Impl) Get_o3_1(ctx context.Context, ind string) (ts1.Sub3, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if o, ok := s.o3_1[ind]; ok {
		return o, nil
	}
	o := &sub3Impl{ind: ind}
	s.o3_1[ind] = o
	return o, nil
}

func (s *sub2Impl) RRObjectType() string { return "com.robotraconteur.testing.TestService1.sub2" }

type sub3Impl struct {
	ts1.Sub3DefaultImpl
	ind string
}

func (s *sub3Impl) Get_ind(ctx context.Context) (string, error)         { return s.ind, nil }
func (s *sub3Impl) Set_ind(ctx context.Context, _ string) error         { return nil }
func (s *sub3Impl) Add(ctx context.Context, d float64) (float64, error) { return d + 42, nil }

// TestService2SubObj implements com.robotraconteur.testing.TestService2.subobj.
type TestService2SubObj struct{}

func (TestService2SubObj) Add_val(ctx context.Context, v float64) (float64, error) { return v + 1, nil }
func (TestService2SubObj) RRObjectType() string {
	return "com.robotraconteur.testing.TestService2.subobj"
}

// ---- registration

// AuthData is the password file used for RobotRaconteurTestService_auth.
const AuthData = "testuser1 0b91dec4fe98266a03b136b59219d0d6 objectlock\n" +
	"testuser2 841c4221c2e7e0cefbc0392a35222512 objectlock\n" +
	"testsuperuser 503ed776c50169f681ad7bbc14198b68 objectlock,objectlockoverride"

// RegisterTestService1 registers RobotRaconteurTestService and
// RobotRaconteurTestService_auth (as the C++ RobotRaconteurTestServiceSupport).
func RegisterTestService1(n *rr.Node) (roots []*Testroot, err error) {
	t1, t2 := NewTestroot(), NewTestroot()
	ctx, err := n.RegisterService("RobotRaconteurTestService", "com.robotraconteur.testing.TestService1.testroot", t1)
	if err != nil {
		return nil, err
	}
	if err := ctx.SetServiceAttributes(map[string]any{"test": "This is a test attribute", "test2": int32(42)}); err != nil {
		return nil, err
	}
	policy := &rr.ServiceSecurityPolicy{
		Authenticator: rr.NewPasswordFileUserAuthenticator(AuthData),
		Policies:      map[string]string{"requirevaliduser": "true", "allowobjectlock": "true"},
	}
	if _, err := n.RegisterService("RobotRaconteurTestService_auth", "com.robotraconteur.testing.TestService1.testroot", t2,
		rr.WithSecurityPolicy(policy)); err != nil {
		return nil, err
	}
	return []*Testroot{t1, t2}, nil
}
