package robotraconteur

import (
	"fmt"
	"reflect"
)

// DataType is a Robot Raconteur data type code (DataTypes).
type DataType uint32

// String returns the Robot Raconteur name of the data type, e.g. "double_t".
func (d DataType) String() string {
	if n, ok := dataTypeNames[d]; ok {
		return n
	}
	return fmt.Sprintf("DataType(%d)", uint32(d))
}

// IsNumber reports whether d is a numeric (or bool/complex) array element type.
func (d DataType) IsNumber() bool {
	return (d >= DataTypeDouble && d <= DataTypeUint64) || d == DataTypeCDouble || d == DataTypeCSingle || d == DataTypeBool
}

// Number is the set of Robot Raconteur real numeric types.
type Number interface {
	~float64 | ~float32 | ~int8 | ~uint8 | ~int16 | ~uint16 | ~int32 | ~uint32 | ~int64 | ~uint64
}

// Primitive is the set of types usable as Robot Raconteur numeric array
// elements: real numbers, complex numbers (cdouble, csingle) and bool.
type Primitive interface {
	Number | ~complex128 | ~complex64 | ~bool
}

// DataTypeOf returns the Robot Raconteur data type of the primitive type T.
func DataTypeOf[T Primitive]() DataType {
	return dataTypeOfKind(reflect.TypeFor[T]().Kind())
}

func dataTypeOfKind(k reflect.Kind) DataType {
	switch k {
	case reflect.Float64:
		return DataTypeDouble
	case reflect.Float32:
		return DataTypeSingle
	case reflect.Int8:
		return DataTypeInt8
	case reflect.Uint8:
		return DataTypeUint8
	case reflect.Int16:
		return DataTypeInt16
	case reflect.Uint16:
		return DataTypeUint16
	case reflect.Int32:
		return DataTypeInt32
	case reflect.Uint32:
		return DataTypeUint32
	case reflect.Int64:
		return DataTypeInt64
	case reflect.Uint64:
		return DataTypeUint64
	case reflect.Complex128:
		return DataTypeCDouble
	case reflect.Complex64:
		return DataTypeCSingle
	case reflect.Bool:
		return DataTypeBool
	}
	return DataTypeVoid
}

// MultiDimArray is a Robot Raconteur numeric multi-dimensional array
// (T[*]). Array is stored in column-major (Fortran) order, as on the wire.
type MultiDimArray[T Primitive] struct {
	Dims  []uint32
	Array []T
}

// NewMultiDimArray creates a zeroed multi-dimensional array with the given dims.
func NewMultiDimArray[T Primitive](dims ...uint32) *MultiDimArray[T] {
	return &MultiDimArray[T]{Dims: append([]uint32(nil), dims...), Array: make([]T, dimsCount(dims))}
}

// PodMultiDimArray is a multi-dimensional array of pods (pod[*]).
type PodMultiDimArray[T any] struct {
	Dims     []uint32
	PodArray []T
}

// NamedMultiDimArray is a multi-dimensional array of namedarrays (namedarray[*]).
type NamedMultiDimArray[T any] struct {
	Dims            []uint32
	NamedArrayArray []T
}

func dimsCount(dims []uint32) int {
	n := 1
	for _, d := range dims {
		n *= int(d)
	}
	return n
}

// VerifyArrayLength checks an array against a fixed (varlen false) or
// maximum (varlen true) length declared in a service definition.
func VerifyArrayLength[T any](a []T, n int, varlen bool) ([]T, error) {
	if varlen {
		if len(a) > n {
			return nil, NewDataTypeError(fmt.Sprintf("array exceeds maximum length %d", n))
		}
	} else if len(a) != n {
		return nil, NewDataTypeError(fmt.Sprintf("array must have length %d", n))
	}
	return a, nil
}

// VerifyMultiDimArrayDims checks multi-dimensional array dims against a
// fixed shape declared in a service definition.
func VerifyMultiDimArrayDims(dims []uint32, count int, want []uint32) error {
	if len(dims) != len(want) {
		return NewDataTypeError("multidimarray dimension mismatch")
	}
	for i := range dims {
		if dims[i] != want[i] {
			return NewDataTypeError("multidimarray dimension mismatch")
		}
	}
	if dimsCount(dims) != count {
		return NewDataTypeError("multidimarray element count mismatch")
	}
	return nil
}
