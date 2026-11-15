// Package testservices is a Go port of the upstream C++ test services
// (robotraconteur/test/cpp/service/ServiceTest*.cpp). The services can be
// run with cmd/rrgo_test_server so the upstream C++ client test suites can
// be pointed at a Go server (ROBOTRACONTEUR_TEST_SERVER_URL).
package testservices

import (
	"fmt"
	"reflect"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	tv "github.com/robotraconteur/robotraconteur_go/test/testvectors"
)

// check returns an error describing a mismatch (sent to the client).
func check(name string, got, want any) error {
	if !reflect.DeepEqual(got, want) {
		return rr.NewOperationFailed(fmt.Sprintf("%s: got %v, want %v", name, abbrev(got), abbrev(want)))
	}
	return nil
}

func abbrev(v any) string {
	s := fmt.Sprint(v)
	if len(s) > 200 {
		return s[:200] + "..."
	}
	return s
}

func vec[T interface {
	~float64 | ~float32 | ~int8 | ~uint8 | ~int16 | ~uint16 | ~int32 | ~uint32 | ~int64 | ~uint64
}](name string) []T {
	return tv.MustGet[T](name)
}

func mda[T rr.Primitive, D interface{ ~uint32 }](dims []D, a []T) *rr.MultiDimArray[T] {
	d := make([]uint32, len(dims))
	for i := range dims {
		d[i] = uint32(dims[i])
	}
	return &rr.MultiDimArray[T]{Dims: d, Array: a}
}

func mdaVec(name string) *rr.MultiDimArray[float64] {
	return &rr.MultiDimArray[float64]{Dims: vec[uint32](name + ".dims"), Array: vec[float64](name)}
}
