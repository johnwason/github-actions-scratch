package robotraconteur

import (
	"time"

	swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"
)

// TimeSpec is a Robot Raconteur timestamp: seconds and nanoseconds since the
// Unix epoch (node time).
type TimeSpec struct {
	Seconds     int64
	Nanoseconds int32
}

// Time converts the TimeSpec to a time.Time.
func (t TimeSpec) Time() time.Time { return time.Unix(t.Seconds, int64(t.Nanoseconds)).UTC() }

// TimeSpecFromTime converts a time.Time to a TimeSpec.
func TimeSpecFromTime(t time.Time) TimeSpec {
	return TimeSpec{Seconds: t.Unix(), Nanoseconds: int32(t.Nanosecond())}
}

func timeSpecFromNative(t swig.TimeSpec) TimeSpec {
	if t == nil || t.Swigcptr() == 0 {
		return TimeSpec{}
	}
	return TimeSpec{Seconds: t.GetSeconds(), Nanoseconds: t.GetNanoseconds()}
}
