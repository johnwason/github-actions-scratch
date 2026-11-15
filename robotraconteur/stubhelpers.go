package robotraconteur

import "time"

// Helpers used by generated client stubs.

// GetProperty reads a property and unpacks it with c.
func GetProperty[T any](b *StubBase, name string, c Codec[T]) (T, error) {
	e, err := b.PropertyGet(name)
	if err != nil {
		var zero T
		return zero, err
	}
	return UnpackReturn(e, c)
}

// SetProperty packs v with c and writes the property.
func SetProperty[T any](b *StubBase, name string, v T, c Codec[T]) error {
	e, err := c.Pack("value", v)
	if err != nil {
		return err
	}
	return b.PropertySet(name, e)
}

// AsyncGetProperty reads a property asynchronously.
func AsyncGetProperty[T any](b *StubBase, name string, c Codec[T], timeout time.Duration, handler func(T, error)) {
	b.AsyncPropertyGet(name, timeout, func(e *Element, err error) {
		if err != nil {
			var zero T
			handler(zero, err)
			return
		}
		handler(UnpackReturn(e, c))
	})
}

// AsyncSetProperty writes a property asynchronously.
func AsyncSetProperty[T any](b *StubBase, name string, v T, c Codec[T], timeout time.Duration, handler func(error)) {
	e, err := c.Pack("value", v)
	if err != nil {
		go handler(err)
		return
	}
	b.AsyncPropertySet(name, e, timeout, handler)
}

// CallFunction calls a function with packed args and unpacks the return value.
func CallFunction[T any](b *StubBase, name string, args *ElementBuilder, c Codec[T]) (T, error) {
	var zero T
	a, err := args.Result()
	if err != nil {
		return zero, err
	}
	e, err := b.FunctionCall(name, a)
	if err != nil {
		return zero, err
	}
	return UnpackReturn(e, c)
}

// CallFunctionVoid calls a function without a return value.
func CallFunctionVoid(b *StubBase, name string, args *ElementBuilder) error {
	a, err := args.Result()
	if err != nil {
		return err
	}
	_, err = b.FunctionCall(name, a)
	return err
}

// AsyncCallFunction calls a function asynchronously.
func AsyncCallFunction[T any](b *StubBase, name string, args *ElementBuilder, c Codec[T], timeout time.Duration, handler func(T, error)) {
	a, err := args.Result()
	if err != nil {
		var zero T
		go handler(zero, err)
		return
	}
	b.AsyncFunctionCall(name, a, timeout, func(e *Element, err error) {
		if err != nil {
			var zero T
			handler(zero, err)
			return
		}
		handler(UnpackReturn(e, c))
	})
}

// AsyncCallFunctionVoid calls a function without return value asynchronously.
func AsyncCallFunctionVoid(b *StubBase, name string, args *ElementBuilder, timeout time.Duration, handler func(error)) {
	a, err := args.Result()
	if err != nil {
		go handler(err)
		return
	}
	b.AsyncFunctionCall(name, a, timeout, func(_ *Element, err error) { handler(err) })
}

// AsyncFindObjRefTyped returns a typed object reference asynchronously.
func AsyncFindObjRefTyped[T any](b *StubBase, name, index, objectType string, timeout time.Duration, handler func(T, error)) {
	b.AsyncFindObjRef(name, index, objectType, timeout, func(o any, err error) {
		var zero T
		if err != nil {
			handler(zero, err)
			return
		}
		t, ok := o.(T)
		if !ok {
			handler(zero, NewDataTypeMismatch("object reference type mismatch"))
			return
		}
		handler(t, nil)
	})
}
