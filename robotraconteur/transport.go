package robotraconteur

import (
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// TCPTransport is the rr+tcp/rrs+tcp transport of a node.
type TCPTransport struct{ native swig.TcpTransport }

// LocalTransport is the rr+local transport (same machine).
type LocalTransport struct{ native swig.LocalTransport }

// IntraTransport is the rr+intra transport (same process).
type IntraTransport struct{ native swig.IntraTransport }

// TCPTransport returns the TCP transport of the setup, or nil.
func (s *NodeSetup) TCPTransport() *TCPTransport {
	t, err := call(func() swig.TcpTransport {
		return swig.SwigcptrWrappedRobotRaconteurNodeSetup(s.native.Swigcptr()).GetTcpTransport()
	})
	if err != nil || t == nil || t.Swigcptr() == 0 {
		return nil
	}
	tt := &TCPTransport{native: t}
	addCleanup(tt, t, swig.DeleteTcpTransport)
	return tt
}

// LocalTransport returns the local transport of the setup, or nil.
func (s *NodeSetup) LocalTransport() *LocalTransport {
	t, err := call(func() swig.LocalTransport {
		return swig.SwigcptrWrappedRobotRaconteurNodeSetup(s.native.Swigcptr()).GetLocalTransport()
	})
	if err != nil || t == nil || t.Swigcptr() == 0 {
		return nil
	}
	lt := &LocalTransport{native: t}
	addCleanup(lt, t, swig.DeleteLocalTransport)
	return lt
}

// IntraTransport returns the intra transport of the setup, or nil.
func (s *NodeSetup) IntraTransport() *IntraTransport {
	t, err := call(func() swig.IntraTransport {
		return swig.SwigcptrWrappedRobotRaconteurNodeSetup(s.native.Swigcptr()).GetIntraTransport()
	})
	if err != nil || t == nil || t.Swigcptr() == 0 {
		return nil
	}
	it := &IntraTransport{native: t}
	addCleanup(it, t, swig.DeleteIntraTransport)
	return it
}

// ListenPort returns the TCP listen port.
func (t *TCPTransport) ListenPort() (int, error) {
	p, err := call(func() int32 { return t.native.GetListenPort() })
	return int(p), err
}

// ServerListenURLs returns the URLs the transport listens on.
func (t *TCPTransport) ServerListenURLs() ([]string, error) {
	return call(func() []string {
		v := t.native.GetServerListenUrls()
		defer swig.DeleteVectorstring(v)
		return stringsFromNative(v)
	})
}

// DefaultConnectTimeout returns the connect timeout.
func (t *TCPTransport) DefaultConnectTimeout() time.Duration {
	return time.Duration(t.native.GetDefaultConnectTimeout()) * time.Millisecond
}

// SetDefaultConnectTimeout sets the connect timeout.
func (t *TCPTransport) SetDefaultConnectTimeout(d time.Duration) {
	t.native.SetDefaultConnectTimeout(int32(d.Milliseconds()))
}

// EnableNodeDiscoveryListening listens for node announcements (UDP).
func (t *TCPTransport) EnableNodeDiscoveryListening() error {
	return callVoid(func() { swig.SwigcptrTcpTransport(t.native.Swigcptr()).EnableNodeDiscoveryListening__SWIG_1() })
}

// DisableNodeDiscoveryListening stops listening for announcements.
func (t *TCPTransport) DisableNodeDiscoveryListening() error {
	return callVoid(func() { t.native.DisableNodeDiscoveryListening() })
}

// EnableNodeAnnounce announces the node (UDP).
func (t *TCPTransport) EnableNodeAnnounce() error {
	return callVoid(func() { swig.SwigcptrTcpTransport(t.native.Swigcptr()).EnableNodeAnnounce__SWIG_1() })
}

// DisableNodeAnnounce stops announcing the node.
func (t *TCPTransport) DisableNodeAnnounce() error {
	return callVoid(func() { t.native.DisableNodeAnnounce() })
}

// MaxMessageSize returns the maximum message size.
func (t *TCPTransport) MaxMessageSize() int { return int(t.native.GetMaxMessageSize()) }

// SetMaxMessageSize sets the maximum message size.
func (t *TCPTransport) SetMaxMessageSize(n int) error {
	return callVoid(func() { t.native.SetMaxMessageSize(int32(n)) })
}

// RequireTLS reports whether TLS is required.
func (t *TCPTransport) RequireTLS() bool { return t.native.GetRequireTls() }

// SetRequireTLS sets whether TLS is required.
func (t *TCPTransport) SetRequireTLS(v bool) { t.native.SetRequireTls(v) }

// IsTransportConnectionSecure reports whether the connection of a client
// endpoint (service side) uses TLS.
func (t *TCPTransport) IsTransportConnectionSecure(endpoint uint32) (bool, error) {
	return call(func() bool {
		return swig.SwigcptrTcpTransport(t.native.Swigcptr()).IsTransportConnectionSecure__SWIG_0(endpoint)
	})
}

// Close closes the transport.
func (t *TCPTransport) Close() error { return callVoid(func() { t.native.Close() }) }

// EnableNodeDiscoveryListening detects nodes on this machine.
func (t *LocalTransport) EnableNodeDiscoveryListening() error {
	return callVoid(func() { t.native.EnableNodeDiscoveryListening() })
}

// DisableNodeDiscoveryListening stops detecting local nodes.
func (t *LocalTransport) DisableNodeDiscoveryListening() error {
	return callVoid(func() { t.native.DisableNodeDiscoveryListening() })
}

// StartServerAsNodeName starts the local transport server with a node name.
func (t *LocalTransport) StartServerAsNodeName(name string, public bool) error {
	return callVoid(func() { swig.SwigcptrLocalTransport(t.native.Swigcptr()).StartServerAsNodeName__SWIG_0(name, public) })
}

// StartClientAsNodeName starts the local transport client with a node name.
func (t *LocalTransport) StartClientAsNodeName(name string) error {
	return callVoid(func() { t.native.StartClientAsNodeName(name) })
}

// ServerListenURLs returns the URLs the transport listens on.
func (t *LocalTransport) ServerListenURLs() ([]string, error) {
	return call(func() []string {
		v := t.native.GetServerListenUrls()
		defer swig.DeleteVectorstring(v)
		return stringsFromNative(v)
	})
}

// Close closes the transport.
func (t *LocalTransport) Close() error { return callVoid(func() { t.native.Close() }) }

// StartServer starts the intra transport server.
func (t *IntraTransport) StartServer() error { return callVoid(func() { t.native.StartServer() }) }

// StartClient starts the intra transport client.
func (t *IntraTransport) StartClient() error { return callVoid(func() { t.native.StartClient() }) }

// Close closes the transport.
func (t *IntraTransport) Close() error { return callVoid(func() { t.native.Close() }) }
