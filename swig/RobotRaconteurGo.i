// RobotRaconteurGo.i - SWIG Go module for Robot Raconteur.
//
// Uses the shared SWIG interface files from robotraconteur/SWIG (on the
// include path, unmodified). Go specific behaviour:
//   * go_shared_ptr.i      - boost::shared_ptr/intrusive_ptr support (SWIG Go has none)
//   * GoTypemaps.i         - time, enum and string typemaps
//   * GoExceptionTypemaps.i - C++ exceptions -> pending error slot -> Go error values
// Hand-written idiomatic Go API lives in the robotraconteur package; this
// module is internal (robotraconteur/internal/swig).

%module(directors="1") robotraconteurswig

#pragma SWIG nowarn=473

%{
#include <RobotRaconteur.h>
#include "RobotRaconteurWrapped.h"
#include <boost/lexical_cast.hpp>
#include <RobotRaconteur/RobotRaconteurServiceIndex_stubskel.h>
#include <boost/foreach.hpp>
using namespace RobotRaconteur;
using namespace std;
%}

// CommonInclude.i equivalent (it includes boost_shared_ptr.i, which SWIG Go lacks)
%include "go_shared_ptr.i"
%include <std_string.i>
%include <std_vector.i>
%include <std_map.i>
%include <stdint.i>
%include <attribute.i>
%include "typemaps.i"

// Per-language macro contract used by the shared .i files
%define RR_PUBLIC_OVERRIDE_METHOD(a)
%enddef
%define RR_PUBLIC_VIRTUAL_METHOD(a)
%enddef
%define RR_PROPERTY(a)
%enddef
%define RR_MAKE_METHOD_PRIVATE(a)
%enddef
%define RR_RELEASE_GIL()
%enddef
%define RR_KEEP_GIL()
%enddef
// Director returns of intrusive/shared pointers copy (add-ref) from the Go
// owned handle; Go keeps ownership of its handle, so no reset is required.
%define RR_DIRECTOR_SHARED_PTR_RETURN_DEFAULT(TYPE)
%enddef
%define RR_DIRECTOR_SHARED_PTR_RETURN_RESET(TYPE)
%enddef

%include "RobotRaconteurConstants.i"

%include "GoTypemaps.i"
%include "GoExceptionTypemaps.i"

%include "DataTypes.i"

%include "ServiceDefinition.i"
%include "NodeID.i"
%include "Message.i"
%include "AsyncHandlerDirector.i"

%include "Transport.i"
%include "TcpTransport.i"
%include "LocalTransport.i"
%include "HardwareTransport.i"
%include "IntraTransport.i"

%include "TimerGo.i"
%include "TimeSpecGo.i"
%include "TypedPacket.i"

%include "PipeMember.i"
%include "WireMember.i"
%include "MemoryMember.i"
%include "Generator.i"

%include "Client.i"
%include "Service.i"
%include "ServiceSecurity.i"

%rename(WrappedNodeInfo2) NodeInfo2;
%include "Discovery.i"
%include "Subscription.i"

%include "Logging.i"
%include "NodeDirectories.i"

%nodefaultctor RobotRaconteur::RobotRaconteurNode;
%include "RobotRaconteurNode.i"
%include "NodeSetup.i"
%include "BroadcastDownsampler.i"

%include "RRGoTestSupport.i"

namespace RobotRaconteur {

%feature("director") RRNativeObjectHeapSupport;

class RRNativeObjectHeapSupport
	{
	public:
		static void Set_Support(RRNativeObjectHeapSupport* s);
		virtual void DeleteObject_i(int32_t id)=0;
		static void DeleteObject(int32_t id);
		virtual ~RRNativeObjectHeapSupport();
	};
};
