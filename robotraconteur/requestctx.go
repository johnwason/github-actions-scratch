package robotraconteur

import (
	"context"
	"strings"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// Service member implementations receive a context.Context carrying the
// request: the client endpoint, service path, server context and
// authenticated user. They run on their own goroutine (the core's request
// thread is released at once), so the core's thread-local "current request"
// state is captured into the context when the request arrives.

type requestKey struct{}

type requestInfo struct {
	node        *Node
	endpoint    uint32
	servicePath string
	user        swig.AuthenticatedUser
	adapter     *asyncAdapter
}

// captureRequest reads the current request state. It must run on the core
// thread that delivered the request.
func captureRequest(n *Node, a *asyncAdapter) *requestInfo {
	r := &requestInfo{node: n, adapter: a}
	_ = callVoid(func() {
		r.endpoint = swig.ServerEndpointGetCurrentEndpoint()
		if swig.Rrgo_error_pending() {
			return
		}
		r.servicePath = swig.ServerContextGetCurrentServicePath()
		swig.Rrgo_clear_error()
		if u := swig.ServerEndpointGetCurrentAuthenticatedUser(); u != nil && u.Swigcptr() != 0 {
			r.user = u
		}
		swig.Rrgo_clear_error()
	})
	return r
}

// release frees the captured native handles once the request is complete.
func (r *requestInfo) release() {
	if r.user != nil {
		swig.DeleteAuthenticatedUser(r.user)
		r.user = nil
	}
}

func (r *requestInfo) context() context.Context {
	return context.WithValue(context.Background(), requestKey{}, r)
}

func requestFrom(ctx context.Context) (*requestInfo, error) {
	if ctx != nil {
		if r, ok := ctx.Value(requestKey{}).(*requestInfo); ok && r != nil {
			return r, nil
		}
	}
	return nil, NewInvalidOperation("not called for a service request (use the ctx passed to the service member)")
}

// CurrentEndpoint returns the client endpoint of the request ctx belongs to.
func CurrentEndpoint(ctx context.Context) (uint32, error) {
	r, err := requestFrom(ctx)
	if err != nil {
		return 0, err
	}
	return r.endpoint, nil
}

// CurrentServicePath returns the service path of the object the request ctx
// belongs to was made on.
func CurrentServicePath(ctx context.Context) (string, error) {
	r, err := requestFrom(ctx)
	if err != nil {
		return "", err
	}
	return r.servicePath, nil
}

// CurrentServerContext returns the service (as returned by RegisterService)
// of the request ctx belongs to.
func CurrentServerContext(ctx context.Context) (*ServerContext, error) {
	r, err := requestFrom(ctx)
	if err != nil {
		return nil, err
	}
	name, _, _ := strings.Cut(r.servicePath, ".")
	if c := r.node.registeredService(name); c != nil {
		return c, nil
	}
	return nil, NewInvalidOperation("no current server context")
}

// CurrentAuthenticatedUser returns the user authenticated on the client
// connection of the request ctx belongs to.
func CurrentAuthenticatedUser(ctx context.Context) (*AuthenticatedUser, error) {
	r, err := requestFrom(ctx)
	if err != nil {
		return nil, err
	}
	if r.user == nil {
		return nil, NewAuthenticationError("no authenticated user")
	}
	u := r.user
	return &AuthenticatedUser{
		Username:       u.GetUsername(),
		Privileges:     vectorStrings(u.GetPrivileges()),
		Properties:     vectorStrings(u.GetProperties()),
		LoginTime:      timeFromMicros(u.GetLoginTime()),
		LastAccessTime: timeFromMicros(u.GetLastAccessTime()),
	}, nil
}
