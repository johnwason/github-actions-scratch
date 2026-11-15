package robotraconteur

import (
	"fmt"
	"reflect"
)

// CodecProvider is implemented by generated structures, pods and namedarrays
// (and *MultiDimArray[T]) to provide their Codec for CodecFor.
type CodecProvider interface {
	// RRCodec returns a Codec[T] for the implementing type T.
	RRCodec() any
}

// ArrayCodecProvider is implemented by generated pods and namedarrays to
// provide a Codec[[]T] for slices of the type.
type ArrayCodecProvider interface {
	RRArrayCodec() any
}

// CodecFor returns a codec for T for use with subscriptions and other
// generic APIs: primitives and slices of primitives, string, any (varvalue),
// *MultiDimArray[T], generated structures, pods and namedarrays and slices of
// pods and namedarrays. For other types (maps, lists) pass an explicit codec
// built from the codec constructors.
func CodecFor[T any]() (Codec[T], error) {
	var zero T
	var c any
	switch any(zero).(type) {
	case float64:
		c = ScalarCodec[float64]()
	case float32:
		c = ScalarCodec[float32]()
	case int8:
		c = ScalarCodec[int8]()
	case uint8:
		c = ScalarCodec[uint8]()
	case int16:
		c = ScalarCodec[int16]()
	case uint16:
		c = ScalarCodec[uint16]()
	case int32:
		c = ScalarCodec[int32]()
	case uint32:
		c = ScalarCodec[uint32]()
	case int64:
		c = ScalarCodec[int64]()
	case uint64:
		c = ScalarCodec[uint64]()
	case complex128:
		c = ScalarCodec[complex128]()
	case complex64:
		c = ScalarCodec[complex64]()
	case bool:
		c = ScalarCodec[bool]()
	case string:
		c = StringCodec()
	case []float64:
		c = ArrayCodec[float64](0, false)
	case []float32:
		c = ArrayCodec[float32](0, false)
	case []int8:
		c = ArrayCodec[int8](0, false)
	case []uint8:
		c = ArrayCodec[uint8](0, false)
	case []int16:
		c = ArrayCodec[int16](0, false)
	case []uint16:
		c = ArrayCodec[uint16](0, false)
	case []int32:
		c = ArrayCodec[int32](0, false)
	case []uint32:
		c = ArrayCodec[uint32](0, false)
	case []int64:
		c = ArrayCodec[int64](0, false)
	case []uint64:
		c = ArrayCodec[uint64](0, false)
	case []complex128:
		c = ArrayCodec[complex128](0, false)
	case []complex64:
		c = ArrayCodec[complex64](0, false)
	case []bool:
		c = ArrayCodec[bool](0, false)
	}
	if c == nil {
		t := reflect.TypeFor[T]()
		if t.Kind() == reflect.Interface && t.NumMethod() == 0 {
			c = VarValueCodec()
		} else if t.Kind() == reflect.Pointer {
			// *Struct or *MultiDimArray: call RRCodec on a typed nil/new value
			if p, ok := reflect.New(t.Elem()).Interface().(CodecProvider); ok {
				c = p.RRCodec()
			}
		} else if p, ok := any(zero).(CodecProvider); ok {
			c = p.RRCodec()
		} else if t.Kind() == reflect.Slice {
			if p, ok := reflect.Zero(t.Elem()).Interface().(ArrayCodecProvider); ok {
				c = p.RRArrayCodec()
			}
		}
	}
	if tc, ok := c.(Codec[T]); ok {
		return tc, nil
	}
	return Codec[T]{}, NewInvalidArgument(fmt.Sprintf("no codec for type %T; pass an explicit codec", zero))
}

// RRCodec implements CodecProvider.
func (*MultiDimArray[T]) RRCodec() any { return MultiDimArrayCodec[T]() }
