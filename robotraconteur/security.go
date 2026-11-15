package robotraconteur

import swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"

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
