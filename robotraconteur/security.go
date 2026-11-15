package robotraconteur

import (
	"errors"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// UserAuthenticator authenticates service users.
type UserAuthenticator interface {
	nativeAuthenticator() (swig.NativeUserAuthenticator, error)
}

// PasswordFileUserAuthenticator authenticates users against password file
// contents ("username md5passwordhash privilege1,privilege2" per line; see
// RobotRaconteurGen --md5passwordhash).
type PasswordFileUserAuthenticator struct {
	Data                  string
	RequireVerifiedClient bool
}

// NewPasswordFileUserAuthenticator returns an authenticator for password file contents.
func NewPasswordFileUserAuthenticator(data string) *PasswordFileUserAuthenticator {
	return &PasswordFileUserAuthenticator{Data: data}
}

func (a *PasswordFileUserAuthenticator) nativeAuthenticator() (swig.NativeUserAuthenticator, error) {
	return call(func() swig.NativeUserAuthenticator {
		p := swig.NewPasswordFileUserAuthenticator__SWIG_0(a.Data, a.RequireVerifiedClient)
		if p == nil || p.Swigcptr() == 0 {
			return nil
		}
		defer swig.DeletePasswordFileUserAuthenticator(p)
		return p.SwigGetNativeUserAuthenticator()
	})
}

// ServiceSecurityPolicy configures authentication for a service. Policies
// typically contains {"requirevaliduser": "true", "allowobjectlock": "true"}.
type ServiceSecurityPolicy struct {
	Authenticator UserAuthenticator
	Policies      map[string]string
}

func (p *ServiceSecurityPolicy) toNative() (swig.ServiceSecurityPolicy, error) {
	if p.Authenticator == nil {
		return nil, NewInvalidArgument("security policy requires an authenticator")
	}
	a, err := p.Authenticator.nativeAuthenticator()
	if err != nil {
		return nil, err
	}
	defer swig.DeleteNativeUserAuthenticator(a)
	return call(func() swig.ServiceSecurityPolicy {
		m := swig.NewMap_strstr__SWIG_0()
		defer swig.DeleteMap_strstr(m)
		for k, v := range p.Policies {
			m.Set(k, v)
		}
		return swig.NewServiceSecurityPolicy(a, m)
	})
}

// AuthenticatedUser is a user accepted by a UserAuthenticator.
type AuthenticatedUser struct {
	Username string
	// Privileges, e.g. "objectlock" or "objectlockoverride".
	Privileges []string
	Properties []string
	// LoginTime and LastAccessTime are set by the node (CurrentAuthenticatedUser).
	LoginTime      time.Time
	LastAccessTime time.Time
}

// UserAuthenticatorFunc authenticates users with Go code. Return the
// accepted user, or an error to reject the login (the client receives an
// AuthenticationError unless err is an *Error with another code). It is
// called on Robot Raconteur threads.
type UserAuthenticatorFunc func(username string, credentials map[string]any, ctx *ServerContext) (*AuthenticatedUser, error)

type userAuthenticatorDirector struct {
	f UserAuthenticatorFunc
}

func (d *userAuthenticatorDirector) AuthenticateUser(username string, credentials swig.MessageElement, context swig.ServerContext) (ret swig.AuthenticatedUser) {
	defer recoverDirector()
	// Handles passed to directors are owned by Go.
	if credentials != nil && credentials.Swigcptr() != 0 {
		defer swig.DeleteMessageElement(credentials)
	}
	var ctx *ServerContext
	if context != nil && context.Swigcptr() != 0 {
		ctx = &ServerContext{node: DefaultNode(), native: context}
	}
	fail := func(err error) swig.AuthenticatedUser {
		var rerr *Error
		if !errors.As(err, &rerr) {
			err = NewAuthenticationError(err.Error())
		}
		setDirectorError(err)
		return swig.SwigcptrAuthenticatedUser(0)
	}
	var cred map[string]any
	if credentials != nil && credentials.Swigcptr() != 0 {
		e, err := fromNative(credentials)
		if err == nil {
			cred, err = UnpackMapString(e, UnpackVarValue)
		}
		if err != nil {
			return fail(err)
		}
	}
	u, err := d.f(username, cred, ctx)
	if err == nil && u == nil {
		err = NewAuthenticationError("authentication failed")
	}
	if err != nil {
		return fail(err)
	}
	priv := swig.NewVectorstring__SWIG_0()
	defer swig.DeleteVectorstring(priv)
	for _, p := range u.Privileges {
		priv.Add(p)
	}
	props := swig.NewVectorstring__SWIG_0()
	defer swig.DeleteVectorstring(props)
	for _, p := range u.Properties {
		props.Add(p)
	}
	name := u.Username
	if name == "" {
		name = username
	}
	// The returned handle is transferred to C++ (director return).
	return swig.NewAuthenticatedUser(name, priv, props, context)
}

var _ swig.WrappedUserAuthenticatorDirectorMethods = (*userAuthenticatorDirector)(nil)

func (f UserAuthenticatorFunc) nativeAuthenticator() (swig.NativeUserAuthenticator, error) {
	if f == nil {
		return nil, NewInvalidArgument("nil authenticator")
	}
	return call(func() swig.NativeUserAuthenticator {
		impl := &userAuthenticatorDirector{f: f}
		d := swig.NewDirectorWrappedUserAuthenticatorDirector(impl)
		id := heapAdd(impl, func() { swig.DeleteDirectorWrappedUserAuthenticatorDirector(d) })
		w := swig.NewWrappedUserAuthenticator()
		defer swig.DeleteWrappedUserAuthenticator(w)
		w.SetRRDirector(d, int(id))
		return w.SwigGetNativeUserAuthenticator()
	})
}

func vectorStrings(v swig.Vectorstring) []string {
	defer swig.DeleteVectorstring(v)
	n := int(v.Size())
	o := make([]string, n)
	for i := 0; i < n; i++ {
		o[i] = v.Get(i)
	}
	return o
}
