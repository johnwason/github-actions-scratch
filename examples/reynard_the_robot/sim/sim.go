// Package sim is a simulated Reynard the Robot implementing the
// experimental.reynard_the_robot.Reynard object. It stands in for the
// Reynard GUI used by the other language examples, so the Go example runs
// without external programs.
package sim

import (
	"math"
	"sync"
	"time"

	reynard "github.com/robotraconteur/robotraconteur_go/examples/reynard_the_robot/gen/experimental__reynard_the_robot"
	rr "github.com/robotraconteur/robotraconteur_go/robotraconteur"
)

const updatePeriod = 20 * time.Millisecond

// Reynard is the simulated robot. Create it with New and call Close when
// done.
type Reynard struct {
	reynard.ReynardDefaultImpl

	mu       sync.Mutex
	start    time.Time
	robotPos [2]float64
	robotVel [2]float64
	robotEnd time.Time // zero: no timeout
	armPos   [3]float64
	armVel   [3]float64
	armEnd   time.Time
	color    []float64
	state    *rr.WireBroadcaster[*reynard.ReynardState]
	done     chan struct{}
	wg       sync.WaitGroup
}

var _ reynard.Reynard = (*Reynard)(nil)

// New creates a simulated robot and starts its update loop.
func New() *Reynard {
	r := &Reynard{start: time.Now(), color: []float64{0.929, 0.49, 0.192}, done: make(chan struct{})}
	r.wg.Add(1)
	go r.run()
	return r
}

// Close stops the update loop.
func (r *Reynard) Close() {
	close(r.done)
	r.wg.Wait()
}

func (r *Reynard) run() {
	defer r.wg.Done()
	t := time.NewTicker(updatePeriod)
	defer t.Stop()
	last := time.Now()
	for {
		select {
		case <-r.done:
			return
		case now := <-t.C:
			st, b := r.step(now, now.Sub(last).Seconds())
			last = now
			if b != nil {
				_ = b.SetOutValue(st)
			}
		}
	}
}

// SetState is called by the service skeleton when the service is
// registered.
func (r *Reynard) SetState(w *rr.Wire[*reynard.ReynardState]) {
	b, err := rr.NewWireBroadcaster(w)
	if err != nil {
		return
	}
	st, _ := r.step(time.Now(), 0)
	_ = b.SetOutValue(st) // so clients can peek before the first update
	r.mu.Lock()
	defer r.mu.Unlock()
	r.state = b
}

func (r *Reynard) step(now time.Time, dt float64) (*reynard.ReynardState, *rr.WireBroadcaster[*reynard.ReynardState]) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if !r.robotEnd.IsZero() && now.After(r.robotEnd) {
		r.robotVel = [2]float64{}
		r.robotEnd = time.Time{}
	}
	if !r.armEnd.IsZero() && now.After(r.armEnd) {
		r.armVel = [3]float64{}
		r.armEnd = time.Time{}
	}
	for i := range r.robotPos {
		r.robotPos[i] += r.robotVel[i] * dt
	}
	for i := range r.armPos {
		r.armPos[i] += r.armVel[i] * dt
	}
	return &reynard.ReynardState{
		Time:           now.Sub(r.start).Seconds(),
		Robot_position: append([]float64(nil), r.robotPos[:]...),
		Arm_position:   append([]float64(nil), r.armPos[:]...),
		Robot_velocity: append([]float64(nil), r.robotVel[:]...),
		Arm_velocity:   append([]float64(nil), r.armVel[:]...),
	}, r.state
}

func finite(v ...float64) error {
	for _, x := range v {
		if math.IsNaN(x) || math.IsInf(x, 0) {
			return rr.NewInvalidArgument("value must be finite")
		}
	}
	return nil
}

// motionEnd returns the end time for a motion with timeout in seconds
// (<= 0: until changed).
func motionEnd(timeout float64) time.Time {
	if timeout <= 0 {
		return time.Time{}
	}
	return time.Now().Add(time.Duration(timeout * float64(time.Second)))
}

func (r *Reynard) Teleport(x, y float64) error {
	if err := finite(x, y); err != nil {
		return err
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	r.robotPos = [2]float64{x, y}
	return nil
}

func (r *Reynard) Setf_arm_position(q1, q2, q3 float64) error {
	if err := finite(q1, q2, q3); err != nil {
		return err
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	r.armPos = [3]float64{q1, q2, q3}
	r.armVel = [3]float64{}
	return nil
}

func (r *Reynard) Getf_arm_position() ([]float64, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	return append([]float64(nil), r.armPos[:]...), nil
}

func (r *Reynard) GetRobot_position() ([]float64, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	return append([]float64(nil), r.robotPos[:]...), nil
}

func (r *Reynard) Drive_robot(vx, vy, timeout float64, wait bool) error {
	if err := finite(vx, vy, timeout); err != nil {
		return err
	}
	if wait && timeout <= 0 {
		return rr.NewInvalidArgument("wait requires a timeout")
	}
	r.mu.Lock()
	r.robotVel = [2]float64{vx, vy}
	r.robotEnd = motionEnd(timeout)
	end := r.robotEnd
	r.mu.Unlock()
	if wait {
		time.Sleep(time.Until(end) + updatePeriod)
	}
	return nil
}

func (r *Reynard) Drive_arm(q1, q2, q3, timeout float64, wait bool) error {
	if err := finite(q1, q2, q3, timeout); err != nil {
		return err
	}
	if wait && timeout <= 0 {
		return rr.NewInvalidArgument("wait requires a timeout")
	}
	r.mu.Lock()
	r.armVel = [3]float64{q1, q2, q3}
	r.armEnd = motionEnd(timeout)
	end := r.armEnd
	r.mu.Unlock()
	if wait {
		time.Sleep(time.Until(end) + updatePeriod)
	}
	return nil
}

func (r *Reynard) Say(message string) error {
	for _, h := range r.New_message().Handlers() {
		h(message)
	}
	return nil
}

func (r *Reynard) GetColor() ([]float64, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	return append([]float64(nil), r.color...), nil
}

func (r *Reynard) SetColor(c []float64) error {
	if len(c) != 3 {
		return rr.NewInvalidArgument("color must have 3 elements")
	}
	if err := finite(c...); err != nil {
		return err
	}
	for _, v := range c {
		if v < 0 || v > 1 {
			return rr.NewInvalidArgument("color elements must be between 0 and 1")
		}
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	r.color = append([]float64(nil), c...)
	return nil
}
