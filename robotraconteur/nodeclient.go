package robotraconteur

import (
	"runtime"
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// ObjectLockFlags selects the kind of object lock.
type ObjectLockFlags uint32

// Object lock kinds.
const (
	// UserLock locks the object for all connections of the authenticated user.
	UserLock ObjectLockFlags = 0
	// ClientLock locks the object for this client connection only.
	ClientLock ObjectLockFlags = 1
)

// RequestObjectLock locks obj (and its sub objects) on the service.
func (n *Node) RequestObjectLock(obj any, flags ObjectLockFlags) (string, error) {
	b, err := stubBaseOf(obj)
	if err != nil {
		return "", err
	}
	return call(func() string {
		return n.native.RequestObjectLock(b.inner, swig.RobotRaconteurRobotRaconteurObjectLockFlags(flags))
	})
}

// ReleaseObjectLock releases an object lock.
func (n *Node) ReleaseObjectLock(obj any) (string, error) {
	b, err := stubBaseOf(obj)
	if err != nil {
		return "", err
	}
	return call(func() string { return n.native.ReleaseObjectLock(b.inner) })
}

// WithMonitorLock runs f while holding the monitor lock of obj (a short
// exclusive lock across all clients of the object). The Robot Raconteur
// monitor lock belongs to the calling OS thread, so the goroutine is locked
// to its thread while f runs; f must make its calls on this goroutine.
func (n *Node) WithMonitorLock(obj any, timeout time.Duration, f func() error) error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	if err := n.MonitorEnter(obj, timeout); err != nil {
		return err
	}
	ferr := f()
	if err := n.MonitorExit(obj); err != nil && ferr == nil {
		return err
	}
	return ferr
}

// MonitorEnter acquires the monitor lock of obj. The lock belongs to the
// calling OS thread: call runtime.LockOSThread first and MonitorExit from the
// same goroutine, or use WithMonitorLock.
func (n *Node) MonitorEnter(obj any, timeout time.Duration) error {
	b, err := stubBaseOf(obj)
	if err != nil {
		return err
	}
	t := timeoutMs(timeout)
	return callVoid(func() { swig.SwigcptrRobotRaconteurNode(n.native.Swigcptr()).MonitorEnter__SWIG_0(b.inner, t) })
}

// MonitorExit releases the monitor lock of obj.
func (n *Node) MonitorExit(obj any) error {
	b, err := stubBaseOf(obj)
	if err != nil {
		return err
	}
	return callVoid(func() { n.native.MonitorExit(b.inner) })
}

// ServiceAttributes returns the attributes of the service obj belongs to.
func (n *Node) ServiceAttributes(obj any) (map[string]any, error) {
	b, err := stubBaseOf(obj)
	if err != nil {
		return nil, err
	}
	var out map[string]any
	var cerr error
	err = callVoid(func() {
		m := n.native.GetServiceAttributes(b.inner)
		if m == nil || m.Swigcptr() == 0 {
			return
		}
		defer swig.DeleteMessageElement(m)
		e, err := fromNative(m)
		if err != nil {
			cerr = err
			return
		}
		out, cerr = UnpackMapString(e, UnpackVarValue)
	})
	if err != nil {
		return nil, err
	}
	return out, cerr
}

// ServiceNodeID returns the node id of the service obj belongs to.
func (n *Node) ServiceNodeID(obj any) (NodeID, error) {
	b, err := stubBaseOf(obj)
	if err != nil {
		return NodeID{}, err
	}
	return call(func() NodeID {
		id := n.native.GetServiceNodeID(b.inner)
		defer swig.DeleteNodeID(id)
		return nodeIDFromNative(id)
	})
}

// ServiceNodeName returns the node name of the service obj belongs to.
func (n *Node) ServiceNodeName(obj any) (string, error) {
	b, err := stubBaseOf(obj)
	if err != nil {
		return "", err
	}
	return call(func() string { return n.native.GetServiceNodeName(b.inner) })
}

// ServiceName returns the service name obj belongs to.
func (n *Node) ServiceName(obj any) (string, error) {
	b, err := stubBaseOf(obj)
	if err != nil {
		return "", err
	}
	return call(func() string { return n.native.GetServiceName(b.inner) })
}
