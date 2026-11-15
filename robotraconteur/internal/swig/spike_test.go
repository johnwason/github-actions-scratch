package robotraconteurswig

// Phase 0 spike: exercises the riskiest parts of the SWIG Go layer directly.
//   - smart pointer handles (shared_ptr / intrusive_ptr) and upcasts
//   - directors called from C++ threads (WallTimer)
//   - numeric array data through MessageElement / RRBaseArray
//   - C++ exception -> pending error slot -> Go error (no panics)
//   - Go director error -> C++ exception (RRDirectorExceptionHelper)

import (
	"errors"
	"fmt"
	"runtime"
	"sync"
	"testing"
	"time"
	"unsafe"
)

// ---- minimal error plumbing (prototype of the runtime package's helper) ----

type rrError struct {
	Code    uint32
	Name    string
	Message string
}

func (e *rrError) Error() string { return fmt.Sprintf("%s: %s", e.Name, e.Message) }

func takeError() error {
	h := Rrgo_take_error()
	if h == nil || h.Swigcptr() == 0 {
		return nil
	}
	defer DeleteHandlerErrorInfo(h)
	return &rrError{Code: h.GetError_code(), Name: h.GetErrorname(), Message: h.GetErrormessage()}
}

func call[T any](f func() T) (T, error) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	Rrgo_clear_error()
	v := f()
	if err := takeError(); err != nil {
		var zero T
		return zero, err
	}
	return v, nil
}

func callVoid(f func()) error {
	_, err := call(func() struct{} { f(); return struct{}{} })
	return err
}

// ---- object heap ----

type objHeap struct {
	mu   sync.Mutex
	next int32
	objs map[int32]any
}

var heap = &objHeap{objs: map[int32]any{}}

func (h *objHeap) add(o any) int32 {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.next++
	h.objs[h.next] = o
	return h.next
}

type heapSupport struct{ deleted chan int32 }

func (s *heapSupport) DeleteObject_i(id int32) {
	heap.mu.Lock()
	o := heap.objs[id]
	delete(heap.objs, id)
	heap.mu.Unlock()
	if d, ok := o.(interface{ deleteDirector() }); ok {
		d.deleteDirector()
	}
	if s.deleted != nil {
		select {
		case s.deleted <- id:
		default:
		}
	}
}

var heapSupportDirector RRNativeObjectHeapSupport
var heapDeleted = make(chan int32, 16)

func init() {
	heapSupportDirector = NewDirectorRRNativeObjectHeapSupport(&heapSupport{deleted: heapDeleted})
	RRNativeObjectHeapSupportSet_Support(heapSupportDirector)
}

// ---- tests ----

func TestErrorFromCpp(t *testing.T) {
	v, err := call(func() int32 { return ReturnOrThrow(21) })
	if err != nil || v != 42 {
		t.Fatalf("ReturnOrThrow(21) = %v, %v", v, err)
	}
	_, err = call(func() int32 { return ReturnOrThrow(-1) })
	var rerr *rrError
	if !errors.As(err, &rerr) || rerr.Name != "RobotRaconteur.InvalidArgument" || rerr.Message != "negative" {
		t.Fatalf("expected InvalidArgument error, got %#v", err)
	}
	if rerr.Code != uint32(MessageErrorType_InvalidArgument) {
		t.Fatalf("code = %d", rerr.Code)
	}
	err = callVoid(func() { ThrowError(uint32(MessageErrorType_ConnectionError), "RobotRaconteur.ConnectionError", "boom") })
	if !errors.As(err, &rerr) || rerr.Code != uint32(MessageErrorType_ConnectionError) || rerr.Message != "boom" {
		t.Fatalf("expected ConnectionError, got %#v", err)
	}
	err = callVoid(func() { ThrowStd("std boom") })
	if !errors.As(err, &rerr) || rerr.Code != uint32(MessageErrorType_UnknownError) || rerr.Message != "std boom" {
		t.Fatalf("expected UnknownError, got %#v", err)
	}
	// String returning function with error returns zero value
	s, err := call(func() string { ThrowStd("x"); return "unreachable?" })
	_ = s
	if err == nil {
		t.Fatal("expected error")
	}
	// No error left pending
	if Rrgo_error_pending() {
		t.Fatal("error still pending")
	}
}

func TestErrorsDoNotCrossGoroutines(t *testing.T) {
	var wg sync.WaitGroup
	fail := make(chan string, 100)
	for g := 0; g < 16; g++ {
		wg.Add(1)
		go func(g int) {
			defer wg.Done()
			for i := 0; i < 2000; i++ {
				arg := i
				if (i+g)%3 == 0 {
					arg = -1
				}
				v, err := call(func() int32 { return ReturnOrThrow(int32(arg)) })
				if arg < 0 && err == nil {
					fail <- "missing error"
					return
				}
				if arg >= 0 && (err != nil || int(v) != arg*2) {
					fail <- fmt.Sprintf("unexpected %v %v", v, err)
					return
				}
				runtime.Gosched()
			}
		}(g)
	}
	wg.Wait()
	close(fail)
	for f := range fail {
		t.Fatal(f)
	}
}

type voidDirector struct {
	err   error
	panic bool
}

func (d *voidDirector) Handler(e HandlerErrorInfo) {
	defer func() {
		if r := recover(); r != nil {
			reportDirectorError(&rrError{Code: uint32(MessageErrorType_UnknownError), Name: "RobotRaconteur.UnknownError", Message: fmt.Sprint(r)})
		}
	}()
	if d.panic {
		panic("go panic in director")
	}
	if d.err != nil {
		reportDirectorError(d.err)
	}
}

func newErrorInfo(code uint32, name, msg string) HandlerErrorInfo {
	h := NewHandlerErrorInfo()
	h.SetError_code(code)
	h.SetErrorname(name)
	h.SetErrormessage(msg)
	return h
}

func reportDirectorError(err error) {
	var rerr *rrError
	if !errors.As(err, &rerr) {
		rerr = &rrError{Code: uint32(MessageErrorType_UnknownError), Name: "RobotRaconteur.UnknownError", Message: err.Error()}
	}
	info := newErrorInfo(rerr.Code, rerr.Name, rerr.Message)
	defer DeleteHandlerErrorInfo(info)
	Rrgo_director_set_error(info)
}

func TestDirectorErrorToCpp(t *testing.T) {
	for _, tc := range []struct {
		d    *voidDirector
		want string
	}{
		{&voidDirector{}, ""},
		{&voidDirector{err: &rrError{Code: uint32(MessageErrorType_InvalidOperation), Name: "RobotRaconteur.InvalidOperation", Message: "bad op"}}, "RobotRaconteur.InvalidOperation: bad op"},
		{&voidDirector{err: errors.New("plain go error")}, "RobotRaconteur.UnknownError: plain go error"},
		{&voidDirector{panic: true}, "RobotRaconteur.UnknownError: go panic in director"},
	} {
		d := NewDirectorAsyncVoidReturnDirector(tc.d)
		got, err := call(func() string { return CallVoidDirector(d) })
		DeleteDirectorAsyncVoidReturnDirector(d)
		if err != nil || got != tc.want {
			t.Errorf("CallVoidDirector = %q, %v; want %q", got, err, tc.want)
		}
	}
}

func TestSmartPointersAndArrays(t *testing.T) {
	arr, err := call(func() RRBaseArray { return AllocateRRArrayByType(DataTypes_double_t, 5) })
	if err != nil || arr == nil {
		t.Fatalf("AllocateRRArrayByType: %v", err)
	}
	if arr.Size() != 5 || arr.ElementSize() != 8 || arr.GetTypeID() != DataTypes_double_t {
		t.Fatalf("bad array %d %d %v", arr.Size(), arr.ElementSize(), arr.GetTypeID())
	}
	data := unsafe.Slice((*float64)(unsafe.Pointer(arr.Void_ptr())), arr.Size())
	copy(data, []float64{1.5, 2.5, 3.5, 4.5, 5.5})

	el := NewMessageElement()
	el.SetElementName("value")
	// RRBaseArray -> MessageElementData requires a real upcast (new handle)
	med := arr.SwigGetMessageElementData()
	el.SetData(med)
	DeleteMessageElementData(med)
	DeleteRRBaseArray(arr)

	if el.GetElementName() != "value" {
		t.Fatal("element name")
	}
	got := el.GetData()
	if got == nil {
		t.Fatal("nil data")
	}
	arr2 := MessageElementDataUtilToRRBaseArray(got)
	DeleteMessageElementData(got)
	if arr2 == nil || arr2.Size() != 5 {
		t.Fatal("ToRRBaseArray")
	}
	out := make([]float64, arr2.Size())
	copy(out, unsafe.Slice((*float64)(unsafe.Pointer(arr2.Void_ptr())), arr2.Size()))
	DeleteRRBaseArray(arr2)
	if out[0] != 1.5 || out[4] != 5.5 {
		t.Fatalf("roundtrip %v", out)
	}
	// MessageElement has a *virtual* base RRValue: the upcast must adjust the pointer
	v := el.SwigGetRRValue()
	if v == nil || v.Swigcptr() == 0 {
		t.Fatal("upcast to RRValue")
	}
	DeleteRRValue(v)
	DeleteMessageElement(el)
}

type timerDirector struct {
	ch chan HandlerErrorInfo
}

func (d *timerDirector) Handler(ev TimerEvent, err HandlerErrorInfo) {
	// Called from a Robot Raconteur thread pool thread (C++ boost thread)
	d.ch <- newErrorInfo(err.GetError_code(), err.GetErrorname(), err.GetErrormessage())
}

func (d *timerDirector) deleteDirector() {}

func TestTimerDirectorFromCppThread(t *testing.T) {
	node, err := call(func() RobotRaconteurNode { return RobotRaconteurNodeX_get_s() })
	if err != nil || node == nil {
		t.Fatalf("node: %v", err)
	}
	td := &timerDirector{ch: make(chan HandlerErrorInfo, 4)}
	d := NewDirectorAsyncTimerEventReturnDirector(td)
	id := heap.add(td)
	init := NewWallTimer_initstruct()
	init.SetHandler(d)
	init.SetId(id)
	timer, err := call(func() WallTimer { return NewWallTimer(50, true, node, init) })
	DeleteWallTimer_initstruct(init)
	if err != nil {
		t.Fatalf("NewWallTimer: %v", err)
	}
	if err := callVoid(func() { timer.Start() }); err != nil {
		t.Fatalf("Start: %v", err)
	}
	select {
	case e := <-td.ch:
		if e.GetError_code() != 0 {
			t.Fatalf("timer error %v", e.GetErrormessage())
		}
		DeleteHandlerErrorInfo(e)
	case <-time.After(5 * time.Second):
		t.Fatal("timer did not fire")
	}
	DeleteWallTimer(timer)
	// The one-shot timer releases its handler: C++ ReleaseDirector -> DeleteObject(id)
	select {
	case gotID := <-heapDeleted:
		if gotID != id {
			t.Fatalf("deleted id %d, want %d", gotID, id)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("director was not released")
	}
	DeleteDirectorAsyncTimerEventReturnDirector(d)
	DeleteRobotRaconteurNode(node)
}

func TestNodeIDError(t *testing.T) {
	id, err := call(func() NodeID { return NewNodeID("not a node id") })
	if err == nil {
		t.Fatalf("expected error, got %v", id)
	}
	id, err = call(func() NodeID { return NewNodeID("{b45ab6b0-7c84-4cdd-8a4b-7e4d0f7d3e1c}") })
	if err != nil {
		t.Fatal(err)
	}
	if s := id.ToString(); s != "{b45ab6b0-7c84-4cdd-8a4b-7e4d0f7d3e1c}" {
		t.Fatal(s)
	}
	DeleteNodeID(id)
}
