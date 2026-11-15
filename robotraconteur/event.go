package robotraconteur

import "sync"

// Event is a Robot Raconteur event member. F is the handler function type,
// e.g. func(d float64, s string).
//
// On a client, handlers added with Connect are called when the service fires
// the event (on a Robot Raconteur thread; handlers should not block). On a
// service, the generated skeleton connects to the object's events and
// forwards each Fire to all connected clients.
type Event[F any] struct {
	mu       sync.Mutex
	next     uint64
	handlers []eventHandler[F]
}

type eventHandler[F any] struct {
	id uint64
	f  F
}

// EventConnection identifies a connected handler.
type EventConnection struct {
	disconnect func()
}

// Disconnect removes the handler.
func (c *EventConnection) Disconnect() {
	if c != nil && c.disconnect != nil {
		c.disconnect()
	}
}

// Connect adds a handler.
func (e *Event[F]) Connect(f F) *EventConnection {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.next++
	id := e.next
	e.handlers = append(e.handlers, eventHandler[F]{id, f})
	return &EventConnection{disconnect: func() {
		e.mu.Lock()
		defer e.mu.Unlock()
		for i, h := range e.handlers {
			if h.id == id {
				e.handlers = append(e.handlers[:i:i], e.handlers[i+1:]...)
				return
			}
		}
	}}
}

// Handlers returns a snapshot of the connected handlers. Generated code
// fires an event by calling each handler.
func (e *Event[F]) Handlers() []F {
	e.mu.Lock()
	defer e.mu.Unlock()
	o := make([]F, len(e.handlers))
	for i, h := range e.handlers {
		o[i] = h.f
	}
	return o
}

// Callback is a Robot Raconteur callback member. F is the callback function
// type, e.g. func(a int32) (float64, error).
//
// On a client, set Function to the function the service may call. On a
// service, use GetClientFunction to obtain a function that calls the
// callback of a specific client endpoint.
type Callback[F any] struct {
	mu       sync.Mutex
	function F
	set      bool
	// client returns the function for a client endpoint (service side).
	client func(endpoint uint32) (F, error)
}

// SetFunction sets the client side callback function.
func (c *Callback[F]) SetFunction(f F) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.function, c.set = f, true
}

// Function returns the client side callback function.
func (c *Callback[F]) Function() (F, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.function, c.set
}

// GetClientFunction returns a function calling the callback of the client
// identified by endpoint (service side).
func (c *Callback[F]) GetClientFunction(endpoint uint32) (F, error) {
	c.mu.Lock()
	cl := c.client
	c.mu.Unlock()
	if cl == nil {
		var zero F
		return zero, NewInvalidOperation("callback is not attached to a service")
	}
	return cl(endpoint)
}

// RRSetClientFunctionSource is used by generated skeletons.
func (c *Callback[F]) RRSetClientFunctionSource(f func(endpoint uint32) (F, error)) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.client = f
}
