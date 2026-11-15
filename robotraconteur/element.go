package robotraconteur

import (
	"fmt"
	"strconv"
	"unsafe"
)

// Element is a Robot Raconteur message element: a named (or numbered) value
// in Robot Raconteur wire format. Generated code and the runtime build trees
// of Elements in Go and convert them to native messages in one step, so no
// native handles are held while packing.
//
// Data is one of:
//   - nil (null value)
//   - a numeric slice of a base type: []float64, []float32, []int8, []uint8,
//     []int16, []uint16, []int32, []uint32, []int64, []uint64,
//     []complex128 (cdouble), []complex64 (csingle), []bool
//   - string
//   - *NestedList (structures, pods, namedarrays, maps, lists, multidimarrays)
type Element struct {
	Name      string
	Number    int32
	UseNumber bool
	Data      any
	// Type and TypeName are filled in for received elements.
	Type     DataType
	TypeName string
}

// NestedList is the data of a nested message element (structure_t,
// pod_t, pod_array_t, vector_t, dictionary_t, list_t, multidimarray_t, ...).
type NestedList struct {
	Type     DataType
	TypeName string
	Elements []*Element
}

// NewElement returns a named element.
func NewElement(name string, data any) *Element {
	return &Element{Name: name, Data: data}
}

// NewNumberedElement returns an element identified by number (used by
// lists, int32 keyed maps and pod arrays).
func NewNumberedElement(n int32, data any) *Element {
	return &Element{Number: n, UseNumber: true, Data: data}
}

// DataType returns the element data type, from Type if set (received
// elements) or derived from Data.
func (e *Element) DataType() DataType {
	if e.Type != DataTypeVoid {
		return e.Type
	}
	return dataTypeOfData(e.Data)
}

func dataTypeOfData(d any) DataType {
	switch v := d.(type) {
	case nil:
		return DataTypeVoid
	case string:
		return DataTypeString
	case *NestedList:
		return v.Type
	case []float64:
		return DataTypeDouble
	case []float32:
		return DataTypeSingle
	case []int8:
		return DataTypeInt8
	case []uint8:
		return DataTypeUint8
	case []int16:
		return DataTypeInt16
	case []uint16:
		return DataTypeUint16
	case []int32:
		return DataTypeInt32
	case []uint32:
		return DataTypeUint32
	case []int64:
		return DataTypeInt64
	case []uint64:
		return DataTypeUint64
	case []complex128:
		return DataTypeCDouble
	case []complex64:
		return DataTypeCSingle
	case []bool:
		return DataTypeBool
	}
	return DataTypeVoid
}

// ElementNumber returns the element number of an element in a list, pod
// array or int32 keyed map. Elements named with a decimal string are accepted.
func (e *Element) ElementNumber() (int32, error) {
	if e.UseNumber {
		return e.Number, nil
	}
	n, err := strconv.ParseInt(e.Name, 10, 32)
	if err != nil {
		return 0, NewDataTypeError("could not determine element number")
	}
	return int32(n), nil
}

// FindElement returns the element with the given name.
func FindElement(elems []*Element, name string) (*Element, error) {
	for _, e := range elems {
		if !e.UseNumber && e.Name == name {
			return e, nil
		}
	}
	return nil, NewMessageElementNotFound(fmt.Sprintf("element %q not found", name))
}

// nestedOf returns the nested list data of e, checking its type.
func nestedOf(e *Element, types ...DataType) (*NestedList, error) {
	if e == nil {
		return nil, NewNullValue("null element")
	}
	l, ok := e.Data.(*NestedList)
	if !ok {
		if e.Data == nil {
			return nil, nil
		}
		return nil, NewDataTypeMismatch(fmt.Sprintf("element %q: expected nested data", e.Name))
	}
	for _, t := range types {
		if l.Type == t {
			return l, nil
		}
	}
	return nil, NewDataTypeMismatch(fmt.Sprintf("element %q: unexpected data type %v", e.Name, l.Type))
}

// toBaseSlice reinterprets a slice of a named primitive type (e.g. an enum
// or a user defined float type) as the slice of its base type.
func toBaseSlice[T Primitive](v []T) any {
	if v == nil {
		v = []T{}
	}
	p := unsafe.Pointer(unsafe.SliceData(v))
	n := len(v)
	switch DataTypeOf[T]() {
	case DataTypeDouble:
		return unsafe.Slice((*float64)(p), n)
	case DataTypeSingle:
		return unsafe.Slice((*float32)(p), n)
	case DataTypeInt8:
		return unsafe.Slice((*int8)(p), n)
	case DataTypeUint8:
		return unsafe.Slice((*uint8)(p), n)
	case DataTypeInt16:
		return unsafe.Slice((*int16)(p), n)
	case DataTypeUint16:
		return unsafe.Slice((*uint16)(p), n)
	case DataTypeInt32:
		return unsafe.Slice((*int32)(p), n)
	case DataTypeUint32:
		return unsafe.Slice((*uint32)(p), n)
	case DataTypeInt64:
		return unsafe.Slice((*int64)(p), n)
	case DataTypeUint64:
		return unsafe.Slice((*uint64)(p), n)
	case DataTypeCDouble:
		return unsafe.Slice((*complex128)(p), n)
	case DataTypeCSingle:
		return unsafe.Slice((*complex64)(p), n)
	case DataTypeBool:
		return unsafe.Slice((*bool)(p), n)
	}
	panic("robotraconteur: invalid primitive type")
}

// fromBaseSlice converts element data to []T. The data must have exactly the
// data type of T.
func fromBaseSlice[T Primitive](d any) ([]T, bool) {
	if dataTypeOfData(d) != DataTypeOf[T]() {
		return nil, false
	}
	switch v := d.(type) {
	case []T:
		return v, true
	case []float64:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []float32:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []int8:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []uint8:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []int16:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []uint16:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []int32:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []uint32:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []int64:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []uint64:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []complex128:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []complex64:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	case []bool:
		return unsafe.Slice((*T)(unsafe.Pointer(unsafe.SliceData(v))), len(v)), true
	}
	return nil, false
}
