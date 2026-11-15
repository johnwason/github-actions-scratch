package robotraconteur

import swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"

// AsyncAdapter lets a generated skeleton complete a service call
// asynchronously (for service objects implementing the Async<Object>
// interface).
type AsyncAdapter struct {
	native swig.WrappedServiceSkelAsyncAdapter
	async  bool
}

// MakeAsync marks the current call as asynchronous; End must be called.
func (a *AsyncAdapter) MakeAsync() {
	if a == nil || a.native == nil {
		return
	}
	a.native.MakeAsync()
	a.async = true
}

// IsAsync reports whether MakeAsync was called.
func (a *AsyncAdapter) IsAsync() bool { return a != nil && a.async }

// End completes an asynchronous call with a return element (nil for void)
// or an error.
func (a *AsyncAdapter) End(ret *Element, err error) {
	if a == nil || a.native == nil {
		return
	}
	n := swig.SwigcptrWrappedServiceSkelAsyncAdapter(a.native.Swigcptr())
	callVoid(func() {
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

func newAsyncAdapter(n swig.WrappedServiceSkelAsyncAdapter) *AsyncAdapter {
	if n == nil || n.Swigcptr() == 0 {
		return nil
	}
	return &AsyncAdapter{native: n}
}

// release deletes the native adapter unless the call was made asynchronous.
func (a *AsyncAdapter) release() {
	if a != nil && !a.async && a.native != nil {
		swig.DeleteWrappedServiceSkelAsyncAdapter(a.native)
		a.native = nil
	}
}
