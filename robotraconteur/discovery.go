package robotraconteur

import (
	"context"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// ServiceInfo2 describes a service found by discovery.
type ServiceInfo2 struct {
	Name                 string
	RootObjectType       string
	RootObjectImplements []string
	ConnectionURL        []string
	Attributes           map[string]any
	NodeID               NodeID
	NodeName             string
}

// NodeInfo2 describes a node found by discovery.
type NodeInfo2 struct {
	NodeID        NodeID
	NodeName      string
	ConnectionURL []string
}

func stringsFromNative(v swig.Vectorstring) []string {
	if v == nil || v.Swigcptr() == 0 {
		return nil
	}
	o := make([]string, v.Size())
	for i := range o {
		o[i] = v.Get(i)
	}
	return o
}

func stringsToNative(s []string) swig.Vectorstring {
	v := swig.NewVectorstring()
	for _, x := range s {
		v.Add(x)
	}
	return v
}

func serviceInfo2FromNative(w swig.ServiceInfo2Wrapped) ServiceInfo2 {
	o := ServiceInfo2{
		Name: w.GetName(), RootObjectType: w.GetRootObjectType(), NodeName: w.GetNodeName(),
		RootObjectImplements: stringsFromNative(w.GetRootObjectImplements()),
		ConnectionURL:        stringsFromNative(w.GetConnectionURL()),
	}
	id := w.GetNodeID()
	o.NodeID = nodeIDFromNative(id)
	if a := w.GetAttributes(); a != nil && a.Swigcptr() != 0 {
		if e, err := fromNative(a); err == nil {
			o.Attributes, _ = UnpackMapString(e, UnpackVarValue)
		}
		swig.DeleteMessageElement(a)
	}
	return o
}

func nodeInfo2FromNative(w swig.WrappedNodeInfo2) NodeInfo2 {
	return NodeInfo2{NodeID: nodeIDFromNative(w.GetNodeID()), NodeName: w.GetNodeName(),
		ConnectionURL: stringsFromNative(w.GetConnectionURL())}
}

// DefaultTransportSchemes are the transport schemes searched by discovery
// when none are given.
var DefaultTransportSchemes = []string{"rr+local", "rr+tcp", "rrs+tcp", "rr+intra"}

func schemesOrDefault(s []string) []string {
	if len(s) == 0 {
		return DefaultTransportSchemes
	}
	return s
}

// discoveryTimeout bounds discovery operations without a ctx deadline.
const discoveryTimeout = 5 * time.Second

// FindServiceByType finds services implementing serviceType (fully qualified
// object type) using discovery on the given transport schemes. It waits up
// to the ctx deadline, or 5 seconds, for candidates to respond.
func (n *Node) FindServiceByType(ctx context.Context, serviceType string, schemes ...string) ([]ServiceInfo2, error) {
	return await(ctx, func(done func([]ServiceInfo2, error)) error {
		impl := &asyncServiceInfo2{handler: done}
		d := swig.NewDirectorAsyncServiceInfo2VectorReturnDirector(impl)
		id := heapAdd(impl, func() { swig.DeleteDirectorAsyncServiceInfo2VectorReturnDirector(d) })
		t := ctxTimeoutMs(ctx, discoveryTimeout)
		err := startNative(func() error {
			sv := stringsToNative(schemesOrDefault(schemes))
			defer swig.DeleteVectorstring(sv)
			swig.AsyncWrappedFindServiceByType(n.native, serviceType, sv, t, d, id)
			return nil
		})
		if err != nil {
			heapRemove(id)
		}
		return err
	}, nil)
}

func (n *Node) awaitNodeInfos(ctx context.Context, start func(t int32, d swig.AsyncNodeInfo2VectorReturnDirector, id int32)) ([]NodeInfo2, error) {
	return await(ctx, func(done func([]NodeInfo2, error)) error {
		impl := &asyncNodeInfo2{handler: done}
		d := swig.NewDirectorAsyncNodeInfo2VectorReturnDirector(impl)
		id := heapAdd(impl, func() { swig.DeleteDirectorAsyncNodeInfo2VectorReturnDirector(d) })
		t := ctxTimeoutMs(ctx, discoveryTimeout)
		err := startNative(func() error { start(t, d, id); return nil })
		if err != nil {
			heapRemove(id)
		}
		return err
	}, nil)
}

// FindNodeByID finds nodes by id using discovery.
func (n *Node) FindNodeByID(ctx context.Context, id NodeID, schemes ...string) ([]NodeInfo2, error) {
	return n.awaitNodeInfos(ctx, func(t int32, d swig.AsyncNodeInfo2VectorReturnDirector, hid int32) {
		sv := stringsToNative(schemesOrDefault(schemes))
		defer swig.DeleteVectorstring(sv)
		nid := id.toNative()
		defer swig.DeleteNodeID(nid)
		swig.AsyncWrappedFindNodeByID(n.native, nid, sv, t, d, hid)
	})
}

// FindNodeByName finds nodes by name using discovery.
func (n *Node) FindNodeByName(ctx context.Context, name string, schemes ...string) ([]NodeInfo2, error) {
	return n.awaitNodeInfos(ctx, func(t int32, d swig.AsyncNodeInfo2VectorReturnDirector, hid int32) {
		sv := stringsToNative(schemesOrDefault(schemes))
		defer swig.DeleteVectorstring(sv)
		swig.AsyncWrappedFindNodeByName(n.native, name, sv, t, d, hid)
	})
}

// UpdateDetectedNodes refreshes the detected node cache.
func (n *Node) UpdateDetectedNodes(ctx context.Context, schemes ...string) error {
	_, err := await(ctx, func(done func(struct{}, error)) error {
		d, id := newAsyncVoidNoErr(func() { done(struct{}{}, nil) })
		t := ctxTimeoutMs(ctx, discoveryTimeout)
		err := startNative(func() error {
			sv := stringsToNative(schemesOrDefault(schemes))
			defer swig.DeleteVectorstring(sv)
			swig.AsyncWrappedUpdateDetectedNodes(n.native, sv, t, d, id)
			return nil
		})
		if err != nil {
			heapRemove(id)
		}
		return err
	}, nil)
	return err
}

// DetectedNodes returns the ids of detected nodes.
func (n *Node) DetectedNodes() ([]NodeID, error) {
	return call(func() []NodeID {
		v := swig.WrappedGetDetectedNodes(n.native)
		defer swig.DeleteVectorstring(v)
		var o []NodeID
		for _, s := range stringsFromNative(v) {
			if id, err := ParseNodeID(s); err == nil {
				o = append(o, id)
			}
		}
		return o
	})
}

type asyncServiceInfo2 struct{ handler func([]ServiceInfo2, error) }

// Handler receives a vector owned by C++ (valid during the call only).
func (a *asyncServiceInfo2) Handler(r swig.Vectorserviceinfo2wrapped) {
	defer recoverDirector()
	o := make([]ServiceInfo2, r.Size())
	for i := range o {
		o[i] = serviceInfo2FromNative(r.Get(i))
	}
	a.handler(o, nil)
}

var _ swig.AsyncServiceInfo2VectorReturnDirectorMethods = (*asyncServiceInfo2)(nil)

type asyncNodeInfo2 struct{ handler func([]NodeInfo2, error) }

// Handler receives a vector owned by C++ (valid during the call only).
func (a *asyncNodeInfo2) Handler(r swig.Vectornodeinfo2) {
	defer recoverDirector()
	o := make([]NodeInfo2, r.Size())
	for i := range o {
		o[i] = nodeInfo2FromNative(r.Get(i))
	}
	a.handler(o, nil)
}

var _ swig.AsyncNodeInfo2VectorReturnDirectorMethods = (*asyncNodeInfo2)(nil)
