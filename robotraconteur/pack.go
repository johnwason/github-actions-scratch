package robotraconteur

import (
	"fmt"
	"reflect"
)

// Packing functions used by generated code (RobotRaconteurGenGo) and the
// runtime. They convert Go values to and from Element trees using the same
// wire formats as the C++, C#, Java and Python bindings.

// PackFunc packs a value of type T into an element with the given name.
type PackFunc[T any] func(name string, v T) (*Element, error)

// UnpackFunc unpacks a value of type T from an element.
type UnpackFunc[T any] func(e *Element) (T, error)

// Codec packs and unpacks values of type T (used for pipes, wires,
// callbacks, generators and containers).
type Codec[T any] struct {
	Pack   PackFunc[T]
	Unpack UnpackFunc[T]
}

// ---- primitives ----

// PackScalar packs a scalar number, complex or bool.
func PackScalar[T Primitive](name string, v T) *Element {
	return NewElement(name, toBaseSlice([]T{v}))
}

// UnpackScalar unpacks a scalar number, complex or bool.
func UnpackScalar[T Primitive](e *Element) (T, error) {
	var zero T
	a, err := UnpackArray[T](e)
	if err != nil {
		return zero, err
	}
	if len(a) != 1 {
		return zero, NewDataTypeError(fmt.Sprintf("element %q: invalid scalar", e.Name))
	}
	return a[0], nil
}

// PackArray packs a numeric array. A nil slice is packed as an empty array.
func PackArray[T Primitive](name string, v []T) *Element {
	return NewElement(name, toBaseSlice(v))
}

// UnpackArray unpacks a numeric array.
func UnpackArray[T Primitive](e *Element) ([]T, error) {
	if e == nil || e.Data == nil {
		return nil, NewNullValue("null array")
	}
	a, ok := fromBaseSlice[T](e.Data)
	if !ok {
		return nil, NewDataTypeMismatch(fmt.Sprintf("element %q: expected %v array, got %v", e.Name, DataTypeOf[T](), dataTypeOfData(e.Data)))
	}
	return a, nil
}

// PackEnum packs an enum value (transmitted as int32).
func PackEnum[T ~int32](name string, v T) *Element {
	return NewElement(name, []int32{int32(v)})
}

// UnpackEnum unpacks an enum value.
func UnpackEnum[T ~int32](e *Element) (T, error) {
	v, err := UnpackScalar[int32](e)
	return T(v), err
}

// PackString packs a string.
func PackString(name string, v string) *Element {
	return NewElement(name, v)
}

// UnpackString unpacks a string.
func UnpackString(e *Element) (string, error) {
	if e == nil || e.Data == nil {
		return "", NewNullValue("null string")
	}
	s, ok := e.Data.(string)
	if !ok {
		return "", NewDataTypeMismatch(fmt.Sprintf("element %q: expected string", e.Name))
	}
	return s, nil
}

// ---- numeric multidimarrays ----

// PackMultiDimArray packs a numeric multi-dimensional array.
func PackMultiDimArray[T Primitive](name string, v *MultiDimArray[T]) (*Element, error) {
	if v == nil {
		return nil, NewNullValue("null multidimarray")
	}
	if dimsCount(v.Dims) != len(v.Array) {
		return nil, NewDataTypeError("multidimarray dims do not match array length")
	}
	return NewElement(name, &NestedList{Type: DataTypeMultiDimArray, Elements: []*Element{
		PackArray("dims", v.Dims), PackArray("array", v.Array)}}), nil
}

// UnpackMultiDimArray unpacks a numeric multi-dimensional array.
func UnpackMultiDimArray[T Primitive](e *Element) (*MultiDimArray[T], error) {
	l, err := nestedOf(e, DataTypeMultiDimArray)
	if err != nil {
		return nil, err
	}
	if l == nil {
		return nil, NewNullValue("null multidimarray")
	}
	return unpackMultiDimArrayList[T](l)
}

func unpackMultiDimArrayList[T Primitive](l *NestedList) (*MultiDimArray[T], error) {
	de, err := FindElement(l.Elements, "dims")
	if err != nil {
		return nil, err
	}
	dims, err := UnpackArray[uint32](de)
	if err != nil {
		return nil, err
	}
	ae, err := FindElement(l.Elements, "array")
	if err != nil {
		return nil, err
	}
	a, err := UnpackArray[T](ae)
	if err != nil {
		return nil, err
	}
	if dimsCount(dims) != len(a) {
		return nil, NewDataTypeError("multidimarray dims do not match array length")
	}
	return &MultiDimArray[T]{Dims: dims, Array: a}, nil
}

// ---- structures ----

// Structure is implemented by generated Robot Raconteur structure types
// (pointer receivers). A nil *T packs as a null structure.
type Structure interface {
	RRType() string
	RRPack() ([]*Element, error)
	RRUnpack(elems []*Element) error
}

// StructurePtr constrains P to be *T implementing Structure.
type StructurePtr[T any] interface {
	*T
	Structure
}

// PackStructure packs a structure. A nil pointer packs as null.
func PackStructure[T any, P StructurePtr[T]](name string, v *T) (*Element, error) {
	if v == nil {
		return NewElement(name, nil), nil
	}
	return packStructureValue(name, P(v))
}

func packStructureValue(name string, s Structure) (*Element, error) {
	elems, err := s.RRPack()
	if err != nil {
		return nil, err
	}
	return NewElement(name, &NestedList{Type: DataTypeStructure, TypeName: s.RRType(), Elements: elems}), nil
}

// UnpackStructure unpacks a structure. A null element unpacks as nil.
func UnpackStructure[T any, P StructurePtr[T]](e *Element) (*T, error) {
	l, err := nestedOf(e, DataTypeStructure)
	if err != nil || l == nil {
		return nil, err
	}
	v := new(T)
	p := P(v)
	if l.TypeName != p.RRType() {
		return nil, NewDataTypeMismatch(fmt.Sprintf("structure type mismatch: expected %s, got %s", p.RRType(), l.TypeName))
	}
	if err := p.RRUnpack(l.Elements); err != nil {
		return nil, err
	}
	return v, nil
}

// ---- pods ----

// Pod is implemented by generated pod types (value receivers).
type Pod interface {
	RRType() string
	RRPackPod() ([]*Element, error)
}

// PodPtr constrains P to be *T where T is a pod that can be unpacked.
type PodPtr[T any] interface {
	*T
	RRUnpackPod(elems []*Element) error
}

func packPodArrayList[T Pod](v []T) (*NestedList, error) {
	var zero T
	elems := make([]*Element, len(v))
	for i := range v {
		pe, err := v[i].RRPackPod()
		if err != nil {
			return nil, err
		}
		elems[i] = NewNumberedElement(int32(i), &NestedList{Type: DataTypePod, Elements: pe})
	}
	return &NestedList{Type: DataTypePodArray, TypeName: zero.RRType(), Elements: elems}, nil
}

// PackPodToArray packs a single pod (as a pod array of length one).
func PackPodToArray[T Pod](name string, v T) (*Element, error) {
	l, err := packPodArrayList([]T{v})
	if err != nil {
		return nil, err
	}
	return NewElement(name, l), nil
}

// PackPodArray packs a pod array.
func PackPodArray[T Pod](name string, v []T) (*Element, error) {
	l, err := packPodArrayList(v)
	if err != nil {
		return nil, err
	}
	return NewElement(name, l), nil
}

// PackPodMultiDimArray packs a pod multi-dimensional array.
func PackPodMultiDimArray[T Pod](name string, v *PodMultiDimArray[T]) (*Element, error) {
	if v == nil {
		return nil, NewNullValue("null pod multidimarray")
	}
	if dimsCount(v.Dims) != len(v.PodArray) {
		return nil, NewDataTypeError("pod multidimarray dims do not match array length")
	}
	l, err := packPodArrayList(v.PodArray)
	if err != nil {
		return nil, err
	}
	var zero T
	return NewElement(name, &NestedList{Type: DataTypePodMultiDimArray, TypeName: zero.RRType(),
		Elements: []*Element{PackArray("dims", v.Dims), NewElement("array", l)}}), nil
}

func unpackPodArrayList[T Pod, P PodPtr[T]](l *NestedList) ([]T, error) {
	var zero T
	if l.TypeName != zero.RRType() {
		return nil, NewDataTypeMismatch(fmt.Sprintf("pod type mismatch: expected %s, got %s", zero.RRType(), l.TypeName))
	}
	o := make([]T, len(l.Elements))
	for i, e := range l.Elements {
		n, err := e.ElementNumber()
		if err != nil {
			return nil, err
		}
		if int(n) != i {
			return nil, NewDataTypeError("error in pod array format")
		}
		pl, err := nestedOf(e, DataTypePod)
		if err != nil {
			return nil, err
		}
		if pl == nil {
			return nil, NewNullValue("null pod")
		}
		if err := P(&o[i]).RRUnpackPod(pl.Elements); err != nil {
			return nil, err
		}
	}
	return o, nil
}

// UnpackPodFromArray unpacks a single pod.
func UnpackPodFromArray[T Pod, P PodPtr[T]](e *Element) (T, error) {
	var zero T
	a, err := UnpackPodArray[T, P](e)
	if err != nil {
		return zero, err
	}
	if len(a) != 1 {
		return zero, NewDataTypeError("pod array must have length one")
	}
	return a[0], nil
}

// UnpackPodArray unpacks a pod array.
func UnpackPodArray[T Pod, P PodPtr[T]](e *Element) ([]T, error) {
	l, err := nestedOf(e, DataTypePodArray)
	if err != nil {
		return nil, err
	}
	if l == nil {
		return nil, NewNullValue("null pod array")
	}
	return unpackPodArrayList[T, P](l)
}

// UnpackPodMultiDimArray unpacks a pod multi-dimensional array.
func UnpackPodMultiDimArray[T Pod, P PodPtr[T]](e *Element) (*PodMultiDimArray[T], error) {
	l, err := nestedOf(e, DataTypePodMultiDimArray)
	if err != nil {
		return nil, err
	}
	if l == nil {
		return nil, NewNullValue("null pod multidimarray")
	}
	return unpackPodMultiDimArrayList[T, P](l)
}

func unpackPodMultiDimArrayList[T Pod, P PodPtr[T]](l *NestedList) (*PodMultiDimArray[T], error) {
	var zero T
	if l.TypeName != zero.RRType() {
		return nil, NewDataTypeMismatch("pod type mismatch")
	}
	de, err := FindElement(l.Elements, "dims")
	if err != nil {
		return nil, err
	}
	dims, err := UnpackArray[uint32](de)
	if err != nil {
		return nil, err
	}
	ae, err := FindElement(l.Elements, "array")
	if err != nil {
		return nil, err
	}
	a, err := UnpackPodArray[T, P](ae)
	if err != nil {
		return nil, err
	}
	if dimsCount(dims) != len(a) {
		return nil, NewDataTypeError("pod multidimarray dims do not match array length")
	}
	return &PodMultiDimArray[T]{Dims: dims, PodArray: a}, nil
}

// ---- namedarrays ----

// NamedArray is implemented by generated namedarray types (value receivers).
// E is the namedarray's numeric element type.
type NamedArray[E Primitive] interface {
	RRType() string
	// RRElementCount is the number of E elements in one namedarray value.
	RRElementCount() int
	// RRToArray writes the value into dst (len RRElementCount()).
	RRToArray(dst []E)
}

// NamedArrayPtr constrains P to be *T for a namedarray T with element type E.
type NamedArrayPtr[T any, E Primitive] interface {
	*T
	// RRFromArray reads the value from src (len RRElementCount()).
	RRFromArray(src []E)
}

func namedArrayToNumeric[T NamedArray[E], E Primitive](v []T) []E {
	var zero T
	n := zero.RRElementCount()
	a := make([]E, n*len(v))
	for i := range v {
		v[i].RRToArray(a[i*n : (i+1)*n])
	}
	return a
}

func numericToNamedArray[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](a []E) ([]T, error) {
	var zero T
	n := zero.RRElementCount()
	if n == 0 || len(a)%n != 0 {
		return nil, NewDataTypeError("invalid namedarray array length")
	}
	o := make([]T, len(a)/n)
	for i := range o {
		P(&o[i]).RRFromArray(a[i*n : (i+1)*n])
	}
	return o, nil
}

func packNamedArrayList[T NamedArray[E], E Primitive](v []T) *NestedList {
	var zero T
	return &NestedList{Type: DataTypeNamedArrayArray, TypeName: zero.RRType(),
		Elements: []*Element{PackArray("array", namedArrayToNumeric[T, E](v))}}
}

// PackNamedArrayToArray packs a single namedarray value.
func PackNamedArrayToArray[T NamedArray[E], E Primitive](name string, v T) *Element {
	return NewElement(name, packNamedArrayList[T, E]([]T{v}))
}

// PackNamedArray packs a namedarray array.
func PackNamedArray[T NamedArray[E], E Primitive](name string, v []T) *Element {
	return NewElement(name, packNamedArrayList[T, E](v))
}

// PackNamedMultiDimArray packs a namedarray multi-dimensional array.
func PackNamedMultiDimArray[T NamedArray[E], E Primitive](name string, v *NamedMultiDimArray[T]) (*Element, error) {
	if v == nil {
		return nil, NewNullValue("null namedarray multidimarray")
	}
	if dimsCount(v.Dims) != len(v.NamedArrayArray) {
		return nil, NewDataTypeError("namedarray multidimarray dims do not match array length")
	}
	var zero T
	return NewElement(name, &NestedList{Type: DataTypeNamedArrayMultiDimArray, TypeName: zero.RRType(),
		Elements: []*Element{PackArray("dims", v.Dims),
			NewElement("array", packNamedArrayList[T, E](v.NamedArrayArray))}}), nil
}

func unpackNamedArrayList[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](l *NestedList) ([]T, error) {
	var zero T
	if l.TypeName != zero.RRType() {
		return nil, NewDataTypeMismatch(fmt.Sprintf("namedarray type mismatch: expected %s, got %s", zero.RRType(), l.TypeName))
	}
	if len(l.Elements) != 1 {
		return nil, NewDataTypeError("invalid namedarray format")
	}
	ae, err := FindElement(l.Elements, "array")
	if err != nil {
		return nil, err
	}
	a, err := UnpackArray[E](ae)
	if err != nil {
		return nil, err
	}
	return numericToNamedArray[T, E, P](a)
}

// UnpackNamedArrayFromArray unpacks a single namedarray value.
func UnpackNamedArrayFromArray[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](e *Element) (T, error) {
	var zero T
	a, err := UnpackNamedArray[T, E, P](e)
	if err != nil {
		return zero, err
	}
	if len(a) != 1 {
		return zero, NewDataTypeError("namedarray array must have length one")
	}
	return a[0], nil
}

// UnpackNamedArray unpacks a namedarray array.
func UnpackNamedArray[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](e *Element) ([]T, error) {
	l, err := nestedOf(e, DataTypeNamedArrayArray)
	if err != nil {
		return nil, err
	}
	if l == nil {
		return nil, NewNullValue("null namedarray array")
	}
	return unpackNamedArrayList[T, E, P](l)
}

// UnpackNamedMultiDimArray unpacks a namedarray multi-dimensional array.
func UnpackNamedMultiDimArray[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](e *Element) (*NamedMultiDimArray[T], error) {
	l, err := nestedOf(e, DataTypeNamedArrayMultiDimArray)
	if err != nil {
		return nil, err
	}
	if l == nil {
		return nil, NewNullValue("null namedarray multidimarray")
	}
	return unpackNamedMultiDimArrayList[T, E, P](l)
}

func unpackNamedMultiDimArrayList[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](l *NestedList) (*NamedMultiDimArray[T], error) {
	var zero T
	if l.TypeName != zero.RRType() {
		return nil, NewDataTypeMismatch("namedarray type mismatch")
	}
	de, err := FindElement(l.Elements, "dims")
	if err != nil {
		return nil, err
	}
	dims, err := UnpackArray[uint32](de)
	if err != nil {
		return nil, err
	}
	ae, err := FindElement(l.Elements, "array")
	if err != nil {
		return nil, err
	}
	a, err := UnpackNamedArray[T, E, P](ae)
	if err != nil {
		return nil, err
	}
	if dimsCount(dims) != len(a) {
		return nil, NewDataTypeError("namedarray multidimarray dims do not match array length")
	}
	return &NamedMultiDimArray[T]{Dims: dims, NamedArrayArray: a}, nil
}

// ---- containers ----

// PackMapInt32 packs an int32 keyed map ({int32}). A nil map packs as null.
func PackMapInt32[T any](name string, m map[int32]T, pack PackFunc[T]) (*Element, error) {
	if m == nil {
		return NewElement(name, nil), nil
	}
	elems := make([]*Element, 0, len(m))
	for k, v := range m {
		e, err := pack("", v)
		if err != nil {
			return nil, err
		}
		e.Name, e.Number, e.UseNumber = "", k, true
		elems = append(elems, e)
	}
	return NewElement(name, &NestedList{Type: DataTypeVector, Elements: elems}), nil
}

// UnpackMapInt32 unpacks an int32 keyed map. A null element unpacks as nil.
func UnpackMapInt32[T any](e *Element, unpack UnpackFunc[T]) (map[int32]T, error) {
	l, err := nestedOf(e, DataTypeVector)
	if err != nil || l == nil {
		return nil, err
	}
	o := make(map[int32]T, len(l.Elements))
	for _, el := range l.Elements {
		k, err := el.ElementNumber()
		if err != nil {
			return nil, err
		}
		v, err := unpack(el)
		if err != nil {
			return nil, err
		}
		o[k] = v
	}
	return o, nil
}

// PackMapString packs a string keyed map ({string}). A nil map packs as null.
func PackMapString[T any](name string, m map[string]T, pack PackFunc[T]) (*Element, error) {
	if m == nil {
		return NewElement(name, nil), nil
	}
	elems := make([]*Element, 0, len(m))
	for k, v := range m {
		e, err := pack(k, v)
		if err != nil {
			return nil, err
		}
		e.Name, e.UseNumber = k, false
		elems = append(elems, e)
	}
	return NewElement(name, &NestedList{Type: DataTypeDictionary, Elements: elems}), nil
}

// UnpackMapString unpacks a string keyed map. A null element unpacks as nil.
func UnpackMapString[T any](e *Element, unpack UnpackFunc[T]) (map[string]T, error) {
	l, err := nestedOf(e, DataTypeDictionary)
	if err != nil || l == nil {
		return nil, err
	}
	o := make(map[string]T, len(l.Elements))
	for _, el := range l.Elements {
		v, err := unpack(el)
		if err != nil {
			return nil, err
		}
		o[el.Name] = v
	}
	return o, nil
}

// PackList packs a list ({list}). A nil slice packs as null.
func PackList[T any](name string, a []T, pack PackFunc[T]) (*Element, error) {
	if a == nil {
		return NewElement(name, nil), nil
	}
	elems := make([]*Element, len(a))
	for i, v := range a {
		e, err := pack("", v)
		if err != nil {
			return nil, err
		}
		e.Name, e.Number, e.UseNumber = "", int32(i), true
		elems[i] = e
	}
	return NewElement(name, &NestedList{Type: DataTypeList, Elements: elems}), nil
}

// UnpackList unpacks a list. A null element unpacks as nil.
func UnpackList[T any](e *Element, unpack UnpackFunc[T]) ([]T, error) {
	l, err := nestedOf(e, DataTypeList)
	if err != nil || l == nil {
		return nil, err
	}
	o := make([]T, len(l.Elements))
	for i, el := range l.Elements {
		n, err := el.ElementNumber()
		if err != nil {
			return nil, err
		}
		if int(n) != i {
			return nil, NewDataTypeError("error in list format")
		}
		if o[i], err = unpack(el); err != nil {
			return nil, err
		}
	}
	return o, nil
}

// ---- varvalue ----

// typeInfo describes a registered structure, pod or namedarray type so it can
// be packed/unpacked as a varvalue.
type typeInfo struct {
	kind            DataType // DataTypeStructure, DataTypePod or DataTypeNamedArray
	newStructure    func() Structure
	packValue       func(v any) (*NestedList, bool, error) // value, []value or multidim
	unpackArray     func(l *NestedList) (any, error)
	unpackMultiDim  func(l *NestedList) (any, error)
}

var typeRegistry = map[string]*typeInfo{}

// RegisterStructureType registers a generated structure type for varvalue
// unpacking. Called from generated init functions.
func RegisterStructureType[T any, P StructurePtr[T]]() {
	var p P = new(T)
	typeRegistry[p.RRType()] = &typeInfo{kind: DataTypeStructure, newStructure: func() Structure { return P(new(T)) }}
}

// RegisterPodType registers a generated pod type for varvalue use.
func RegisterPodType[T Pod, P PodPtr[T]]() {
	var zero T
	typeRegistry[zero.RRType()] = &typeInfo{
		kind: DataTypePod,
		packValue: func(v any) (*NestedList, bool, error) {
			switch x := v.(type) {
			case T:
				l, err := packPodArrayList([]T{x})
				return l, true, err
			case []T:
				l, err := packPodArrayList(x)
				return l, true, err
			case *PodMultiDimArray[T]:
				e, err := PackPodMultiDimArray("", x)
				if err != nil {
					return nil, true, err
				}
				return e.Data.(*NestedList), true, nil
			}
			return nil, false, nil
		},
		unpackArray:    func(l *NestedList) (any, error) { return unpackPodArrayList[T, P](l) },
		unpackMultiDim: func(l *NestedList) (any, error) { return unpackPodMultiDimArrayList[T, P](l) },
	}
}

// RegisterNamedArrayType registers a generated namedarray type for varvalue use.
func RegisterNamedArrayType[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]]() {
	var zero T
	typeRegistry[zero.RRType()] = &typeInfo{
		kind: DataTypeNamedArray,
		packValue: func(v any) (*NestedList, bool, error) {
			switch x := v.(type) {
			case T:
				return packNamedArrayList[T, E]([]T{x}), true, nil
			case []T:
				return packNamedArrayList[T, E](x), true, nil
			case *NamedMultiDimArray[T]:
				e, err := PackNamedMultiDimArray[T, E]("", x)
				if err != nil {
					return nil, true, err
				}
				return e.Data.(*NestedList), true, nil
			}
			return nil, false, nil
		},
		unpackArray:    func(l *NestedList) (any, error) { return unpackNamedArrayList[T, E, P](l) },
		unpackMultiDim: func(l *NestedList) (any, error) { return unpackNamedMultiDimArrayList[T, E, P](l) },
	}
}

// multiDimArrayPacker is implemented by *MultiDimArray[T] for varvalue packing.
type multiDimArrayPacker interface {
	packMultiDimArray(name string) (*Element, error)
}

func (m *MultiDimArray[T]) packMultiDimArray(name string) (*Element, error) {
	return PackMultiDimArray(name, m)
}

// PackVarValue packs a varvalue. Supported Go values: nil; numeric, complex
// and bool scalars and slices; string; *MultiDimArray[T]; generated
// structures (pointers), pods and namedarrays (values, slices and multidim
// arrays); map[int32]any, map[string]any and []any containing varvalues.
func PackVarValue(name string, v any) (*Element, error) {
	switch x := v.(type) {
	case nil:
		return NewElement(name, nil), nil
	case string:
		return PackString(name, x), nil
	case float64:
		return PackScalar(name, x), nil
	case float32:
		return PackScalar(name, x), nil
	case int8:
		return PackScalar(name, x), nil
	case uint8:
		return PackScalar(name, x), nil
	case int16:
		return PackScalar(name, x), nil
	case uint16:
		return PackScalar(name, x), nil
	case int32:
		return PackScalar(name, x), nil
	case uint32:
		return PackScalar(name, x), nil
	case int64:
		return PackScalar(name, x), nil
	case uint64:
		return PackScalar(name, x), nil
	case complex128:
		return PackScalar(name, x), nil
	case complex64:
		return PackScalar(name, x), nil
	case bool:
		return PackScalar(name, x), nil
	case []float64, []float32, []int8, []uint8, []int16, []uint16, []int32, []uint32, []int64, []uint64,
		[]complex128, []complex64, []bool:
		return NewElement(name, x), nil
	case multiDimArrayPacker:
		return x.packMultiDimArray(name)
	case map[int32]any:
		return PackMapInt32(name, x, PackVarValue)
	case map[string]any:
		return PackMapString(name, x, PackVarValue)
	case []any:
		return PackList(name, x, PackVarValue)
	case Structure:
		if rv := reflect.ValueOf(x); rv.Kind() == reflect.Pointer && rv.IsNil() {
			return NewElement(name, nil), nil
		}
		return packStructureValue(name, x)
	}
	// pods and namedarrays: find the registered type by value, slice or multidim element type
	for _, ti := range typeRegistry {
		if ti.packValue == nil {
			continue
		}
		l, ok, err := ti.packValue(v)
		if ok {
			if err != nil {
				return nil, err
			}
			return NewElement(name, l), nil
		}
	}
	return nil, NewDataTypeError(fmt.Sprintf("invalid varvalue type %T", v))
}

// UnpackVarValue unpacks a varvalue. Numeric data unpacks as slices (as in
// the other Robot Raconteur bindings, scalars are length one slices).
func UnpackVarValue(e *Element) (any, error) {
	if e == nil || e.Data == nil {
		return nil, nil
	}
	switch d := e.Data.(type) {
	case string:
		return d, nil
	case *NestedList:
		switch d.Type {
		case DataTypeStructure:
			ti := typeRegistry[d.TypeName]
			if ti == nil || ti.newStructure == nil {
				return nil, NewDataTypeError("unknown structure type " + d.TypeName)
			}
			s := ti.newStructure()
			if err := s.RRUnpack(d.Elements); err != nil {
				return nil, err
			}
			return s, nil
		case DataTypePodArray, DataTypeNamedArrayArray:
			ti := typeRegistry[d.TypeName]
			if ti == nil || ti.unpackArray == nil {
				return nil, NewDataTypeError("unknown type " + d.TypeName)
			}
			return ti.unpackArray(d)
		case DataTypePodMultiDimArray, DataTypeNamedArrayMultiDimArray:
			ti := typeRegistry[d.TypeName]
			if ti == nil || ti.unpackMultiDim == nil {
				return nil, NewDataTypeError("unknown type " + d.TypeName)
			}
			return ti.unpackMultiDim(d)
		case DataTypeMultiDimArray:
			return unpackMultiDimArrayAny(d)
		case DataTypeVector:
			return UnpackMapInt32(e, UnpackVarValue)
		case DataTypeDictionary:
			return UnpackMapString(e, UnpackVarValue)
		case DataTypeList:
			return UnpackList(e, UnpackVarValue)
		}
		return nil, NewDataTypeError(fmt.Sprintf("invalid varvalue data type %v", d.Type))
	default:
		if dataTypeOfData(d) != DataTypeVoid {
			return d, nil
		}
	}
	return nil, NewDataTypeError("invalid varvalue data")
}

func unpackMultiDimArrayAny(l *NestedList) (any, error) {
	ae, err := FindElement(l.Elements, "array")
	if err != nil {
		return nil, err
	}
	switch dataTypeOfData(ae.Data) {
	case DataTypeDouble:
		return unpackMultiDimArrayList[float64](l)
	case DataTypeSingle:
		return unpackMultiDimArrayList[float32](l)
	case DataTypeInt8:
		return unpackMultiDimArrayList[int8](l)
	case DataTypeUint8:
		return unpackMultiDimArrayList[uint8](l)
	case DataTypeInt16:
		return unpackMultiDimArrayList[int16](l)
	case DataTypeUint16:
		return unpackMultiDimArrayList[uint16](l)
	case DataTypeInt32:
		return unpackMultiDimArrayList[int32](l)
	case DataTypeUint32:
		return unpackMultiDimArrayList[uint32](l)
	case DataTypeInt64:
		return unpackMultiDimArrayList[int64](l)
	case DataTypeUint64:
		return unpackMultiDimArrayList[uint64](l)
	case DataTypeCDouble:
		return unpackMultiDimArrayList[complex128](l)
	case DataTypeCSingle:
		return unpackMultiDimArrayList[complex64](l)
	case DataTypeBool:
		return unpackMultiDimArrayList[bool](l)
	}
	return nil, NewDataTypeError("invalid multidimarray element type")
}
