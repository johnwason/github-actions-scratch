/* go_shared_ptr.i
 *
 * boost::shared_ptr / boost::intrusive_ptr support for the SWIG Go module.
 * SWIG 4.2 ships these for C#/Java/Python but not Go. This is a port of
 * Lib/csharp/boost_shared_ptr.i (C-side typemaps are language independent)
 * with Go proxy-side typemaps.
 *
 * Representation: every Go proxy (SwigcptrT) for a class declared with
 * %shared_ptr(T) or %rr_intrusive_ptr(T) holds a heap allocated
 * boost::shared_ptr<T>*. Intrusive pointers are wrapped in a shared_ptr whose
 * deleter calls intrusive_ptr_release (SWIG_intrusive_deleter), exactly like
 * the C# binding. Go owns every handle it receives (return values and
 * director arguments) and must call DeleteT(). Handles returned *from* Go
 * director methods are transferred to C++ (which deletes them).
 *
 * Passing a nil handle where C++ requires an object is a programmer error and
 * panics (_swig_gopanic), matching SWIG's own Go typemaps. Runtime errors
 * from C++ never panic; see GoExceptionTypemaps.i.
 */

%include <shared_ptr.i>

namespace boost { template<class T> class intrusive_ptr {}; }


%define SWIG_SHARED_PTR_TYPEMAPS(CONST, TYPE...)

%naturalvar TYPE;
%naturalvar SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >;

%feature("unref") TYPE "(void)arg1; delete smartarg1;"


// Go side: SWIG's default handling treats shared_ptr<T> as an (undefined)
// interface type Boost_shared_ptr_Sl_T_Sg_ passed as a pointer-sized handle.
// swig/postprocess_go.py turns those placeholder types into aliases of the
// real proxy types. These typemaps only add nil safety.
%typemap(goout) SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >,
                SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > &,
                SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > *
%{ if $input != nil && $input.Swigcptr() == 0 { $result = nil } else { $result = $input } %}

// plain value
%typemap(in) CONST TYPE ($&1_type argp = 0) %{
  argp = (*(SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > **)&$input) ? (*(SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > **)&$input)->get() : 0;
  if (!argp) { _swig_gopanic("Attempt to dereference null $1_type"); }
  $1 = *argp; %}
%typemap(out) CONST TYPE
%{ *(SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > **)&$result = new SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >(new $1_ltype($1)); %}

// plain pointer (also used for 'self')
%typemap(in) CONST TYPE * (SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > *smartarg = 0) %{
  smartarg = *(SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > **)&$input;
  $1 = (TYPE *)(smartarg ? smartarg->get() : 0); %}
%typemap(out, fragment="SWIG_null_deleter") CONST TYPE * %{
  *(SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > **)&$result = $1 ? new SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >($1 SWIG_NO_NULL_DELETER_$owner) : 0; %}
%typemap(directorin, fragment="SWIG_null_deleter") CONST TYPE *
%{ *(SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > **)&$input = $1 ? new SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >($1 SWIG_NO_NULL_DELETER_0) : 0; %}

// plain reference
%typemap(in) CONST TYPE & %{
  $1 = ($1_ltype)((*(SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > **)&$input) ? (*(SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > **)&$input)->get() : 0);
  if (!$1) { _swig_gopanic("$1_type reference is null"); } %}
%typemap(out, fragment="SWIG_null_deleter") CONST TYPE &
%{ *(SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > **)&$result = new SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >($1 SWIG_NO_NULL_DELETER_$owner); %}
%typemap(directorin, fragment="SWIG_null_deleter") CONST TYPE &
%{ *(SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > **)&$input = new SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >(&$1 SWIG_NO_NULL_DELETER_0); %}

// shared_ptr by value
%typemap(in) SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >
%{ if (*($&1_ltype*)&$input) $1 = **($&1_ltype*)&$input; %}
%typemap(out) SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >
%{ *($&1_ltype*)&$result = $1 ? new $1_ltype($1) : 0; %}
%typemap(directorin) SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >
%{ *($&1_ltype*)&$input = $1 ? new $1_ltype($1) : 0; %}
// Director returns: the Go handle is transferred to C++ and deleted here
// (Go code must not delete handles it returns from director methods).
%typemap(directorout) SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >
%{ if (*($&1_ltype*)&$input) { $result = **($&1_ltype*)&$input; delete *($&1_ltype*)&$input; } %}

// shared_ptr by reference
%typemap(in) SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > & ($*1_ltype tempnull)
%{ $1 = *($&1_ltype)&$input ? *($&1_ltype)&$input : &tempnull; %}
%typemap(out) SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > &
%{ *($&1_ltype)&$result = *$1 ? new $*1_ltype(*$1) : 0; %}
%typemap(directorin) SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > &
%{ *($&1_ltype)&$input = $1 ? new $*1_ltype($1) : 0; %}

// shared_ptr by pointer
%typemap(in) SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > * ($*1_ltype tempnull)
%{ $1 = *($&1_ltype)&$input ? *($&1_ltype)&$input : &tempnull; %}
%typemap(out) SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > *
%{ *($&1_ltype)&$result = ($1 && *$1) ? new $*1_ltype(*$1) : 0;
   if ($owner) delete $1; %}

%typemap(in) CONST TYPE[], CONST TYPE[ANY], CONST TYPE (CLASS::*) %{
#error "typemaps for $1_type not available"
%}

%typemap(typecheck, precedence=SWIG_TYPECHECK_POINTER, equivalent="TYPE *")
  TYPE CONST, TYPE CONST &, TYPE CONST *, TYPE *CONST&,
  SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >,
  SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > &,
  SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > *,
  SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE > *& ""

%template() SWIG_SHARED_PTR_QNAMESPACE::shared_ptr< CONST TYPE >;
%enddef

/* ---- intrusive_ptr: represented as shared_ptr<T> with SWIG_intrusive_deleter ---- */
%fragment("SWIG_intrusive_deleter", "header") %{
template<class T> struct SWIG_intrusive_deleter {
    void operator()(T *p) { if (p) intrusive_ptr_release(p); }
};
%}

%define %rr_intrusive_ptr(TYPE...)
%shared_ptr(TYPE)
%feature("novaluewrapper") boost::intrusive_ptr< TYPE >;
%template() boost::intrusive_ptr< TYPE >;


%typemap(goout) boost::intrusive_ptr< TYPE >, boost::intrusive_ptr< TYPE > &, const boost::intrusive_ptr< TYPE > &
%{ if $input != nil && $input.Swigcptr() == 0 { $result = nil } else { $result = $input } %}

// Raw pointers/references to intrusive types (including constructor results)
// must take an intrusive reference: the object's lifetime is governed by its
// intrusive count, which C++ intrusive_ptrs share with the Go handle.
%typemap(out, fragment="SWIG_intrusive_deleter") TYPE *, const TYPE *
%{ if ($1) { intrusive_ptr_add_ref($1);
     *(boost::shared_ptr< TYPE >**)&$result = new boost::shared_ptr< TYPE >($1, SWIG_intrusive_deleter< TYPE >()); }
   else { *(boost::shared_ptr< TYPE >**)&$result = 0; } %}
%typemap(out, fragment="SWIG_intrusive_deleter") TYPE &, const TYPE &
%{ intrusive_ptr_add_ref($1);
   *(boost::shared_ptr< TYPE >**)&$result = new boost::shared_ptr< TYPE >($1, SWIG_intrusive_deleter< TYPE >()); %}
%typemap(directorin, fragment="SWIG_intrusive_deleter") TYPE *, const TYPE *
%{ if ($1) { intrusive_ptr_add_ref($1);
     *(boost::shared_ptr< TYPE >**)&$input = new boost::shared_ptr< TYPE >($1, SWIG_intrusive_deleter< TYPE >()); }
   else { *(boost::shared_ptr< TYPE >**)&$input = 0; } %}

%typemap(in) boost::intrusive_ptr< TYPE >
%{ { boost::shared_ptr< TYPE >* smartarg = *(boost::shared_ptr< TYPE >**)&$input;
     if (smartarg) $1 = boost::intrusive_ptr< TYPE >(smartarg->get(), true); } %}
%typemap(in) const boost::intrusive_ptr< TYPE > & (boost::intrusive_ptr< TYPE > temp), boost::intrusive_ptr< TYPE > & (boost::intrusive_ptr< TYPE > temp)
%{ { boost::shared_ptr< TYPE >* smartarg = *(boost::shared_ptr< TYPE >**)&$input;
     if (smartarg) temp = boost::intrusive_ptr< TYPE >(smartarg->get(), true);
     $1 = &temp; } %}
%typemap(out, fragment="SWIG_intrusive_deleter") boost::intrusive_ptr< TYPE >
%{ if ($1) { intrusive_ptr_add_ref($1.get());
     *(boost::shared_ptr< TYPE >**)&$result = new boost::shared_ptr< TYPE >($1.get(), SWIG_intrusive_deleter< TYPE >()); }
   else { *(boost::shared_ptr< TYPE >**)&$result = 0; } %}
%typemap(out, fragment="SWIG_intrusive_deleter") const boost::intrusive_ptr< TYPE > &, boost::intrusive_ptr< TYPE > &
%{ if (*$1) { intrusive_ptr_add_ref($1->get());
     *(boost::shared_ptr< TYPE >**)&$result = new boost::shared_ptr< TYPE >($1->get(), SWIG_intrusive_deleter< TYPE >()); }
   else { *(boost::shared_ptr< TYPE >**)&$result = 0; } %}
%typemap(directorin, fragment="SWIG_intrusive_deleter") boost::intrusive_ptr< TYPE >, const boost::intrusive_ptr< TYPE > &
%{ if ($1) { intrusive_ptr_add_ref($1.get());
     *(boost::shared_ptr< TYPE >**)&$input = new boost::shared_ptr< TYPE >($1.get(), SWIG_intrusive_deleter< TYPE >()); }
   else { *(boost::shared_ptr< TYPE >**)&$input = 0; } %}
%typemap(directorout) boost::intrusive_ptr< TYPE >
%{ { boost::shared_ptr< TYPE >* smartarg = *(boost::shared_ptr< TYPE >**)&$input;
     if (smartarg) { $result = boost::intrusive_ptr< TYPE >(smartarg->get(), true); delete smartarg; } } %}
%typemap(typecheck, precedence=SWIG_TYPECHECK_POINTER, equivalent="TYPE *")
  boost::intrusive_ptr< TYPE >, boost::intrusive_ptr< TYPE > &, const boost::intrusive_ptr< TYPE > & ""
%enddef

