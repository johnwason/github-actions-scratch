package robotraconteur

import (
	"sort"
	"sync"
)

// ServiceFactory is implemented by the code generated for each Robot
// Raconteur service definition (RobotRaconteurGenGo). It provides the
// service definition text and creates client stubs and service skeletons for
// the service's object types.
type ServiceFactory interface {
	// ServiceName is the service definition name, e.g. "experimental.reynard_the_robot".
	ServiceName() string
	// DefString is the service definition text.
	DefString() string
	// CreateStub creates a client object reference for an object of this
	// service. objectType is the fully qualified object type.
	CreateStub(objectType string, base *StubBase) (any, error)
	// CreateSkel creates the service skeleton for obj, which implements
	// objectType (fully qualified).
	CreateSkel(objectType string, obj any, base *SkelBase) (ServiceSkel, error)
	// DownCastError converts a RemoteError with a service defined error name
	// into the generated error type. Returns nil if the name is unknown.
	DownCastError(e *Error) error
}

var factoryRegistry = struct {
	mu        sync.RWMutex
	factories map[string]ServiceFactory
}{factories: map[string]ServiceFactory{}}

// AddServiceFactory adds a generated service factory to the process wide
// registry. Generated packages call this from init. Registered factories are
// registered with a node by the NodeSetup helpers or Node.RegisterServiceTypes.
func AddServiceFactory(f ServiceFactory) {
	factoryRegistry.mu.Lock()
	defer factoryRegistry.mu.Unlock()
	factoryRegistry.factories[f.ServiceName()] = f
}

// ServiceFactories returns all factories added with AddServiceFactory,
// sorted by service name.
func ServiceFactories() []ServiceFactory {
	factoryRegistry.mu.RLock()
	defer factoryRegistry.mu.RUnlock()
	o := make([]ServiceFactory, 0, len(factoryRegistry.factories))
	for _, f := range factoryRegistry.factories {
		o = append(o, f)
	}
	sort.Slice(o, func(i, j int) bool { return o[i].ServiceName() < o[j].ServiceName() })
	return o
}

func lookupFactory(name string) ServiceFactory {
	factoryRegistry.mu.RLock()
	defer factoryRegistry.mu.RUnlock()
	return factoryRegistry.factories[name]
}

func init() {
	errorDowncaster = func(e *Error) error {
		ns := splitQualifiedName(e.Name)
		if ns == "" {
			return nil
		}
		if f := lookupFactory(ns); f != nil {
			return f.DownCastError(e)
		}
		return nil
	}
}

// splitQualifiedName returns the service name part of a qualified type name
// ("a.b.c.Type" -> "a.b.c").
func splitQualifiedName(name string) string {
	for i := len(name) - 1; i >= 0; i-- {
		if name[i] == '.' {
			return name[:i]
		}
	}
	return ""
}

// SplitQualifiedName splits "a.b.c.Type" into ("a.b.c", "Type").
func SplitQualifiedName(name string) (string, string) {
	ns := splitQualifiedName(name)
	if ns == "" {
		return "", name
	}
	return ns, name[len(ns)+1:]
}
