package robotraconteur

import (
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

// FindServiceByType finds services implementing serviceType (fully qualified
// object type) using discovery on the given transport schemes.
func (n *Node) FindServiceByType(serviceType string, schemes ...string) ([]ServiceInfo2, error) {
	return call(func() []ServiceInfo2 {
		sv := stringsToNative(schemesOrDefault(schemes))
		defer swig.DeleteVectorstring(sv)
		r := swig.WrappedFindServiceByType(n.native, serviceType, sv)
		if r == nil || r.Swigcptr() == 0 {
			return nil
		}
		defer swig.DeleteVectorserviceinfo2wrapped(r)
		o := make([]ServiceInfo2, r.Size())
		for i := range o {
			o[i] = serviceInfo2FromNative(r.Get(i))
		}
		return o
	})
}

func nodeInfos(r swig.Vectornodeinfo2) []NodeInfo2 {
	if r == nil || r.Swigcptr() == 0 {
		return nil
	}
	defer swig.DeleteVectornodeinfo2(r)
	o := make([]NodeInfo2, r.Size())
	for i := range o {
		o[i] = nodeInfo2FromNative(r.Get(i))
	}
	return o
}

// FindNodeByID finds nodes by id using discovery.
func (n *Node) FindNodeByID(id NodeID, schemes ...string) ([]NodeInfo2, error) {
	return call(func() []NodeInfo2 {
		sv := stringsToNative(schemesOrDefault(schemes))
		defer swig.DeleteVectorstring(sv)
		nid := id.toNative()
		defer swig.DeleteNodeID(nid)
		return nodeInfos(swig.WrappedFindNodeByID(n.native, nid, sv))
	})
}

// FindNodeByName finds nodes by name using discovery.
func (n *Node) FindNodeByName(name string, schemes ...string) ([]NodeInfo2, error) {
	return call(func() []NodeInfo2 {
		sv := stringsToNative(schemesOrDefault(schemes))
		defer swig.DeleteVectorstring(sv)
		return nodeInfos(swig.WrappedFindNodeByName(n.native, name, sv))
	})
}

// UpdateDetectedNodes refreshes the detected node cache.
func (n *Node) UpdateDetectedNodes(schemes ...string) error {
	return callVoid(func() {
		sv := stringsToNative(schemesOrDefault(schemes))
		defer swig.DeleteVectorstring(sv)
		swig.WrappedUpdateDetectedNodes(n.native, sv)
	})
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

func (a *asyncServiceInfo2) Handler(r swig.Vectorserviceinfo2wrapped) {
	defer recoverDirector()
	o := make([]ServiceInfo2, r.Size())
	for i := range o {
		o[i] = serviceInfo2FromNative(r.Get(i))
	}
	go a.handler(o, nil)
}

// AsyncFindServiceByType finds services asynchronously.
func (n *Node) AsyncFindServiceByType(serviceType string, timeout time.Duration, handler func([]ServiceInfo2, error), schemes ...string) {
	impl := &asyncServiceInfo2{handler: handler}
	d := swig.NewDirectorAsyncServiceInfo2VectorReturnDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorAsyncServiceInfo2VectorReturnDirector(d) })
	err := callVoid(func() {
		sv := stringsToNative(schemesOrDefault(schemes))
		defer swig.DeleteVectorstring(sv)
		swig.AsyncWrappedFindServiceByType(n.native, serviceType, sv, timeoutMs(timeout), d, id)
	})
	if err != nil {
		heapRemove(id)
		go handler(nil, err)
	}
}

var _ swig.AsyncServiceInfo2VectorReturnDirectorMethods = (*asyncServiceInfo2)(nil)
