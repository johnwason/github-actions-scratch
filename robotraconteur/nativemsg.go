package robotraconteur

import (
	"fmt"
	"unsafe"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// Conversion between Go Element trees and native RobotRaconteur::MessageElement.
// This is the only code that creates and deletes native message handles.

const (
	elementFlagNameStr = 0x01
	elementFlagNumber  = 0x04
)

// toNative converts e to a new native MessageElement (caller deletes it).
// Must be called with the error slot armed (inside call/callVoid).
func toNative(e *Element) (swig.MessageElement, error) {
	m := swig.NewMessageElement()
	if e.UseNumber {
		m.SetElementNumber(e.Number)
		m.SetElementFlags((m.GetElementFlags() &^ elementFlagNameStr) | elementFlagNumber)
	} else {
		m.SetElementName(e.Name)
	}
	if err := setNativeData(m, e.Data); err != nil {
		swig.DeleteMessageElement(m)
		return nil, err
	}
	return m, nil
}

func setNativeData(m swig.MessageElement, data any) error {
	switch d := data.(type) {
	case nil:
		m.SetData(nil)
		return nil
	case string:
		b := []byte(d)
		arr := swig.AllocateRRArrayByType(swig.DataTypes_string_t, int64(len(b)))
		if arr == nil || arr.Swigcptr() == 0 {
			return NewInternalError("could not allocate string")
		}
		if len(b) > 0 {
			copy(unsafe.Slice((*byte)(unsafe.Pointer(arr.Void_ptr())), len(b)), b)
		}
		setArray(m, arr)
		return nil
	case *NestedList:
		vec := swig.NewVectorptr_messageelement()
		defer swig.DeleteVectorptr_messageelement(vec)
		vec.Reserve(int64(len(d.Elements)))
		for _, sub := range d.Elements {
			if sub == nil {
				return NewNullValue("null element in nested list")
			}
			sm, err := toNative(sub)
			if err != nil {
				return err
			}
			vec.Add(sm)
			swig.DeleteMessageElement(sm)
		}
		nl := swig.NewMessageElementNestedElementList(swig.RobotRaconteurDataTypes(d.Type), d.TypeName, vec)
		if nl == nil || nl.Swigcptr() == 0 {
			return NewInternalError("could not create nested element list")
		}
		med := nl.SwigGetMessageElementData()
		m.SetData(med)
		swig.DeleteMessageElementData(med)
		swig.DeleteMessageElementNestedElementList(nl)
		return nil
	}
	dt := dataTypeOfData(data)
	if dt == DataTypeVoid {
		return NewDataTypeError(fmt.Sprintf("invalid element data type %T", data))
	}
	n, src, size := sliceBytes(data)
	arr := swig.AllocateRRArrayByType(swig.RobotRaconteurDataTypes(dt), int64(n))
	if arr == nil || arr.Swigcptr() == 0 {
		return NewInternalError("could not allocate array")
	}
	if n > 0 {
		dst := unsafe.Slice((*byte)(unsafe.Pointer(arr.Void_ptr())), n*size)
		if b, ok := data.([]bool); ok {
			for i, v := range b {
				if v {
					dst[i] = 1
				} else {
					dst[i] = 0
				}
			}
		} else {
			copy(dst, unsafe.Slice((*byte)(src), n*size))
		}
	}
	setArray(m, arr)
	return nil
}

func setArray(m swig.MessageElement, arr swig.RRBaseArray) {
	med := arr.SwigGetMessageElementData()
	m.SetData(med)
	swig.DeleteMessageElementData(med)
	swig.DeleteRRBaseArray(arr)
}

// sliceBytes returns the length, data pointer and element size of a base
// numeric slice.
func sliceBytes(data any) (int, unsafe.Pointer, int) {
	switch d := data.(type) {
	case []float64:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 8
	case []float32:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 4
	case []int8:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 1
	case []uint8:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 1
	case []int16:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 2
	case []uint16:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 2
	case []int32:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 4
	case []uint32:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 4
	case []int64:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 8
	case []uint64:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 8
	case []complex128:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 16
	case []complex64:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 8
	case []bool:
		return len(d), unsafe.Pointer(unsafe.SliceData(d)), 1
	}
	return 0, nil, 0
}

// fromNative converts a native MessageElement to a Go Element. The native
// element is not deleted.
func fromNative(m swig.MessageElement) (*Element, error) {
	if m == nil || m.Swigcptr() == 0 {
		return nil, nil
	}
	e := &Element{
		Type:     DataType(m.GetElementType()),
		TypeName: m.GetElementTypeName(),
	}
	flags := m.GetElementFlags()
	if flags&elementFlagNumber != 0 {
		e.Number, e.UseNumber = m.GetElementNumber(), true
	} else {
		e.Name = m.GetElementName()
	}
	data := m.GetData()
	if data == nil || data.Swigcptr() == 0 {
		e.Type = DataTypeVoid
		return e, nil
	}
	defer swig.DeleteMessageElementData(data)
	var err error
	e.Data, err = nativeData(data)
	return e, err
}

func nativeData(data swig.MessageElementData) (any, error) {
	dt := DataType(data.GetTypeID())
	if dt.IsNumber() || dt == DataTypeString {
		arr := swig.MessageElementDataUtilToRRBaseArray(data)
		if arr == nil || arr.Swigcptr() == 0 {
			return nil, NewDataTypeError("invalid array data")
		}
		defer swig.DeleteRRBaseArray(arr)
		n := int(arr.Size())
		var p unsafe.Pointer
		if n > 0 {
			p = unsafe.Pointer(arr.Void_ptr())
		}
		return copyNativeArray(dt, p, n)
	}
	nl := swig.MessageElementDataUtilToMessageElementNestedElementList(data)
	if nl == nil || nl.Swigcptr() == 0 {
		return nil, NewDataTypeError(fmt.Sprintf("unknown element data type %v", dt))
	}
	defer swig.DeleteMessageElementNestedElementList(nl)
	l := &NestedList{Type: DataType(nl.GetType()), TypeName: nl.GetTypeName()}
	vec := nl.GetElements() // owned by nl
	n := int(vec.Size())
	l.Elements = make([]*Element, n)
	for i := 0; i < n; i++ {
		sm := vec.Get(i)
		se, err := fromNative(sm)
		swig.DeleteMessageElement(sm)
		if err != nil {
			return nil, err
		}
		l.Elements[i] = se
	}
	return l, nil
}

func copyNativeArray(dt DataType, p unsafe.Pointer, n int) (any, error) {
	switch dt {
	case DataTypeString:
		if n == 0 {
			return "", nil
		}
		return string(unsafe.Slice((*byte)(p), n)), nil
	case DataTypeDouble:
		return copyN[float64](p, n), nil
	case DataTypeSingle:
		return copyN[float32](p, n), nil
	case DataTypeInt8:
		return copyN[int8](p, n), nil
	case DataTypeUint8:
		return copyN[uint8](p, n), nil
	case DataTypeInt16:
		return copyN[int16](p, n), nil
	case DataTypeUint16:
		return copyN[uint16](p, n), nil
	case DataTypeInt32:
		return copyN[int32](p, n), nil
	case DataTypeUint32:
		return copyN[uint32](p, n), nil
	case DataTypeInt64:
		return copyN[int64](p, n), nil
	case DataTypeUint64:
		return copyN[uint64](p, n), nil
	case DataTypeCDouble:
		return copyN[complex128](p, n), nil
	case DataTypeCSingle:
		return copyN[complex64](p, n), nil
	case DataTypeBool:
		o := make([]bool, n)
		if n > 0 {
			b := unsafe.Slice((*byte)(p), n)
			for i := range o {
				o[i] = b[i] != 0
			}
		}
		return o, nil
	}
	return nil, NewDataTypeError(fmt.Sprintf("invalid array data type %v", dt))
}

func copyN[T any](p unsafe.Pointer, n int) []T {
	o := make([]T, n)
	if n > 0 {
		copy(o, unsafe.Slice((*T)(p), n))
	}
	return o
}

// elementsToNative converts a list of elements to a new native vector
// (caller deletes it).
func elementsToNative(elems []*Element) (swig.Vectorptr_messageelement, error) {
	vec := swig.NewVectorptr_messageelement()
	for _, e := range elems {
		m, err := toNative(e)
		if err != nil {
			swig.DeleteVectorptr_messageelement(vec)
			return nil, err
		}
		vec.Add(m)
		swig.DeleteMessageElement(m)
	}
	return vec, nil
}

// elementsFromNative converts a native vector of elements to Go.
func elementsFromNative(vec swig.Vectorptr_messageelement) ([]*Element, error) {
	n := int(vec.Size())
	o := make([]*Element, n)
	for i := 0; i < n; i++ {
		m := vec.Get(i)
		e, err := fromNative(m)
		swig.DeleteMessageElement(m)
		if err != nil {
			return nil, err
		}
		o[i] = e
	}
	return o, nil
}

// elementRoundTrip converts an element to native form and back. It is used
// by tests to verify wire format compatibility of packed values.
func elementRoundTrip(e *Element) (*Element, error) {
	var out *Element
	var cerr error
	err := callVoid(func() {
		m, err := toNative(e)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteMessageElement(m)
		out, cerr = fromNative(m)
	})
	if err != nil {
		return nil, err
	}
	return out, cerr
}
