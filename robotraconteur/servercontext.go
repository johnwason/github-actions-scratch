package robotraconteur

import swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"

// CurrentServicePath returns the service path of the object handling the
// current request (only valid inside a service member call).
func CurrentServicePath() (string, error) {
	return call(func() string { return swig.ServerContextGetCurrentServicePath() })
}

// CurrentServerContext returns the service handling the current request.
func CurrentServerContext() (*ServerContext, error) {
	c, err := call(func() swig.ServerContext { return swig.ServerContextGetCurrentServerContext() })
	if err != nil {
		return nil, err
	}
	if c == nil || c.Swigcptr() == 0 {
		return nil, NewInvalidOperation("no current server context")
	}
	return &ServerContext{node: DefaultNode(), native: c}, nil
}

func (c *ServerContext) nativeCtx() (swig.SwigcptrServerContext, error) {
	if c.native == nil || c.native.Swigcptr() == 0 {
		return 0, NewInvalidOperation("server context not available")
	}
	return swig.SwigcptrServerContext(c.native.Swigcptr()), nil
}

// ReleaseServicePath releases the objects at and below path, so the next
// client request creates new objects (e.g. after an objref target changes).
func (c *ServerContext) ReleaseServicePath(path string) error {
	n, err := c.nativeCtx()
	if err != nil {
		return err
	}
	return callVoid(func() { n.X_ReleaseServicePath__SWIG_0(path) })
}

// SetServiceAttributes sets the service attributes advertised by discovery.
func (c *ServerContext) SetServiceAttributes(attrs map[string]any) error {
	n, err := c.nativeCtx()
	if err != nil {
		return err
	}
	e, err := PackMapString("value", attrs, PackVarValue)
	if err != nil {
		return err
	}
	var cerr error
	err = callVoid(func() {
		m, err := toNative(e)
		if err != nil {
			cerr = err
			return
		}
		defer swig.DeleteMessageElement(m)
		n.SetServiceAttributes(m)
	})
	if err != nil {
		return err
	}
	return cerr
}

// CandidateConnectionURLs returns URLs clients may use to connect to the service.
func (c *ServerContext) CandidateConnectionURLs() ([]string, error) {
	n, err := c.nativeCtx()
	if err != nil {
		return nil, err
	}
	return call(func() []string {
		v := n.GetCandidateConnectionURLs()
		defer swig.DeleteVectorstring(v)
		o := make([]string, v.Size())
		for i := range o {
			o[i] = v.Get(i)
		}
		return o
	})
}

// RequestObjectLock locks the object at servicepath for username (server side lock).
func (c *ServerContext) RequestObjectLock(servicepath, username string) error {
	n, err := c.nativeCtx()
	if err != nil {
		return err
	}
	return callVoid(func() { n.RequestObjectLock(servicepath, username) })
}

// ReleaseObjectLock releases an object lock.
func (c *ServerContext) ReleaseObjectLock(servicepath, username string, override bool) error {
	n, err := c.nativeCtx()
	if err != nil {
		return err
	}
	return callVoid(func() { n.ReleaseObjectLock(servicepath, username, override) })
}
