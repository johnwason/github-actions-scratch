package robotraconteur

import swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"

// BroadcastDownsampler reduces the rate of pipe and wire broadcasts per
// client (clients request a downsample factor). Call BeginStep and EndStep
// around each broadcast cycle.
type BroadcastDownsampler struct{ native swig.BroadcastDownsampler }

// NewBroadcastDownsampler creates a downsampler for a service.
func NewBroadcastDownsampler(ctx *ServerContext, defaultDownsample uint32) (*BroadcastDownsampler, error) {
	if ctx == nil || ctx.native == nil {
		return nil, NewInvalidArgument("invalid server context")
	}
	n, err := call(func() swig.BroadcastDownsampler {
		d := swig.NewBroadcastDownsampler()
		swig.SwigcptrBroadcastDownsampler(d.Swigcptr()).Init__SWIG_0(ctx.native, defaultDownsample)
		return d
	})
	if err != nil {
		return nil, err
	}
	return &BroadcastDownsampler{native: n}, nil
}

// ClientDownsample returns the downsample factor of a client endpoint.
func (d *BroadcastDownsampler) ClientDownsample(endpoint uint32) uint32 {
	return d.native.GetClientDownsample(endpoint)
}

// SetClientDownsample sets the downsample factor of a client endpoint.
func (d *BroadcastDownsampler) SetClientDownsample(endpoint, downsample uint32) {
	d.native.SetClientDownsample(endpoint, downsample)
}

// BeginStep starts a broadcast cycle.
func (d *BroadcastDownsampler) BeginStep() { d.native.BeginStep() }

// EndStep ends a broadcast cycle.
func (d *BroadcastDownsampler) EndStep() { d.native.EndStep() }

// AddPipeBroadcasterToDownsampler downsamples a pipe broadcaster.
func AddPipeBroadcasterToDownsampler[T any](d *BroadcastDownsampler, b *PipeBroadcaster[T]) error {
	return callVoid(func() { d.native.AddPipeBroadcaster(b.native) })
}

// AddWireBroadcasterToDownsampler downsamples a wire broadcaster.
func AddWireBroadcasterToDownsampler[T any](d *BroadcastDownsampler, b *WireBroadcaster[T]) error {
	return callVoid(func() { d.native.AddWireBroadcaster(b.native) })
}
