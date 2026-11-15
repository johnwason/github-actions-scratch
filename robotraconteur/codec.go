package robotraconteur

import "fmt"

// Codec constructors used by generated code. Every Robot Raconteur type in a
// service definition maps to a Codec[T] for its Go type T; containers compose
// codecs. Generated structure/pod packing, stub and skeleton code all go
// through codecs, so every pack/unpack returns an error instead of panicking.

func ok(e *Element) (*Element, error) { return e, nil }

// ScalarCodec is the codec for numeric, complex and bool scalars.
func ScalarCodec[T Primitive]() Codec[T] {
	return Codec[T]{
		Pack:   func(n string, v T) (*Element, error) { return ok(PackScalar(n, v)) },
		Unpack: UnpackScalar[T],
	}
}

// ArrayCodec is the codec for numeric arrays. length 0 means unbounded; with
// varlen the length is a maximum, otherwise it is exact.
func ArrayCodec[T Primitive](length int, varlen bool) Codec[[]T] {
	return Codec[[]T]{
		Pack: func(n string, v []T) (*Element, error) {
			if length > 0 {
				if _, err := VerifyArrayLength(v, length, varlen); err != nil {
					return nil, err
				}
			}
			return ok(PackArray(n, v))
		},
		Unpack: func(e *Element) ([]T, error) {
			a, err := UnpackArray[T](e)
			if err != nil || length == 0 {
				return a, err
			}
			return VerifyArrayLength(a, length, varlen)
		},
	}
}

// MultiDimArrayCodec is the codec for numeric multidimarrays. If dims are
// given the array must have exactly that shape.
func MultiDimArrayCodec[T Primitive](dims ...uint32) Codec[*MultiDimArray[T]] {
	return Codec[*MultiDimArray[T]]{
		Pack: func(n string, v *MultiDimArray[T]) (*Element, error) {
			if v != nil && len(dims) > 0 {
				if err := VerifyMultiDimArrayDims(v.Dims, len(v.Array), dims); err != nil {
					return nil, err
				}
			}
			return PackMultiDimArray(n, v)
		},
		Unpack: func(e *Element) (*MultiDimArray[T], error) {
			a, err := UnpackMultiDimArray[T](e)
			if err == nil && len(dims) > 0 {
				err = VerifyMultiDimArrayDims(a.Dims, len(a.Array), dims)
			}
			return a, err
		},
	}
}

// StringCodec is the codec for strings.
func StringCodec() Codec[string] {
	return Codec[string]{
		Pack:   func(n string, v string) (*Element, error) { return ok(PackString(n, v)) },
		Unpack: UnpackString,
	}
}

// VarValueCodec is the codec for varvalue.
func VarValueCodec() Codec[any] {
	return Codec[any]{Pack: PackVarValue, Unpack: UnpackVarValue}
}

// EnumCodec is the codec for enums.
func EnumCodec[T ~int32]() Codec[T] {
	return Codec[T]{
		Pack:   func(n string, v T) (*Element, error) { return ok(PackEnum(n, v)) },
		Unpack: UnpackEnum[T],
	}
}

// StructureCodec is the codec for structures (*T, nil is null).
func StructureCodec[T any, P StructurePtr[T]]() Codec[*T] {
	return Codec[*T]{Pack: PackStructure[T, P], Unpack: UnpackStructure[T, P]}
}

// PodCodec is the codec for a single pod value.
func PodCodec[T Pod, P PodPtr[T]]() Codec[T] {
	return Codec[T]{Pack: PackPodToArray[T], Unpack: UnpackPodFromArray[T, P]}
}

// PodArrayCodec is the codec for pod arrays (see ArrayCodec for length).
func PodArrayCodec[T Pod, P PodPtr[T]](length int, varlen bool) Codec[[]T] {
	return Codec[[]T]{
		Pack: func(n string, v []T) (*Element, error) {
			if length > 0 {
				if _, err := VerifyArrayLength(v, length, varlen); err != nil {
					return nil, err
				}
			}
			return PackPodArray(n, v)
		},
		Unpack: func(e *Element) ([]T, error) {
			a, err := UnpackPodArray[T, P](e)
			if err != nil || length == 0 {
				return a, err
			}
			return VerifyArrayLength(a, length, varlen)
		},
	}
}

// PodMultiDimArrayCodec is the codec for pod multidimarrays.
func PodMultiDimArrayCodec[T Pod, P PodPtr[T]](dims ...uint32) Codec[*PodMultiDimArray[T]] {
	return Codec[*PodMultiDimArray[T]]{
		Pack: func(n string, v *PodMultiDimArray[T]) (*Element, error) {
			if v != nil && len(dims) > 0 {
				if err := VerifyMultiDimArrayDims(v.Dims, len(v.PodArray), dims); err != nil {
					return nil, err
				}
			}
			return PackPodMultiDimArray(n, v)
		},
		Unpack: func(e *Element) (*PodMultiDimArray[T], error) {
			a, err := UnpackPodMultiDimArray[T, P](e)
			if err == nil && len(dims) > 0 {
				err = VerifyMultiDimArrayDims(a.Dims, len(a.PodArray), dims)
			}
			return a, err
		},
	}
}

// NamedArrayCodec is the codec for a single namedarray value.
func NamedArrayCodec[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]]() Codec[T] {
	return Codec[T]{
		Pack:   func(n string, v T) (*Element, error) { return ok(PackNamedArrayToArray[T, E](n, v)) },
		Unpack: UnpackNamedArrayFromArray[T, E, P],
	}
}

// NamedArrayArrayCodec is the codec for namedarray arrays.
func NamedArrayArrayCodec[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](length int, varlen bool) Codec[[]T] {
	return Codec[[]T]{
		Pack: func(n string, v []T) (*Element, error) {
			if length > 0 {
				if _, err := VerifyArrayLength(v, length, varlen); err != nil {
					return nil, err
				}
			}
			return ok(PackNamedArray[T, E](n, v))
		},
		Unpack: func(e *Element) ([]T, error) {
			a, err := UnpackNamedArray[T, E, P](e)
			if err != nil || length == 0 {
				return a, err
			}
			return VerifyArrayLength(a, length, varlen)
		},
	}
}

// NamedArrayMultiDimArrayCodec is the codec for namedarray multidimarrays.
func NamedArrayMultiDimArrayCodec[T NamedArray[E], E Primitive, P NamedArrayPtr[T, E]](dims ...uint32) Codec[*NamedMultiDimArray[T]] {
	return Codec[*NamedMultiDimArray[T]]{
		Pack: func(n string, v *NamedMultiDimArray[T]) (*Element, error) {
			if v != nil && len(dims) > 0 {
				if err := VerifyMultiDimArrayDims(v.Dims, len(v.NamedArrayArray), dims); err != nil {
					return nil, err
				}
			}
			return PackNamedMultiDimArray[T, E](n, v)
		},
		Unpack: func(e *Element) (*NamedMultiDimArray[T], error) {
			a, err := UnpackNamedMultiDimArray[T, E, P](e)
			if err == nil && len(dims) > 0 {
				err = VerifyMultiDimArrayDims(a.Dims, len(a.NamedArrayArray), dims)
			}
			return a, err
		},
	}
}

// ListCodec is the codec for lists ({list}) of the inner type.
func ListCodec[T any](inner Codec[T]) Codec[[]T] {
	return Codec[[]T]{
		Pack:   func(n string, v []T) (*Element, error) { return PackList(n, v, inner.Pack) },
		Unpack: func(e *Element) ([]T, error) { return UnpackList(e, inner.Unpack) },
	}
}

// MapInt32Codec is the codec for int32 keyed maps ({int32}).
func MapInt32Codec[T any](inner Codec[T]) Codec[map[int32]T] {
	return Codec[map[int32]T]{
		Pack:   func(n string, v map[int32]T) (*Element, error) { return PackMapInt32(n, v, inner.Pack) },
		Unpack: func(e *Element) (map[int32]T, error) { return UnpackMapInt32(e, inner.Unpack) },
	}
}

// MapStringCodec is the codec for string keyed maps ({string}).
func MapStringCodec[T any](inner Codec[T]) Codec[map[string]T] {
	return Codec[map[string]T]{
		Pack:   func(n string, v map[string]T) (*Element, error) { return PackMapString(n, v, inner.Pack) },
		Unpack: func(e *Element) (map[string]T, error) { return UnpackMapString(e, inner.Unpack) },
	}
}

// ---- helpers for generated structure/pod code ----

// ElementBuilder collects packed elements, keeping the first error.
type ElementBuilder struct {
	elems []*Element
	err   error
}

// PackField packs v into b using codec c.
func PackField[T any](b *ElementBuilder, name string, v T, c Codec[T]) {
	if b.err != nil {
		return
	}
	e, err := c.Pack(name, v)
	if err != nil {
		b.err = fmt.Errorf("%s: %w", name, err)
		return
	}
	b.elems = append(b.elems, e)
}

// Add appends a packed element (or records its error).
func (b *ElementBuilder) Add(e *Element, err error) {
	if b.err != nil {
		return
	}
	if err != nil {
		b.err = err
		return
	}
	b.elems = append(b.elems, e)
}

// Result returns the packed elements or the first error.
func (b *ElementBuilder) Result() ([]*Element, error) {
	if b.err != nil {
		return nil, b.err
	}
	return b.elems, nil
}

// UnpackField finds the named element and unpacks it with codec c.
func UnpackField[T any](elems []*Element, name string, c Codec[T]) (T, error) {
	var zero T
	e, err := FindElement(elems, name)
	if err != nil {
		return zero, err
	}
	v, err := c.Unpack(e)
	if err != nil {
		return zero, fmt.Errorf("%s: %w", name, err)
	}
	return v, nil
}

// UnpackFixedArray unpacks a numeric array of exactly len(dst) elements into dst.
func UnpackFixedArray[T Primitive](elems []*Element, name string, dst []T) error {
	a, err := UnpackField(elems, name, ArrayCodec[T](len(dst), false))
	if err != nil {
		return err
	}
	copy(dst, a)
	return nil
}

// UnpackReturn unpacks a function/property return value element.
func UnpackReturn[T any](e *Element, c Codec[T]) (T, error) {
	if e == nil {
		var zero T
		return zero, NewNullValue("missing return value")
	}
	return c.Unpack(e)
}

// UnpackFieldInto unpacks a fixed length array field (numeric, pod or
// namedarray) with codec c into dst; the length must equal len(dst).
func UnpackFieldInto[T any](elems []*Element, name string, c Codec[[]T], dst []T) error {
	a, err := UnpackField(elems, name, c)
	if err != nil {
		return err
	}
	if len(a) != len(dst) {
		return NewDataTypeError(fmt.Sprintf("%s: array must have length %d", name, len(dst)))
	}
	copy(dst, a)
	return nil
}
