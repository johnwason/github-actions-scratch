package loopback

import (
	"context"
	"errors"
	"fmt"
	"slices"
	"testing"

	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
	ts1 "github.com/robotraconteur/robotraconteur_go/test/gen/com__robotraconteur__testing__testservice1"
)

// authroot reports the authenticated user from inside a service function.
type authroot struct {
	ts1.TestrootDefaultImpl
}

func (a *authroot) Func3(ctx context.Context, d1, d2 float64) (float64, error) {
	u, err := rr.CurrentAuthenticatedUser(ctx)
	if err != nil {
		return 0, err
	}
	if u.Username != "alice" || !slices.Contains(u.Privileges, "objectlock") || u.LoginTime.IsZero() {
		return 0, rr.NewInvalidOperation(fmt.Sprintf("unexpected user %+v", u))
	}
	return d1 + d2, nil
}

func TestGoUserAuthenticator(t *testing.T) {
	ctx := t.Context()
	auth := rr.UserAuthenticatorFunc(func(username string, cred map[string]any, ctx *rr.ServerContext) (*rr.AuthenticatedUser, error) {
		if ctx == nil {
			return nil, errors.New("no context")
		}
		if username == "alice" && cred["password"] == "secret" {
			return &rr.AuthenticatedUser{Privileges: []string{"objectlock"}}, nil
		}
		if username == "panic" {
			panic("authenticator panic")
		}
		return nil, errors.New("bad password")
	})
	policy := &rr.ServiceSecurityPolicy{Authenticator: auth, Policies: map[string]string{"requirevaliduser": "true"}}
	if _, err := node.RegisterService("go_auth", "com.robotraconteur.testing.TestService1.testroot", &authroot{},
		rr.WithSecurityPolicy(policy)); err != nil {
		t.Fatal(err)
	}
	defer node.CloseService("go_auth")
	url := fmt.Sprintf("rr+tcp://localhost:%d?service=go_auth", port)

	for _, c := range []struct{ user, pass string }{{"alice", "wrong"}, {"bob", "secret"}, {"panic", ""}} {
		_, err := node.ConnectService(ctx, url, rr.WithCredentials(c.user, map[string]any{"password": c.pass}))
		if !errors.Is(err, rr.ErrAuthenticationError) {
			t.Errorf("%s: expected AuthenticationError, got %v", c.user, err)
		}
	}
	if _, err := node.ConnectService(ctx, url); err == nil {
		t.Error("expected error connecting without credentials")
	}

	o, err := node.ConnectService(ctx, url, rr.WithCredentials("alice", map[string]any{"password": "secret"}))
	if err != nil {
		t.Fatal(err)
	}
	defer node.DisconnectService(o)
	v, err := o.(ts1.Testroot).Func3(ctx, 1, 2)
	if err != nil || v != 3 {
		t.Fatalf("Func3 %v %v", v, err)
	}
}
