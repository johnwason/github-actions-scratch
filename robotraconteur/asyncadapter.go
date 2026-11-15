package robotraconteur

import swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"

// asyncAdapter completes a service request from another goroutine.
type asyncAdapter struct {
	native swig.WrappedServiceSkelAsyncAdapter
}

func newAsyncAdapter(n swig.WrappedServiceSkelAsyncAdapter) *asyncAdapter {
	if n == nil || n.Swigcptr() == 0 {
		return nil
	}
	return &asyncAdapter{native: n}
}

// makeAsync tells the core the request completes later through end.
func (a *asyncAdapter) makeAsync() { a.native.MakeAsync() }

// end completes the request with a return element (nil for void) or an
// error, and releases the adapter.
func (a *asyncAdapter) end(ret *Element, err error) {
	if a == nil || a.native == nil {
		return
	}
	n := swig.SwigcptrWrappedServiceSkelAsyncAdapter(a.native.Swigcptr())
	_ = callVoid(func() {
		info := newErrorInfo(err)
		defer swig.DeleteHandlerErrorInfo(info)
		if err == nil && ret != nil {
			m, cerr := toNative(ret)
			if cerr == nil {
				defer swig.DeleteMessageElement(m)
				n.End__SWIG_1(m, info)
				return
			}
			info2 := newErrorInfo(cerr)
			defer swig.DeleteHandlerErrorInfo(info2)
			n.End__SWIG_0(info2)
			return
		}
		if err == nil {
			// void return
			m, _ := toNative(NewElement("return", []int32{0}))
			defer swig.DeleteMessageElement(m)
			n.End__SWIG_1(m, info)
			return
		}
		n.End__SWIG_0(info)
	})
	swig.DeleteWrappedServiceSkelAsyncAdapter(a.native)
	a.native = nil
}
