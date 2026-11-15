package robotraconteur

import (
	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// NodeSetupFlags configure a node setup (RobotRaconteurNodeSetupFlags).
type NodeSetupFlags uint32

// NodeSetup initializes a node with transports and service types. Close it
// to shut down the node:
//
//	setup, err := robotraconteur.ClientNodeSetup()
//	if err != nil { ... }
//	defer setup.Close()
type NodeSetup struct {
	Node   *Node
	native swig.WrappedRobotRaconteurNodeSetup
}

// NodeSetupOption configures a node setup.
type NodeSetupOption func(*nodeSetupOptions)

type nodeSetupOptions struct {
	nodeName  string
	tcpPort   uint16
	flags     *NodeSetupFlags
	factories []ServiceFactory
	node      *Node
}

// WithNodeName sets the node name.
func WithNodeName(name string) NodeSetupOption {
	return func(o *nodeSetupOptions) { o.nodeName = name }
}

// WithTCPPort sets the TCP listen port (servers; 0 = any).
func WithTCPPort(port uint16) NodeSetupOption {
	return func(o *nodeSetupOptions) { o.tcpPort = port }
}

// WithFlags overrides the setup flags.
func WithFlags(flags NodeSetupFlags) NodeSetupOption {
	return func(o *nodeSetupOptions) { o.flags = &flags }
}

// WithServiceTypes registers only the given service factories (default: all
// factories added with AddServiceFactory).
func WithServiceTypes(factories ...ServiceFactory) NodeSetupOption {
	return func(o *nodeSetupOptions) { o.factories = factories }
}

func newNodeSetup(defaultFlags NodeSetupFlags, opts []NodeSetupOption) (*NodeSetup, error) {
	o := nodeSetupOptions{node: DefaultNode()}
	for _, f := range opts {
		f(&o)
	}
	flags := defaultFlags
	if o.flags != nil {
		flags = *o.flags
	}
	n := o.node
	if err := n.RegisterServiceTypes(o.factories...); err != nil {
		return nil, err
	}
	ns, err := call(func() swig.WrappedRobotRaconteurNodeSetup {
		return swig.NewWrappedRobotRaconteurNodeSetup__SWIG_0(n.native, o.nodeName, o.tcpPort, uint32(flags))
	})
	if err != nil {
		return nil, err
	}
	return &NodeSetup{Node: n, native: ns}, nil
}

// ClientNodeSetup initializes the default node for use as a client.
func ClientNodeSetup(opts ...NodeSetupOption) (*NodeSetup, error) {
	return newNodeSetup(NodeSetupClientDefault, opts)
}

// ServerNodeSetup initializes the default node for use as a server with the
// given node name and TCP port.
func ServerNodeSetup(nodeName string, tcpPort uint16, opts ...NodeSetupOption) (*NodeSetup, error) {
	return newNodeSetup(NodeSetupServerDefault, append([]NodeSetupOption{WithNodeName(nodeName), WithTCPPort(tcpPort)}, opts...))
}

// SecureServerNodeSetup initializes the default node as a TLS secured server.
func SecureServerNodeSetup(nodeName string, tcpPort uint16, opts ...NodeSetupOption) (*NodeSetup, error) {
	return newNodeSetup(NodeSetupSecureServerDefault, append([]NodeSetupOption{WithNodeName(nodeName), WithTCPPort(tcpPort)}, opts...))
}

// Close shuts down the node.
func (s *NodeSetup) Close() error {
	if s == nil || s.native == nil {
		return nil
	}
	err := callVoid(func() { swig.DeleteWrappedRobotRaconteurNodeSetup(s.native) })
	s.native = nil
	return err
}
