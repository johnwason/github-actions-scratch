package robotraconteur

import "context"

// Helpers used by generated client stubs.

// GetProperty reads a property and unpacks it with c.
func GetProperty[T any](ctx context.Context, b *StubBase, name string, c Codec[T]) (T, error) {
	e, err := b.RRPropertyGet(ctx, name)
	if err != nil {
		var zero T
		return zero, err
	}
	return UnpackReturn(e, c)
}

// SetProperty packs v with c and writes the property.
func SetProperty[T any](ctx context.Context, b *StubBase, name string, v T, c Codec[T]) error {
	e, err := c.Pack("value", v)
	if err != nil {
		return err
	}
	return b.RRPropertySet(ctx, name, e)
}

// CallFunction calls a function with packed args and unpacks the return value.
func CallFunction[T any](ctx context.Context, b *StubBase, name string, args *ElementBuilder, c Codec[T]) (T, error) {
	var zero T
	a, err := args.Result()
	if err != nil {
		return zero, err
	}
	e, err := b.RRFunctionCall(ctx, name, a)
	if err != nil {
		return zero, err
	}
	return UnpackReturn(e, c)
}

// CallFunctionVoid calls a function without a return value.
func CallFunctionVoid(ctx context.Context, b *StubBase, name string, args *ElementBuilder) error {
	a, err := args.Result()
	if err != nil {
		return err
	}
	_, err = b.RRFunctionCall(ctx, name, a)
	return err
}
