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

// HardwareTransport is the transport for USB, Bluetooth and PCIe devices
// (rr+usb, rr+bluetooth, rr+pci).
type HardwareTransport struct{ native swig.HardwareTransport }

// HardwareTransport returns the hardware transport of the setup, or nil
// (enabled with NodeSetupEnableHardwareTransport).
func (s *NodeSetup) HardwareTransport() *HardwareTransport {
	t, err := call(func() swig.HardwareTransport {
		return swig.SwigcptrWrappedRobotRaconteurNodeSetup(s.native.Swigcptr()).GetHardwareTransport()
	})
	if err != nil || t == nil || t.Swigcptr() == 0 {
		return nil
	}
	return newHardwareTransport(t)
}

func newHardwareTransport(t swig.HardwareTransport) *HardwareTransport {
	ht := &HardwareTransport{native: t}
	addCleanup(ht, t, swig.DeleteHardwareTransport)
	return ht
}

// ServerListenURLs returns the URLs the transport listens on.
func (t *HardwareTransport) ServerListenURLs() ([]string, error) {
	return call(func() []string {
		v := t.native.GetServerListenUrls()
		defer swig.DeleteVectorstring(v)
		return stringsFromNative(v)
	})
}

// MaxMessageSize returns the maximum message size in bytes.
func (t *HardwareTransport) MaxMessageSize() int { return int(t.native.GetMaxMessageSize()) }

// SetMaxMessageSize sets the maximum message size in bytes.
func (t *HardwareTransport) SetMaxMessageSize(n int) error {
	return callVoid(func() { t.native.SetMaxMessageSize(int32(n)) })
}

// DisableMessage4 reports whether message version 4 is disabled.
func (t *HardwareTransport) DisableMessage4() bool { return t.native.GetDisableMessage4() }

// SetDisableMessage4 disables message version 4.
func (t *HardwareTransport) SetDisableMessage4(d bool) { t.native.SetDisableMessage4(d) }

// DisableStringTable reports whether the string table is disabled.
func (t *HardwareTransport) DisableStringTable() bool { return t.native.GetDisableStringTable() }

// SetDisableStringTable disables the string table.
func (t *HardwareTransport) SetDisableStringTable(d bool) { t.native.SetDisableStringTable(d) }

// DisableAsyncMessageIO reports whether asynchronous message IO is disabled.
func (t *HardwareTransport) DisableAsyncMessageIO() bool { return t.native.GetDisableAsyncMessageIO() }

// SetDisableAsyncMessageIO disables asynchronous message IO.
func (t *HardwareTransport) SetDisableAsyncMessageIO(d bool) { t.native.SetDisableAsyncMessageIO(d) }

// Close closes the transport.
func (t *HardwareTransport) Close() error { return callVoid(func() { t.native.Close() }) }

// ---- transports created without NodeSetup ----

// Transport is a transport that can be registered with a node.
type Transport interface {
	nativeTransport() swig.Transport
}

func (t *TCPTransport) nativeTransport() swig.Transport {
	return swig.SwigcptrTcpTransport(t.native.Swigcptr()).SwigGetTransport()
}
func (t *LocalTransport) nativeTransport() swig.Transport {
	return swig.SwigcptrLocalTransport(t.native.Swigcptr()).SwigGetTransport()
}
func (t *IntraTransport) nativeTransport() swig.Transport {
	return swig.SwigcptrIntraTransport(t.native.Swigcptr()).SwigGetTransport()
}
func (t *HardwareTransport) nativeTransport() swig.Transport {
	return swig.SwigcptrHardwareTransport(t.native.Swigcptr()).SwigGetTransport()
}

// RegisterTransport registers a transport created with NewTCPTransport,
// NewLocalTransport, NewIntraTransport or NewHardwareTransport (NodeSetup
// registers its transports itself).
func (n *Node) RegisterTransport(t Transport) error {
	if t == nil {
		return NewInvalidArgument("nil transport")
	}
	return callVoid(func() {
		nt := t.nativeTransport()
		defer swig.DeleteTransport(nt)
		n.native.RegisterTransport(nt)
	})
}

// NewTCPTransport creates a TCP transport for n. Configure it, start a
// server if needed (StartServer) and register it with RegisterTransport.
func NewTCPTransport(n *Node) (*TCPTransport, error) {
	t, err := call(func() swig.TcpTransport { return swig.NewTcpTransport__SWIG_1(n.native) })
	if err != nil {
		return nil, err
	}
	tt := &TCPTransport{native: t}
	addCleanup(tt, t, swig.DeleteTcpTransport)
	return tt, nil
}

// StartServer listens for connections on port (0 for any free port).
// localhostOnly restricts it to the loopback interface.
func (t *TCPTransport) StartServer(port int, localhostOnly bool) error {
	return callVoid(func() { swig.SwigcptrTcpTransport(t.native.Swigcptr()).StartServer__SWIG_0(int32(port), localhostOnly) })
}

// NewLocalTransport creates a local transport for n.
func NewLocalTransport(n *Node) (*LocalTransport, error) {
	t, err := call(func() swig.LocalTransport { return swig.NewLocalTransport__SWIG_1(n.native) })
	if err != nil {
		return nil, err
	}
	lt := &LocalTransport{native: t}
	addCleanup(lt, t, swig.DeleteLocalTransport)
	return lt, nil
}

// NewIntraTransport creates an intra-process transport for n.
func NewIntraTransport(n *Node) (*IntraTransport, error) {
	t, err := call(func() swig.IntraTransport { return swig.NewIntraTransport__SWIG_1(n.native) })
	if err != nil {
		return nil, err
	}
	it := &IntraTransport{native: t}
	addCleanup(it, t, swig.DeleteIntraTransport)
	return it, nil
}

// NewHardwareTransport creates a hardware (USB, Bluetooth, PCIe) transport for n.
func NewHardwareTransport(n *Node) (*HardwareTransport, error) {
	t, err := call(func() swig.HardwareTransport { return swig.NewHardwareTransport__SWIG_1(n.native) })
	if err != nil {
		return nil, err
	}
	return newHardwareTransport(t), nil
}
