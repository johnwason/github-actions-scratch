package robotraconteur

import (
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// TimerEvent is passed to timer handlers.
type TimerEvent struct {
	// Stopped is true when the handler is called because the timer stopped.
	Stopped         bool
	LastExpected    time.Time
	LastReal        time.Time
	CurrentExpected time.Time
	CurrentReal     time.Time
}

// Timer is a periodic or one-shot timer driven by the node (it follows the
// node's time base, for example simulation time when a time provider is
// installed). Create with Node.CreateTimer.
type Timer struct {
	native swig.Timer
}

type timerHandler struct {
	f func(TimerEvent, error)
}

func (h *timerHandler) Handler(ev swig.TimerEvent, info swig.HandlerErrorInfo) {
	defer recoverDirector()
	err := errorFromInfo(info)
	var e TimerEvent
	if ev != nil && ev.Swigcptr() != 0 {
		e = TimerEvent{
			Stopped:         ev.GetStopped(),
			LastExpected:    timeFromMicros(ev.GetLast_expected()),
			LastReal:        timeFromMicros(ev.GetLast_real()),
			CurrentExpected: timeFromMicros(ev.GetCurrent_expected()),
			CurrentReal:     timeFromMicros(ev.GetCurrent_real()),
		}
	}
	h.f(e, err)
}

var _ swig.AsyncTimerEventReturnDirectorMethods = (*timerHandler)(nil)

// CreateTimer creates a timer that calls handler every period (once if
// oneshot). Call Start to start it. The handler runs on a node thread pool
// thread; it receives an error if the timer fails. Close releases the timer.
func (n *Node) CreateTimer(period time.Duration, oneshot bool, handler func(TimerEvent, error)) (*Timer, error) {
	if handler == nil {
		return nil, NewInvalidArgument("nil timer handler")
	}
	if period <= 0 {
		return nil, NewInvalidArgument("timer period must be positive")
	}
	impl := &timerHandler{f: handler}
	d := swig.NewDirectorAsyncTimerEventReturnDirector(impl)
	id := heapAdd(impl, func() { swig.DeleteDirectorAsyncTimerEventReturnDirector(d) })
	t, err := call(func() swig.Timer { return n.native.CreateTimer(timeoutMs(period), oneshot, d, id) })
	if err != nil {
		heapRemove(id)
		return nil, err
	}
	return &Timer{native: t}, nil
}

// Start starts the timer.
func (t *Timer) Start() error { return callVoid(func() { t.native.Start() }) }

// Stop stops the timer. It returns an error if the timer is not running.
func (t *Timer) Stop() error { return callVoid(func() { t.native.Stop() }) }

// TryStop stops the timer if it is running.
func (t *Timer) TryStop() error { return callVoid(func() { t.native.TryStop() }) }

// Period returns the timer period.
func (t *Timer) Period() time.Duration {
	return time.Duration(t.native.GetPeriod()) * time.Millisecond
}

// SetPeriod changes the timer period.
func (t *Timer) SetPeriod(period time.Duration) error {
	return callVoid(func() { t.native.SetPeriod(timeoutMs(period)) })
}

// IsRunning reports whether the timer is running.
func (t *Timer) IsRunning() bool { return t.native.IsRunning() }

// Close stops the timer and releases it.
func (t *Timer) Close() error {
	if t.native == nil {
		return nil
	}
	err := t.TryStop()
	swig.DeleteTimer(t.native)
	t.native = nil
	return err
}

// Rate sleeps to keep a loop at a fixed frequency, using the node's time
// base. Create with Node.CreateRate.
type Rate struct {
	native swig.Rate
}

// CreateRate creates a rate with frequency in Hz.
func (n *Node) CreateRate(frequency float64) (*Rate, error) {
	if !(frequency > 0) {
		return nil, NewInvalidArgument("rate frequency must be positive")
	}
	r, err := call(func() swig.Rate { return n.native.CreateRate(frequency) })
	if err != nil {
		return nil, err
	}
	return &Rate{native: r}, nil
}

// Sleep sleeps until the next period.
func (r *Rate) Sleep() error { return callVoid(func() { r.native.Sleep() }) }

// Close releases the rate.
func (r *Rate) Close() {
	if r.native != nil {
		swig.DeleteRate(r.native)
		r.native = nil
	}
}
