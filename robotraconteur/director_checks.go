package robotraconteur

import swig "github.com/robotraconteur/robotraconteur_go/robotraconteur/internal/swig"

// Compile time checks that Go director implementations match the SWIG
// director method sets (SWIG Go only checks director methods at call time).
var (
	_ swig.RRNativeObjectHeapSupportMethods                   = heapSupport{}
	_ swig.AsyncRequestDirectorMethods                        = (*asyncRequest)(nil)
	_ swig.AsyncVoidReturnDirectorMethods                     = (*asyncVoid)(nil)
	_ swig.AsyncVoidNoErrReturnDirectorMethods                = (*asyncVoidNoErr)(nil)
	_ swig.AsyncStringReturnDirectorMethods                   = (*asyncString)(nil)
	_ swig.AsyncUInt32ReturnDirectorMethods                   = (*asyncUInt32)(nil)
	_ swig.AsyncStubReturnDirectorMethods                     = (*asyncStub)(nil)
	_ swig.WrappedServiceStubDirectorMethods                  = (*stubDirector)(nil)
	_ swig.ClientServiceListenerDirectorMethods               = (*clientListener)(nil)
	_ swig.AsyncPipeEndpointReturnDirectorMethods             = (*asyncPipeEndpoint[int32])(nil)
	_ swig.WrappedPipeEndpointDirectorMethods                 = (*pipeEndpointDirector[int32])(nil)
	_ swig.AsyncWireConnectionReturnDirectorMethods           = (*asyncWireConnection[int32])(nil)
	_ swig.WrappedWireConnectionDirectorMethods               = (*wireConnectionDirector[int32])(nil)
	_ swig.WrappedArrayMemoryDirectorMethods                  = (*arrayMemoryDirector[int32])(nil)
	_ swig.WrappedMultiDimArrayMemoryDirectorMethods          = (*multiDimArrayMemoryDirector[int32])(nil)
	_ swig.WrappedPodArrayMemoryDirectorMethods               = (*elementArrayMemoryDirector[int32])(nil)
	_ swig.WrappedNamedArrayMemoryDirectorMethods             = (*elementArrayMemoryDirector[int32])(nil)
	_ swig.WrappedPodMultiDimArrayMemoryDirectorMethods       = (*elementMultiDimMemoryDirector[int32])(nil)
	_ swig.WrappedNamedMultiDimArrayMemoryDirectorMethods     = (*elementMultiDimMemoryDirector[int32])(nil)
	_ swig.WrappedPodArrayMemoryClientBufferMethods           = (*arrayClientBuffer[int32])(nil)
	_ swig.WrappedNamedArrayMemoryClientBufferMethods         = (*arrayClientBuffer[int32])(nil)
	_ swig.WrappedPodMultiDimArrayMemoryClientBufferMethods   = (*mdClientBuffer[int32])(nil)
	_ swig.WrappedNamedMultiDimArrayMemoryClientBufferMethods = (*mdClientBuffer[int32])(nil)
)
